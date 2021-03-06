/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrGLVertexShaderBuilder.h"
#include "GrGLProgramBuilder.h"
#include "GrGLShaderStringBuilder.h"
#include "../GrGpuGL.h"

#define GL_CALL(X) GR_GL_CALL(fProgramBuilder->gpu()->glInterface(), X)
#define GL_CALL_RET(R, X) GR_GL_CALL_RET(fProgramBuilder->gpu()->glInterface(), R, X)

static const char* color_attribute_name() { return "inColor"; }
static const char* coverage_attribute_name() { return "inCoverage"; }

GrGLVertexBuilder::GrGLVertexBuilder(GrGLProgramBuilder* program)
    : INHERITED(program)
    , fPositionVar(NULL)
    , fLocalCoordsVar(NULL)
    , fEffectAttribOffset(0) {
}

SkString* GrGLVertexBuilder::addVarying(GrSLType type, const char* name,
                                              const char** vsOutName) {
    fOutputs.push_back();
    fOutputs.back().setType(type);
    fOutputs.back().setTypeModifier(GrGLShaderVar::kVaryingOut_TypeModifier);
    fProgramBuilder->nameVariable(fOutputs.back().accessName(), 'v', name);

    if (vsOutName) {
        *vsOutName = fOutputs.back().getName().c_str();
    }
    return fOutputs.back().accessName();
}

void GrGLVertexBuilder::setupLocalCoords() {
    fPositionVar = &fInputs.push_back();
    fPositionVar->set(kVec2f_GrSLType, GrGLShaderVar::kAttribute_TypeModifier, "inPosition");
    if (-1 != fProgramBuilder->header().fLocalCoordAttributeIndex) {
        fLocalCoordsVar = &fInputs.push_back();
        fLocalCoordsVar->set(kVec2f_GrSLType,
                             GrGLShaderVar::kAttribute_TypeModifier,
                             "inLocalCoords");
    } else {
        fLocalCoordsVar = fPositionVar;
    }
    fEffectAttribOffset = fInputs.count();
}

void GrGLVertexBuilder::transformGLToSkiaCoords() {
    const char* viewMName;
    fProgramBuilder->fUniformHandles.fViewMatrixUni =
            fProgramBuilder->addUniform(GrGLProgramBuilder::kVertex_Visibility,
                                        kMat33f_GrSLType,
                                        "ViewM",
                                        &viewMName);

    // Transform the position into Skia's device coords.
    this->codeAppendf("vec3 pos3 = %s * vec3(%s, 1);", viewMName, fPositionVar->c_str());
}

void GrGLVertexBuilder::setupBuiltinVertexAttribute(const char* inName, GrGLSLExpr4* out) {
    SkString name(inName);
    const char *vsName, *fsName;
    fProgramBuilder->addVarying(kVec4f_GrSLType, name.c_str(), &vsName, &fsName);
    name.prepend("in");
    this->addAttribute(GrShaderVar(name.c_str(),
                                   kVec4f_GrSLType,
                                   GrShaderVar::kAttribute_TypeModifier));
    this->codeAppendf("%s = %s;", vsName, name.c_str());
    *out = fsName;
    fEffectAttribOffset++;
}

void GrGLVertexBuilder::emitAttributes(const GrGeometryProcessor& gp) {
    const GrGeometryProcessor::VertexAttribArray& vars = gp.getVertexAttribs();
    int numAttributes = vars.count();
    for (int a = 0; a < numAttributes; ++a) {
        this->addAttribute(vars[a]);
    }
}

void GrGLVertexBuilder::transformSkiaToGLCoords() {
    const char* rtAdjustName;
    fProgramBuilder->fUniformHandles.fRTAdjustmentUni =
            fProgramBuilder->addUniform(GrGLProgramBuilder::kVertex_Visibility,
                                        kVec4f_GrSLType,
                                        "rtAdjustment",
                                        &rtAdjustName);

    // Transform from Skia's device coords to GL's normalized device coords.
    this->codeAppendf("gl_Position = vec4(dot(pos3.xz, %s.xy), dot(pos3.yz, %s.zw), 0, pos3.z);",
                    rtAdjustName, rtAdjustName);
}

void GrGLVertexBuilder::bindVertexAttributes(GrGLuint programID) {
    // Bind the attrib locations to same values for all shaders
    const GrGLProgramDesc::KeyHeader& header = fProgramBuilder->header();
    SkASSERT(-1 != header.fPositionAttributeIndex);
    GL_CALL(BindAttribLocation(programID,
                               header.fPositionAttributeIndex,
                               fPositionVar->c_str()));
    if (-1 != header.fLocalCoordAttributeIndex) {
        GL_CALL(BindAttribLocation(programID,
                                   header.fLocalCoordAttributeIndex,
                                   fLocalCoordsVar->c_str()));
    }
    if (-1 != header.fColorAttributeIndex) {
        GL_CALL(BindAttribLocation(programID,
                                   header.fColorAttributeIndex,
                                   color_attribute_name()));
    }
    if (-1 != header.fCoverageAttributeIndex) {
        GL_CALL(BindAttribLocation(programID,
                                   header.fCoverageAttributeIndex,
                                   coverage_attribute_name()));
    }

    const GrOptDrawState& optState = fProgramBuilder->optState();
    const GrVertexAttrib* vaPtr = optState.getVertexAttribs();
    const int vaCount = optState.getVertexAttribCount();

    // We start binding attributes after builtins
    int i = fEffectAttribOffset;
    for (int index = 0; index < vaCount; index++) {
        if (kGeometryProcessor_GrVertexAttribBinding != vaPtr[index].fBinding) {
            continue;
        }
        SkASSERT(index != header.fPositionAttributeIndex &&
                 index != header.fLocalCoordAttributeIndex &&
                 index != header.fColorAttributeIndex &&
                 index != header.fCoverageAttributeIndex);
        // We should never find another effect attribute if we have bound everything
        SkASSERT(i < fInputs.count());
        GL_CALL(BindAttribLocation(programID, index, fInputs[i].c_str()));
        i++;
    }
    // Make sure we bound everything
    SkASSERT(fInputs.count() == i);
}

bool GrGLVertexBuilder::compileAndAttachShaders(GrGLuint programId,
        SkTDArray<GrGLuint>* shaderIds) const {
    GrGpuGL* gpu = fProgramBuilder->gpu();
    const GrGLContext& glCtx = gpu->glContext();
    const GrGLContextInfo& ctxInfo = gpu->ctxInfo();
    SkString vertShaderSrc(GrGetGLSLVersionDecl(ctxInfo));
    fProgramBuilder->appendUniformDecls(GrGLProgramBuilder::kVertex_Visibility, &vertShaderSrc);
    this->appendDecls(fInputs, &vertShaderSrc);
    this->appendDecls(fOutputs, &vertShaderSrc);
    vertShaderSrc.append("void main() {");
    vertShaderSrc.append(fCode);
    vertShaderSrc.append("}\n");
    GrGLuint vertShaderId = GrGLCompileAndAttachShader(glCtx, programId,
                                                       GR_GL_VERTEX_SHADER, vertShaderSrc,
                                                       gpu->gpuStats());
    if (!vertShaderId) {
        return false;
    }
    *shaderIds->append() = vertShaderId;
    return true;
}

bool GrGLVertexBuilder::addAttribute(const GrShaderVar& var) {
    SkASSERT(GrShaderVar::kAttribute_TypeModifier == var.getTypeModifier());
    for (int i = 0; i < fInputs.count(); ++i) {
        const GrGLShaderVar& attr = fInputs[i];
        // if attribute already added, don't add it again
        if (attr.getName().equals(var.getName())) {
            return false;
        }
    }
    fInputs.push_back(var);
    return true;
}
