
/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkGLContext_DEFINED
#define SkGLContext_DEFINED

#include "GrGLInterface.h"

/**
 * Create an offscreen opengl context with an RGBA8 / 8bit stencil FBO.
 * Provides a GrGLInterface struct of function pointers for the context.
 */

class SK_API SkGLContext : public SkRefCnt {
public:
    SK_DECLARE_INST_COUNT(SkGLContext)

    SkGLContext();
    virtual ~SkGLContext();

    /**
     * Initializes the context and makes it current.
     */
    bool init(GrGLStandard forcedGpuAPI, const int width, const int height);

    int getFBOID() const { return fFBO; }

    const GrGLInterface* gl() const { return fGL; }

    virtual void makeCurrent() const = 0;

    /**
     * The primary purpose of this function it to provide a means of scheduling
     * work on the GPU (since all of the subclasses create primary buffers for
     * testing that are small and not meant to be rendered to the screen).
     *
     * If the drawing surface provided by the platform is double buffered this
     * call will cause the platform to swap which buffer is currently being
     * targeted.  If the current surface does not include a back buffer, this
     * call has no effect.
     */
    virtual void swapBuffers() const = 0;

    bool hasExtension(const char* extensionName) const {
        SkASSERT(fGL);
        return fGL->hasExtension(extensionName);
    }

    /**
     * This notifies the context that we are deliberately testing abandoning
     * the context. It is useful for debugging contexts that would otherwise
     * test that GPU resources are properly deleted. It also allows a debugging
     * context to test that further GL calls are not made by Skia GPU code.
     */
    void testAbandon();

protected:
    /**
     * Subclass implements this to make a GL context. The returned GrGLInterface
     * should be populated with functions compatible with the context. The
     * format and size of backbuffers does not matter since an FBO will be
     * created.
     */
    virtual const GrGLInterface* createGLContext(GrGLStandard forcedGpuAPI) = 0;

    /**
     * Subclass should destroy the underlying GL context.
     */
    virtual void destroyGLContext() = 0;

private:
    GrGLuint fFBO;
    GrGLuint fColorBufferID;
    GrGLuint fDepthStencilBufferID;
    const GrGLInterface* fGL;

    typedef SkRefCnt INHERITED;
};

/** Creates platform-dependent GL context object
 * Note: If Skia embedder needs a custom GL context that sets up the GL
 * interface, this function should be implemented by the embedder.
 * Otherwise, the default implementation for the platform should be compiled in
 * the library.
 */
SK_API SkGLContext* SkCreatePlatformGLContext();

/**
 * Helper macros for using the GL context through the GrGLInterface. Example:
 * SK_GL(glCtx, GenTextures(1, &texID));
 */
#define SK_GL(ctx, X) (ctx).gl()->fFunctions.f ## X;    \
                      SkASSERT(0 == (ctx).gl()->fFunctions.fGetError())
#define SK_GL_RET(ctx, RET, X) (RET) = (ctx).gl()->fFunctions.f ## X;    \
                  SkASSERT(0 == (ctx).gl()->fFunctions.fGetError())
#define SK_GL_NOERRCHECK(ctx, X) (ctx).gl()->fFunctions.f ## X
#define SK_GL_RET_NOERRCHECK(ctx, RET, X) (RET) = (ctx).gl()->fFunctions.f ## X

#endif
