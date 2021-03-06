/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrGLProgramBuilder.h"
#include "gl/GrGLGeometryProcessor.h"
#include "gl/GrGLProgram.h"
#include "gl/GrGLSLPrettyPrint.h"
#include "gl/GrGLUniformHandle.h"
#include "../GrGpuGL.h"
#include "GrCoordTransform.h"
#include "GrGLLegacyNvprProgramBuilder.h"
#include "GrGLNvprProgramBuilder.h"
#include "GrGLProgramBuilder.h"
#include "GrTexture.h"
#include "SkRTConf.h"
#include "SkTraceEvent.h"

#define GL_CALL(X) GR_GL_CALL(this->gpu()->glInterface(), X)
#define GL_CALL_RET(R, X) GR_GL_CALL_RET(this->gpu()->glInterface(), R, X)

// ES2 FS only guarantees mediump and lowp support
static const GrGLShaderVar::Precision kDefaultFragmentPrecision = GrGLShaderVar::kMedium_Precision;

//////////////////////////////////////////////////////////////////////////////

const int GrGLProgramBuilder::kVarsPerBlock = 8;

GrGLProgram* GrGLProgramBuilder::CreateProgram(const GrOptDrawState& optState,
                                               const GrGLProgramDesc& desc,
                                               GrGpu::DrawType drawType,
                                               const GrGeometryStage* geometryProcessor,
                                               const GrFragmentStage* colorStages[],
                                               const GrFragmentStage* coverageStages[],
                                               GrGpuGL* gpu) {
    // create a builder.  This will be handed off to effects so they can use it to add
    // uniforms, varyings, textures, etc
    SkAutoTDelete<GrGLProgramBuilder> builder(CreateProgramBuilder(desc,
                                                                   optState,
                                                                   drawType,
                                                                   SkToBool(geometryProcessor),
                                                                   gpu));

    GrGLProgramBuilder* pb = builder.get();
    const GrGLProgramDesc::KeyHeader& header = pb->header();

    // emit code to read the dst copy texture, if necessary
    if (GrGLFragmentShaderBuilder::kNoDstRead_DstReadKey != header.fDstReadKey
            && !gpu->glCaps().fbFetchSupport()) {
        pb->fFS.emitCodeToReadDstTexture();
    }

    // get the initial color and coverage to feed into the first effect in each effect chain
    GrGLSLExpr4 inputColor, inputCoverage;
    pb->setupUniformColorAndCoverageIfNeeded(&inputColor,  &inputCoverage);

    // if we have a vertex shader(we don't only if we are using NVPR or NVPR ES), then we may have
    // to setup a few more things like builtin vertex attributes
    bool hasVertexShader = !header.fUseFragShaderOnly;
    if (hasVertexShader) {
        pb->fVS.setupLocalCoords();
        pb->fVS.transformGLToSkiaCoords();
        if (header.fEmitsPointSize) {
            pb->fVS.codeAppend("gl_PointSize = 1.0;");
        }
        if (GrGLProgramDesc::kAttribute_ColorInput == header.fColorInput) {
            pb->fVS.setupBuiltinVertexAttribute("Color", &inputColor);
        }
        if (GrGLProgramDesc::kAttribute_ColorInput == header.fCoverageInput) {
            pb->fVS.setupBuiltinVertexAttribute("Coverage", &inputCoverage);
        }
    }

    pb->createAndEmitProcessors(geometryProcessor, colorStages, coverageStages, &inputColor,
                                &inputCoverage);

    if (hasVertexShader) {
        pb->fVS.transformSkiaToGLCoords();
    }

    // write the secondary color output if necessary
    if (GrOptDrawState::kNone_SecondaryOutputType != header.fSecondaryOutputType) {
        pb->fFS.enableSecondaryOutput(inputColor, inputCoverage);
    }

    pb->fFS.combineColorAndCoverage(inputColor, inputCoverage);

    return pb->finalize();
}

GrGLProgramBuilder*
GrGLProgramBuilder::CreateProgramBuilder(const GrGLProgramDesc& desc,
                                         const GrOptDrawState& optState,
                                         GrGpu::DrawType drawType,
                                         bool hasGeometryProcessor,
                                         GrGpuGL* gpu) {
    if (desc.getHeader().fUseFragShaderOnly) {
        SkASSERT(gpu->glCaps().pathRenderingSupport());
        SkASSERT(gpu->glPathRendering()->texturingMode() ==
                 GrGLPathRendering::FixedFunction_TexturingMode);
        SkASSERT(!hasGeometryProcessor);
        return SkNEW_ARGS(GrGLLegacyNvprProgramBuilder, (gpu, optState, desc));
    } else if (GrGpu::IsPathRenderingDrawType(drawType)) {
        SkASSERT(gpu->glCaps().pathRenderingSupport());
        SkASSERT(gpu->glPathRendering()->texturingMode() ==
                 GrGLPathRendering::SeparableShaders_TexturingMode);
        SkASSERT(!hasGeometryProcessor);
        return SkNEW_ARGS(GrGLNvprProgramBuilder, (gpu, optState, desc));
    } else {
        return SkNEW_ARGS(GrGLProgramBuilder, (gpu, optState, desc));
    }
}

/////////////////////////////////////////////////////////////////////////////

GrGLProgramBuilder::GrGLProgramBuilder(GrGpuGL* gpu, const GrOptDrawState& optState,
                                       const GrGLProgramDesc& desc)
    : fVS(this)
    , fGS(this)
    , fFS(this, desc)
    , fOutOfStage(true)
    , fStageIndex(-1)
    , fOptState(optState)
    , fDesc(desc)
    , fGpu(gpu)
    , fUniforms(kVarsPerBlock) {
}

void GrGLProgramBuilder::addVarying(GrSLType type,
                                    const char* name,
                                    const char** vsOutName,
                                    const char** fsInName,
                                    GrGLShaderVar::Precision fsPrecision) {
    SkString* fsInputName = fVS.addVarying(type, name, vsOutName);
    fFS.addVarying(type, fsInputName->c_str(), fsInName, fsPrecision);
}

void GrGLProgramBuilder::nameVariable(SkString* out, char prefix, const char* name) {
    if ('\0' == prefix) {
        *out = name;
    } else {
        out->printf("%c%s", prefix, name);
    }
    if (!fOutOfStage) {
        if (out->endsWith('_')) {
            // Names containing "__" are reserved.
            out->append("x");
        }
        out->appendf("_Stage%d", fStageIndex);
    }
}

GrGLProgramDataManager::UniformHandle GrGLProgramBuilder::addUniformArray(uint32_t visibility,
                                                                          GrSLType type,
                                                                          const char* name,
                                                                          int count,
                                                                          const char** outName) {
    SkASSERT(name && strlen(name));
    SkDEBUGCODE(static const uint32_t kVisibilityMask = kVertex_Visibility | kFragment_Visibility);
    SkASSERT(0 == (~kVisibilityMask & visibility));
    SkASSERT(0 != visibility);

    UniformInfo& uni = fUniforms.push_back();
    uni.fVariable.setType(type);
    uni.fVariable.setTypeModifier(GrGLShaderVar::kUniform_TypeModifier);
    this->nameVariable(uni.fVariable.accessName(), 'u', name);
    uni.fVariable.setArrayCount(count);
    uni.fVisibility = visibility;

    // If it is visible in both the VS and FS, the precision must match.
    // We declare a default FS precision, but not a default VS. So set the var
    // to use the default FS precision.
    if ((kVertex_Visibility | kFragment_Visibility) == visibility) {
        // the fragment and vertex precisions must match
        uni.fVariable.setPrecision(kDefaultFragmentPrecision);
    }

    if (outName) {
        *outName = uni.fVariable.c_str();
    }
    return GrGLProgramDataManager::UniformHandle::CreateFromUniformIndex(fUniforms.count() - 1);
}

void GrGLProgramBuilder::appendUniformDecls(ShaderVisibility visibility,
                                            SkString* out) const {
    for (int i = 0; i < fUniforms.count(); ++i) {
        if (fUniforms[i].fVisibility & visibility) {
            fUniforms[i].fVariable.appendDecl(this->ctxInfo(), out);
            out->append(";\n");
        }
    }
}

const GrGLContextInfo& GrGLProgramBuilder::ctxInfo() const {
    return fGpu->ctxInfo();
}

void GrGLProgramBuilder::setupUniformColorAndCoverageIfNeeded(GrGLSLExpr4* inputColor,
                                                              GrGLSLExpr4* inputCoverage) {
    const GrGLProgramDesc::KeyHeader& header = this->header();
    if (GrGLProgramDesc::kUniform_ColorInput == header.fColorInput) {
        const char* name;
        fUniformHandles.fColorUni =
            this->addUniform(GrGLProgramBuilder::kFragment_Visibility,
                             kVec4f_GrSLType,
                             "Color",
                             &name);
        *inputColor = GrGLSLExpr4(name);
    } else if (GrGLProgramDesc::kAllOnes_ColorInput == header.fColorInput) {
        *inputColor = GrGLSLExpr4(1);
    }
    if (GrGLProgramDesc::kUniform_ColorInput == header.fCoverageInput) {
        const char* name;
        fUniformHandles.fCoverageUni =
            this->addUniform(GrGLProgramBuilder::kFragment_Visibility,
                             kVec4f_GrSLType,
                             "Coverage",
                             &name);
        *inputCoverage = GrGLSLExpr4(name);
    } else if (GrGLProgramDesc::kAllOnes_ColorInput == header.fCoverageInput) {
        *inputCoverage = GrGLSLExpr4(1);
    }
}

void GrGLProgramBuilder::createAndEmitProcessors(const GrGeometryStage* geometryProcessor,
                                                 const GrFragmentStage* colorStages[],
                                                 const GrFragmentStage* coverageStages[],
                                                 GrGLSLExpr4* inputColor,
                                                 GrGLSLExpr4* inputCoverage) {
    bool useLocalCoords = fVS.hasExplicitLocalCoords();

    EffectKeyProvider colorKeyProvider(&fDesc, EffectKeyProvider::kColor_EffectType);
    int numColorEffects = fDesc.numColorEffects();
    GrGLInstalledProcessors* ip = SkNEW_ARGS(GrGLInstalledProcessors, (numColorEffects,
                                                                       useLocalCoords));
    this->createAndEmitProcessors<GrFragmentStage>(colorStages, numColorEffects, colorKeyProvider,
                                                   inputColor, ip);
    fColorEffects.reset(ip);

    if (geometryProcessor) {
        fVS.emitAttributes(*geometryProcessor->getProcessor());
        EffectKeyProvider gpKeyProvider(&fDesc, EffectKeyProvider::kGeometryProcessor_EffectType);
        ip = SkNEW_ARGS(GrGLInstalledProcessors, (1, useLocalCoords));
        this->createAndEmitProcessors<GrGeometryStage>(&geometryProcessor, 1, gpKeyProvider,
                                                       inputCoverage, ip);
        fGeometryProcessor.reset(ip);
    }

    EffectKeyProvider coverageKeyProvider(&fDesc, EffectKeyProvider::kCoverage_EffectType);
    int numCoverageEffects = fDesc.numCoverageEffects();
    ip = SkNEW_ARGS(GrGLInstalledProcessors, (numCoverageEffects, useLocalCoords));
    this->createAndEmitProcessors<GrFragmentStage>(coverageStages, numCoverageEffects,
                                                   coverageKeyProvider, inputCoverage, ip);
    fCoverageEffects.reset(ip);
}

template <class ProcessorStage>
void GrGLProgramBuilder::createAndEmitProcessors(const ProcessorStage* processStages[],
                                                 int effectCnt,
                                                 const EffectKeyProvider& keyProvider,
                                                 GrGLSLExpr4* fsInOutColor,
                                                 GrGLInstalledProcessors* installedProcessors) {
    bool effectEmitted = false;

    GrGLSLExpr4 inColor = *fsInOutColor;
    GrGLSLExpr4 outColor;

    for (int e = 0; e < effectCnt; ++e) {
        // Program builders have a bit of state we need to clear with each effect
        AutoStageAdvance adv(this);
        const ProcessorStage& stage = *processStages[e];
        SkASSERT(stage.getProcessor());

        if (inColor.isZeros()) {
            SkString inColorName;

            // Effects have no way to communicate zeros, they treat an empty string as ones.
            this->nameVariable(&inColorName, '\0', "input");
            fFS.codeAppendf("vec4 %s = %s;", inColorName.c_str(), inColor.c_str());
            inColor = inColorName;
        }

        // create var to hold stage result
        SkString outColorName;
        this->nameVariable(&outColorName, '\0', "output");
        fFS.codeAppendf("vec4 %s;", outColorName.c_str());
        outColor = outColorName;

        SkASSERT(installedProcessors);
        const typename ProcessorStage::Processor& processor = *stage.getProcessor();
        SkSTArray<2, GrGLProcessor::TransformedCoords> coords(processor.numTransforms());
        SkSTArray<4, GrGLProcessor::TextureSampler> samplers(processor.numTextures());

        this->emitTransforms(stage, &coords, installedProcessors);
        this->emitSamplers(processor, &samplers, installedProcessors);

        typename ProcessorStage::GLProcessor* glEffect =
                processor.getFactory().createGLInstance(processor);
        installedProcessors->addEffect(glEffect);

        // Enclose custom code in a block to avoid namespace conflicts
        SkString openBrace;
        openBrace.printf("{ // Stage %d: %s\n", fStageIndex, glEffect->name());
        fFS.codeAppend(openBrace.c_str());
        fVS.codeAppend(openBrace.c_str());

        glEffect->emitCode(this, processor, keyProvider.get(e), outColor.c_str(),
                           inColor.isOnes() ? NULL : inColor.c_str(), coords, samplers);

        // We have to check that effects and the code they emit are consistent, ie if an effect
        // asks for dst color, then the emit code needs to follow suit
        verify(processor);
        fFS.codeAppend("}");
        fVS.codeAppend("}");

        inColor = outColor;
        effectEmitted = true;
    }

    if (effectEmitted) {
        *fsInOutColor = outColor;
    }
}

void GrGLProgramBuilder::verify(const GrGeometryProcessor& gp) {
    SkASSERT(fFS.hasReadFragmentPosition() == gp.willReadFragmentPosition());
}

void GrGLProgramBuilder::verify(const GrFragmentProcessor& fp) {
    SkASSERT(fFS.hasReadFragmentPosition() == fp.willReadFragmentPosition());
    SkASSERT(fFS.hasReadDstColor() == fp.willReadDstColor());
}

void GrGLProgramBuilder::emitTransforms(const GrProcessorStage& effectStage,
                                        GrGLProcessor::TransformedCoordsArray* outCoords,
                                        GrGLInstalledProcessors* installedProcessors) {
    SkTArray<GrGLInstalledProcessors::Transform, true>& transforms =
            installedProcessors->addTransforms();
    const GrProcessor* effect = effectStage.getProcessor();
    int numTransforms = effect->numTransforms();
    transforms.push_back_n(numTransforms);

    for (int t = 0; t < numTransforms; t++) {
        const char* uniName = "StageMatrix";
        GrSLType varyingType =
                effectStage.isPerspectiveCoordTransform(t, fVS.hasExplicitLocalCoords()) ?
                        kVec3f_GrSLType :
                        kVec2f_GrSLType;

        SkString suffixedUniName;
        if (0 != t) {
            suffixedUniName.append(uniName);
            suffixedUniName.appendf("_%i", t);
            uniName = suffixedUniName.c_str();
        }
        transforms[t].fHandle = this->addUniform(GrGLProgramBuilder::kVertex_Visibility,
                                                 kMat33f_GrSLType,
                                                 uniName,
                                                 &uniName).toShaderBuilderIndex();

        const char* varyingName = "MatrixCoord";
        SkString suffixedVaryingName;
        if (0 != t) {
            suffixedVaryingName.append(varyingName);
            suffixedVaryingName.appendf("_%i", t);
            varyingName = suffixedVaryingName.c_str();
        }
        const char* vsVaryingName;
        const char* fsVaryingName;
        this->addVarying(varyingType, varyingName, &vsVaryingName, &fsVaryingName);

        const GrGLShaderVar& coords =
                kPosition_GrCoordSet == effect->coordTransform(t).sourceCoords() ?
                                          fVS.positionAttribute() :
                                          fVS.localCoordsAttribute();

        // varying = matrix * coords (logically)
        SkASSERT(kVec2f_GrSLType == varyingType || kVec3f_GrSLType == varyingType);
        if (kVec2f_GrSLType == varyingType) {
            fVS.codeAppendf("%s = (%s * vec3(%s, 1)).xy;",
                            vsVaryingName, uniName, coords.c_str());
        } else {
            fVS.codeAppendf("%s = %s * vec3(%s, 1);",
                            vsVaryingName, uniName, coords.c_str());
        }
        SkNEW_APPEND_TO_TARRAY(outCoords, GrGLProcessor::TransformedCoords,
                               (SkString(fsVaryingName), varyingType));
    }
}

void GrGLProgramBuilder::emitSamplers(const GrProcessor& processor,
                                      GrGLProcessor::TextureSamplerArray* outSamplers,
                                      GrGLInstalledProcessors* installedProcessors) {
    SkTArray<GrGLInstalledProcessors::Sampler, true>& samplers = installedProcessors->addSamplers();
    int numTextures = processor.numTextures();
    samplers.push_back_n(numTextures);
    SkString name;
    for (int t = 0; t < numTextures; ++t) {
        name.printf("Sampler%d", t);
        samplers[t].fUniform = this->addUniform(GrGLProgramBuilder::kFragment_Visibility,
                                                kSampler2D_GrSLType,
                                                name.c_str());
        SkNEW_APPEND_TO_TARRAY(outSamplers, GrGLProcessor::TextureSampler,
                               (samplers[t].fUniform, processor.textureAccess(t)));
    }
}

GrGLProgram* GrGLProgramBuilder::finalize() {
    // verify we can get a program id
    GrGLuint programID;
    GL_CALL_RET(programID, CreateProgram());
    if (0 == programID) {
        return NULL;
    }

    // compile shaders and bind attributes / uniforms
    SkTDArray<GrGLuint> shadersToDelete;
    if (!fFS.compileAndAttachShaders(programID, &shadersToDelete)) {
        this->cleanupProgram(programID, shadersToDelete);
        return NULL;
    }
    if (!this->header().fUseFragShaderOnly) {
        if (!fVS.compileAndAttachShaders(programID, &shadersToDelete)) {
            this->cleanupProgram(programID, shadersToDelete);
            return NULL;
        }
        fVS.bindVertexAttributes(programID);
    }
    bool usingBindUniform = fGpu->glInterface()->fFunctions.fBindUniformLocation != NULL;
    if (usingBindUniform) {
        this->bindUniformLocations(programID);
    }
    fFS.bindFragmentShaderLocations(programID);
    GL_CALL(LinkProgram(programID));

    // Calling GetProgramiv is expensive in Chromium. Assume success in release builds.
    bool checkLinked = !fGpu->ctxInfo().isChromium();
#ifdef SK_DEBUG
    checkLinked = true;
#endif
    if (checkLinked) {
        checkLinkStatus(programID);
    }
    if (!usingBindUniform) {
        this->resolveUniformLocations(programID);
    }

    this->cleanupShaders(shadersToDelete);

    return this->createProgram(programID);
}

void GrGLProgramBuilder::bindUniformLocations(GrGLuint programID) {
    int count = fUniforms.count();
    for (int i = 0; i < count; ++i) {
        GL_CALL(BindUniformLocation(programID, i, fUniforms[i].fVariable.c_str()));
        fUniforms[i].fLocation = i;
    }
}

bool GrGLProgramBuilder::checkLinkStatus(GrGLuint programID) {
    GrGLint linked = GR_GL_INIT_ZERO;
    GL_CALL(GetProgramiv(programID, GR_GL_LINK_STATUS, &linked));
    if (!linked) {
        GrGLint infoLen = GR_GL_INIT_ZERO;
        GL_CALL(GetProgramiv(programID, GR_GL_INFO_LOG_LENGTH, &infoLen));
        SkAutoMalloc log(sizeof(char)*(infoLen+1));  // outside if for debugger
        if (infoLen > 0) {
            // retrieve length even though we don't need it to workaround
            // bug in chrome cmd buffer param validation.
            GrGLsizei length = GR_GL_INIT_ZERO;
            GL_CALL(GetProgramInfoLog(programID,
                                      infoLen+1,
                                      &length,
                                      (char*)log.get()));
            GrPrintf((char*)log.get());
        }
        SkDEBUGFAIL("Error linking program");
        GL_CALL(DeleteProgram(programID));
        programID = 0;
    }
    return SkToBool(linked);
}

void GrGLProgramBuilder::resolveUniformLocations(GrGLuint programID) {
    int count = fUniforms.count();
    for (int i = 0; i < count; ++i) {
        GrGLint location;
        GL_CALL_RET(location, GetUniformLocation(programID, fUniforms[i].fVariable.c_str()));
        fUniforms[i].fLocation = location;
    }
}

void GrGLProgramBuilder::cleanupProgram(GrGLuint programID, const SkTDArray<GrGLuint>& shaderIDs) {
    GL_CALL(DeleteProgram(programID));
    cleanupShaders(shaderIDs);
}
void GrGLProgramBuilder::cleanupShaders(const SkTDArray<GrGLuint>& shaderIDs) {
    for (int i = 0; i < shaderIDs.count(); ++i) {
      GL_CALL(DeleteShader(shaderIDs[i]));
    }
}

GrGLProgram* GrGLProgramBuilder::createProgram(GrGLuint programID) {
    return SkNEW_ARGS(GrGLProgram, (fGpu, fDesc, fUniformHandles, programID, fUniforms,
                                    fGeometryProcessor, fColorEffects, fCoverageEffects));
}

////////////////////////////////////////////////////////////////////////////////

GrGLInstalledProcessors::~GrGLInstalledProcessors() {
    int numEffects = fGLProcessors.count();
    for (int e = 0; e < numEffects; ++e) {
        SkDELETE(fGLProcessors[e]);
    }
}
