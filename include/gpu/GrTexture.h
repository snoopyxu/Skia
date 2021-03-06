
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrTexture_DEFINED
#define GrTexture_DEFINED

#include "GrSurface.h"
#include "GrRenderTarget.h"
#include "SkPoint.h"
#include "SkRefCnt.h"

class GrResourceKey;
class GrTextureParams;
class GrTexturePriv;

class GrTexture : public GrSurface {
public:
    /**
     *  Approximate number of bytes used by the texture
     */
    virtual size_t gpuMemorySize() const SK_OVERRIDE;

    // GrSurface overrides
    virtual bool readPixels(int left, int top, int width, int height,
                            GrPixelConfig config,
                            void* buffer,
                            size_t rowBytes = 0,
                            uint32_t pixelOpsFlags = 0) SK_OVERRIDE;

    virtual void writePixels(int left, int top, int width, int height,
                             GrPixelConfig config,
                             const void* buffer,
                             size_t rowBytes = 0,
                             uint32_t pixelOpsFlags = 0) SK_OVERRIDE;

    virtual GrTexture* asTexture() SK_OVERRIDE { return this; }
    virtual const GrTexture* asTexture() const SK_OVERRIDE { return this; }
    virtual GrRenderTarget* asRenderTarget() SK_OVERRIDE { return fRenderTarget.get(); }
    virtual const GrRenderTarget* asRenderTarget() const SK_OVERRIDE { return fRenderTarget.get(); }

    /**
     *  Return the native ID or handle to the texture, depending on the
     *  platform. e.g. on OpenGL, return the texture ID.
     */
    virtual GrBackendObject getTextureHandle() const = 0;

    /**
     * This function indicates that the texture parameters (wrap mode, filtering, ...) have been
     * changed externally to Skia.
     */
    virtual void textureParamsModified() = 0;

    /**
     * Informational texture flags. This will be removed soon.
     */
    enum FlagBits {
        kFirstBit = (kLastPublic_GrTextureFlagBit << 1),

        /**
         * This texture should be returned to the texture cache when
         * it is no longer reffed
         */
        kReturnToCache_FlagBit        = kFirstBit,
    };

    void resetFlag(GrTextureFlags flags) {
        fDesc.fFlags = fDesc.fFlags & ~flags;
    }

#ifdef SK_DEBUG
    void validate() const {
        this->INHERITED::validate();
        this->validateDesc();
    }
#endif

    /** Access methods that are only to be used within Skia code. */
    inline GrTexturePriv texturePriv();
    inline const GrTexturePriv texturePriv() const;

protected:
    // A texture refs its rt representation but not vice-versa. It is up to
    // the subclass constructor to initialize this pointer.
    SkAutoTUnref<GrRenderTarget> fRenderTarget;

    GrTexture(GrGpu* gpu, bool isWrapped, const GrTextureDesc& desc);

    virtual ~GrTexture();

    // GrResource overrides
    virtual void onRelease() SK_OVERRIDE;
    virtual void onAbandon() SK_OVERRIDE;

    void validateDesc() const;

private:
    void dirtyMipMaps(bool mipMapsDirty);

    enum MipMapsStatus {
        kNotAllocated_MipMapsStatus,
        kAllocated_MipMapsStatus,
        kValid_MipMapsStatus
    };

    MipMapsStatus   fMipMapsStatus;
    // These two shift a fixed-point value into normalized coordinates	
    // for this texture if the texture is power of two sized.
    int             fShiftFixedX;
    int             fShiftFixedY;

    friend class GrTexturePriv;

    typedef GrSurface INHERITED;
};

/**
 * Represents a texture that is intended to be accessed in device coords with an offset.
 */
class GrDeviceCoordTexture {
public:
    GrDeviceCoordTexture() { fOffset.set(0, 0); }

    GrDeviceCoordTexture(const GrDeviceCoordTexture& other) {
        *this = other;
    }

    GrDeviceCoordTexture(GrTexture* texture, const SkIPoint& offset)
        : fTexture(SkSafeRef(texture))
        , fOffset(offset) {
    }

    GrDeviceCoordTexture& operator=(const GrDeviceCoordTexture& other) {
        fTexture.reset(SkSafeRef(other.fTexture.get()));
        fOffset = other.fOffset;
        return *this;
    }

    const SkIPoint& offset() const { return fOffset; }

    void setOffset(const SkIPoint& offset) { fOffset = offset; }
    void setOffset(int ox, int oy) { fOffset.set(ox, oy); }

    GrTexture* texture() const { return fTexture.get(); }

    GrTexture* setTexture(GrTexture* texture) {
        fTexture.reset(SkSafeRef(texture));
        return texture;
    }

private:
    SkAutoTUnref<GrTexture> fTexture;
    SkIPoint                fOffset;
};

#endif
