/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrSurface.h"
#include "GrSurfacePriv.h"

#include "SkBitmap.h"
#include "SkGr.h"
#include "SkImageEncoder.h"
#include <stdio.h>

SkImageInfo GrSurface::info() const {
    SkColorType colorType;
    if (!GrPixelConfig2ColorType(this->config(), &colorType)) {
        sk_throw();
    }
    return SkImageInfo::Make(this->width(), this->height(), colorType, kPremul_SkAlphaType);
}

// TODO: This should probably be a non-member helper function. It might only be needed in
// debug or developer builds.
bool GrSurface::savePixels(const char* filename) {
    SkBitmap bm;
    if (!bm.tryAllocPixels(SkImageInfo::MakeN32Premul(this->width(), this->height()))) {
        return false;
    }

    bool result = this->readPixels(0, 0, this->width(), this->height(), kSkia8888_GrPixelConfig,
                                   bm.getPixels());
    if (!result) {
        SkDebugf("------ failed to read pixels for %s\n", filename);
        return false;
    }

    // remove any previous version of this file
    remove(filename);

    if (!SkImageEncoder::EncodeFile(filename, bm, SkImageEncoder::kPNG_Type, 100)) {
        SkDebugf("------ failed to encode %s\n", filename);
        remove(filename);   // remove any partial file
        return false;
    }

    return true;
}

void GrSurface::flushWrites() {
    if (!this->wasDestroyed()) {
        this->getContext()->flushSurfaceWrites(this);
    }
}

bool GrSurface::hasPendingRead() const {
    const GrTexture* thisTex = this->asTexture();
    if (thisTex && thisTex->internalHasPendingRead()) {
        return true;
    }
    const GrRenderTarget* thisRT = this->asRenderTarget();
    if (thisRT && thisRT->internalHasPendingRead()) {
        return true;
    }
    return false;
}

bool GrSurface::hasPendingWrite() const {
    const GrTexture* thisTex = this->asTexture();
    if (thisTex && thisTex->internalHasPendingWrite()) {
        return true;
    }
    const GrRenderTarget* thisRT = this->asRenderTarget();
    if (thisRT && thisRT->internalHasPendingWrite()) {
        return true;
    }
    return false;
}

bool GrSurface::hasPendingIO() const {
    const GrTexture* thisTex = this->asTexture();
    if (thisTex && thisTex->internalHasPendingIO()) {
        return true;
    }
    const GrRenderTarget* thisRT = this->asRenderTarget();
    if (thisRT && thisRT->internalHasPendingIO()) {
        return true;
    }
    return false;
}

bool GrSurface::isSameAs(const GrSurface* other) const {
    const GrRenderTarget* thisRT = this->asRenderTarget();
    if (thisRT) {
        return thisRT == other->asRenderTarget();
    } else {
        const GrTexture* thisTex = this->asTexture();
        SkASSERT(thisTex); // We must be one or the other
        return thisTex == other->asTexture();
    }
}
