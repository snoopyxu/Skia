/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkBitmapCache.h"
#include "SkResourceCache.h"
#include "SkMipMap.h"
#include "SkRect.h"

SkBitmap::Allocator* SkBitmapCache::GetAllocator() {
    return SkResourceCache::GetAllocator();
}

/**
 This function finds the bounds of the bitmap *within its pixelRef*.
 If the bitmap lacks a pixelRef, it will return an empty rect, since
 that doesn't make sense.  This may be a useful enough function that
 it should be somewhere else (in SkBitmap?).
 */
static SkIRect get_bounds_from_bitmap(const SkBitmap& bm) {
    if (!(bm.pixelRef())) {
        return SkIRect::MakeEmpty();
    }
    SkIPoint origin = bm.pixelRefOrigin();
    return SkIRect::MakeXYWH(origin.fX, origin.fY, bm.width(), bm.height());
}

struct BitmapKey : public SkResourceCache::Key {
public:
    BitmapKey(uint32_t genID, SkScalar scaleX, SkScalar scaleY, const SkIRect& bounds)
    : fGenID(genID)
    , fScaleX(scaleX)
    , fScaleY(scaleY)
    , fBounds(bounds)
    {
        this->init(sizeof(fGenID) + sizeof(fScaleX) + sizeof(fScaleY) + sizeof(fBounds));
    }

    uint32_t    fGenID;
    SkScalar    fScaleX;
    SkScalar    fScaleY;
    SkIRect     fBounds;
};

//////////////////////////////////////////////////////////////////////////////////////////

struct BitmapRec : public SkResourceCache::Rec {
    BitmapRec(uint32_t genID, SkScalar scaleX, SkScalar scaleY, const SkIRect& bounds,
              const SkBitmap& result)
        : fKey(genID, scaleX, scaleY, bounds)
        , fBitmap(result)
    {}

    BitmapKey   fKey;
    SkBitmap    fBitmap;

    virtual const Key& getKey() const SK_OVERRIDE { return fKey; }
    virtual size_t bytesUsed() const SK_OVERRIDE { return sizeof(fKey) + fBitmap.getSize(); }

    static bool Visitor(const SkResourceCache::Rec& baseRec, void* contextBitmap) {
        const BitmapRec& rec = static_cast<const BitmapRec&>(baseRec);
        SkBitmap* result = (SkBitmap*)contextBitmap;

        *result = rec.fBitmap;
        result->lockPixels();
        return SkToBool(result->getPixels());
    }
};

#define CHECK_LOCAL(localCache, localName, globalName, ...) \
    ((localCache) ? localCache->localName(__VA_ARGS__) : SkResourceCache::globalName(__VA_ARGS__))

bool SkBitmapCache::Find(const SkBitmap& src, SkScalar invScaleX, SkScalar invScaleY, SkBitmap* result,
                         SkResourceCache* localCache) {
    if (0 == invScaleX || 0 == invScaleY) {
        // degenerate, and the key we use for mipmaps
        return false;
    }
    BitmapKey key(src.getGenerationID(), invScaleX, invScaleY, get_bounds_from_bitmap(src));

    return CHECK_LOCAL(localCache, find, Find, key, BitmapRec::Visitor, result);
}

void SkBitmapCache::Add(const SkBitmap& src, SkScalar invScaleX, SkScalar invScaleY,
                        const SkBitmap& result, SkResourceCache* localCache) {
    if (0 == invScaleX || 0 == invScaleY) {
        // degenerate, and the key we use for mipmaps
        return;
    }
    SkASSERT(result.isImmutable());
    BitmapRec* rec = SkNEW_ARGS(BitmapRec, (src.getGenerationID(), invScaleX, invScaleY,
                                            get_bounds_from_bitmap(src), result));
    CHECK_LOCAL(localCache, add, Add, rec);
}

bool SkBitmapCache::Find(uint32_t genID, const SkIRect& subset, SkBitmap* result,
                         SkResourceCache* localCache) {
    BitmapKey key(genID, SK_Scalar1, SK_Scalar1, subset);

    return CHECK_LOCAL(localCache, find, Find, key, BitmapRec::Visitor, result);
}

bool SkBitmapCache::Add(uint32_t genID, const SkIRect& subset, const SkBitmap& result,
                        SkResourceCache* localCache) {
    SkASSERT(result.isImmutable());

    if (subset.isEmpty()
        || subset.top() < 0
        || subset.left() < 0
        || result.width() != subset.width()
        || result.height() != subset.height()) {
        return false;
    } else {
        BitmapRec* rec = SkNEW_ARGS(BitmapRec, (genID, SK_Scalar1, SK_Scalar1, subset, result));

        CHECK_LOCAL(localCache, add, Add, rec);
        return true;
    }
}
//////////////////////////////////////////////////////////////////////////////////////////

struct MipMapRec : public SkResourceCache::Rec {
    MipMapRec(const SkBitmap& src, const SkMipMap* result)
        : fKey(src.getGenerationID(), 0, 0, get_bounds_from_bitmap(src))
        , fMipMap(result)
    {
        fMipMap->attachToCacheAndRef();
    }

    virtual ~MipMapRec() {
        fMipMap->detachFromCacheAndUnref();
    }

    virtual const Key& getKey() const SK_OVERRIDE { return fKey; }
    virtual size_t bytesUsed() const SK_OVERRIDE { return sizeof(fKey) + fMipMap->size(); }

    static bool Visitor(const SkResourceCache::Rec& baseRec, void* contextMip) {
        const MipMapRec& rec = static_cast<const MipMapRec&>(baseRec);
        const SkMipMap* mm = SkRef(rec.fMipMap);
        // the call to ref() above triggers a "lock" in the case of discardable memory,
        // which means we can now check for null (in case the lock failed).
        if (NULL == mm->data()) {
            mm->unref();    // balance our call to ref()
            return false;
        }
        // the call must call unref() when they are done.
        *(const SkMipMap**)contextMip = mm;
        return true;
    }

private:
    BitmapKey       fKey;
    const SkMipMap* fMipMap;
};

const SkMipMap* SkMipMapCache::FindAndRef(const SkBitmap& src, SkResourceCache* localCache) {
    BitmapKey key(src.getGenerationID(), 0, 0, get_bounds_from_bitmap(src));
    const SkMipMap* result;

    if (!CHECK_LOCAL(localCache, find, Find, key, MipMapRec::Visitor, &result)) {
        result = NULL;
    }
    return result;
}

static SkResourceCache::DiscardableFactory get_fact(SkResourceCache* localCache) {
    return localCache ? localCache->GetDiscardableFactory()
                      : SkResourceCache::GetDiscardableFactory();
}

const SkMipMap* SkMipMapCache::AddAndRef(const SkBitmap& src, SkResourceCache* localCache) {
    SkMipMap* mipmap = SkMipMap::Build(src, get_fact(localCache));
    if (mipmap) {
        MipMapRec* rec = SkNEW_ARGS(MipMapRec, (src, mipmap));
        CHECK_LOCAL(localCache, add, Add, rec);
    }
    return mipmap;
}

