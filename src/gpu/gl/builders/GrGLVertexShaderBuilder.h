/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrGLVertexShader_DEFINED
#define GrGLVertexShader_DEFINED

#include "GrGLShaderBuilder.h"

class GrGLProgramBuilder;

// TODO we only actually ever need to return a GrGLShaderBuilder for this guy, none of the below
// functions need to be part of VertexShaderBuilder's public interface
class GrGLVertexBuilder : public GrGLShaderBuilder {
public:
    GrGLVertexBuilder(GrGLProgramBuilder* program);

    /**
     * Are explicit local coordinates provided as input to the vertex shader.
     */
    bool hasExplicitLocalCoords() const { return (fLocalCoordsVar != fPositionVar); }

    /** Returns a vertex attribute that represents the local coords in the VS. This may be the same
        as positionAttribute() or it may not be. It depends upon whether the rendering code
        specified explicit local coords or not in the GrDrawState. */
    const GrGLShaderVar& localCoordsAttribute() const { return *fLocalCoordsVar; }

    /** Returns a vertex attribute that represents the vertex position in the VS. This is the
        pre-matrix position and is commonly used by effects to compute texture coords via a matrix.
      */
    const GrGLShaderVar& positionAttribute() const { return *fPositionVar; }

    /*
     * Internal call for GrGLProgramBuilder.addVarying
     */
    SkString* addVarying(GrSLType type, const char* name, const char** vsOutName);

    /*
     * private helpers for compilation by GrGLProgramBuilder
     */
    void setupLocalCoords();
    void transformGLToSkiaCoords();
    void setupBuiltinVertexAttribute(const char* inName, GrGLSLExpr4* out);
    void emitAttributes(const GrGeometryProcessor& gp);
    void transformSkiaToGLCoords();
    void bindVertexAttributes(GrGLuint programID);
    bool compileAndAttachShaders(GrGLuint programId, SkTDArray<GrGLuint>* shaderIds) const;

private:
    // an internal call which checks for uniquness of a var before adding it to the list of inputs
    bool addAttribute(const GrShaderVar& var);
    struct AttributePair {
        void set(int index, const SkString& name) {
            fIndex = index; fName = name;
        }
        int      fIndex;
        SkString fName;
    };

    GrGLShaderVar*                      fPositionVar;
    GrGLShaderVar*                      fLocalCoordsVar;
    int                                 fEffectAttribOffset;

    typedef GrGLShaderBuilder INHERITED;
};

#endif
