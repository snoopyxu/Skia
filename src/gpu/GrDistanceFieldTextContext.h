/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrDistanceFieldTextContext_DEFINED
#define GrDistanceFieldTextContext_DEFINED

#include "GrTextContext.h"

class GrGeometryProcessor;
class GrTextStrike;

/*
 * This class implements GrTextContext using distance field fonts
 */
class GrDistanceFieldTextContext : public GrTextContext {
public:
    GrDistanceFieldTextContext(GrContext*, const SkDeviceProperties&, bool enable);
    virtual ~GrDistanceFieldTextContext();

    virtual bool canDraw(const SkPaint& paint) SK_OVERRIDE;

    virtual void drawText(const GrPaint&, const SkPaint&, const char text[], size_t byteLength,
                          SkScalar x, SkScalar y) SK_OVERRIDE;
    virtual void drawPosText(const GrPaint&, const SkPaint&,
                             const char text[], size_t byteLength,
                             const SkScalar pos[], int scalarsPerPosition,
                             const SkPoint& offset) SK_OVERRIDE;

private:
    enum {
        kMinRequestedGlyphs      = 1,
        kDefaultRequestedGlyphs  = 64,
        kMinRequestedVerts       = kMinRequestedGlyphs * 4,
        kDefaultRequestedVerts   = kDefaultRequestedGlyphs * 4,
    };

    GrTextStrike*                      fStrike;
    SkScalar                           fTextRatio;
    bool                               fUseLCDText;
    bool                               fEnableDFRendering;
    SkAutoTUnref<GrGeometryProcessor>  fCachedGeometryProcessor;
    // Used to check whether fCachedEffect is still valid.
    uint32_t                           fEffectTextureUniqueID;
    SkColor                            fEffectColor;
    uint32_t                           fEffectFlags;
    GrTexture*                         fGammaTexture;
    void*                              fVertices;
    int32_t                            fMaxVertices;
    GrTexture*                         fCurrTexture;
    int                                fCurrVertex;
    SkRect                             fVertexBounds;

    void init(const GrPaint&, const SkPaint&);
    void appendGlyph(GrGlyph::PackedID, SkFixed left, SkFixed top, GrFontScaler*);
    void setupCoverageEffect(const SkColor& filteredColor);
    void flush();                 // automatically called by destructor
    void finish();
};

#endif
