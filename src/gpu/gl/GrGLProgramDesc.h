/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrGLProgramDesc_DEFINED
#define GrGLProgramDesc_DEFINED

#include "GrGLProcessor.h"
#include "GrDrawState.h"
#include "GrGpu.h"
#include "GrOptDrawState.h"

class GrGpuGL;

/** This class describes a program to generate. It also serves as a program cache key. Very little
    of this is GL-specific. The GL-specific parts could be factored out into a subclass. */
class GrGLProgramDesc {
public:
    GrGLProgramDesc() {}
    GrGLProgramDesc(const GrGLProgramDesc& desc) { *this = desc; }

    // Returns this as a uint32_t array to be used as a key in the program cache.
    const uint32_t* asKey() const {
        return reinterpret_cast<const uint32_t*>(fKey.begin());
    }

    // Gets the number of bytes in asKey(). It will be a 4-byte aligned value. When comparing two
    // keys the size of either key can be used with memcmp() since the lengths themselves begin the
    // keys and thus the memcmp will exit early if the keys are of different lengths.
    uint32_t keyLength() const { return *this->atOffset<uint32_t, kLengthOffset>(); }

    // Gets the a checksum of the key. Can be used as a hash value for a fast lookup in a cache.
    uint32_t getChecksum() const { return *this->atOffset<uint32_t, kChecksumOffset>(); }

    /**
     * Builds a program descriptor from a GrOptDrawState. Whether the primitive type is points, and
     * the caps of the GrGpuGL are also inputs. It also outputs the color and coverage stages
     * referenced by the generated descriptor. Coverage stages from the drawState may be treated as
     * color stages in the output.
     */
    static bool Build(const GrOptDrawState&,
                      GrGpu::DrawType,
                      GrBlendCoeff srcCoeff,
                      GrBlendCoeff dstCoeff,
                      GrGpuGL*,
                      const GrDeviceCoordTexture* dstCopy,
                      const GrGeometryStage** geometryProcessor,
                      SkTArray<const GrFragmentStage*, true>* colorStages,
                      SkTArray<const GrFragmentStage*, true>* coverageStages,
                      GrGLProgramDesc*);

    bool hasGeometryProcessor() const {
        return SkToBool(this->getHeader().fHasGeometryProcessor);
    }

    int numColorEffects() const {
        return this->getHeader().fColorEffectCnt;
    }

    int numCoverageEffects() const {
        return this->getHeader().fCoverageEffectCnt;
    }

    int numTotalEffects() const { return this->numColorEffects() + this->numCoverageEffects(); }

    GrGLProgramDesc& operator= (const GrGLProgramDesc& other);

    bool operator== (const GrGLProgramDesc& other) const {
        // The length is masked as a hint to the compiler that the address will be 4 byte aligned.
        return 0 == memcmp(this->asKey(), other.asKey(), this->keyLength() & ~0x3);
    }

    bool operator!= (const GrGLProgramDesc& other) const {
        return !(*this == other);
    }

    static bool Less(const GrGLProgramDesc& a, const GrGLProgramDesc& b) {
        return memcmp(a.asKey(), b.asKey(), a.keyLength() & ~0x3) < 0;
    }

private:
    // Specifies where the initial color comes from before the stages are applied.
    enum ColorInput {
        kAllOnes_ColorInput,
        kAttribute_ColorInput,
        kUniform_ColorInput,

        kColorInputCnt
    };

    struct KeyHeader {
        uint8_t                          fDstReadKey;   // set by GrGLShaderBuilder if there
                                                        // are effects that must read the dst.
                                                        // Otherwise, 0.
        uint8_t                          fFragPosKey;   // set by GrGLShaderBuilder if there are
                                                        // effects that read the fragment position.
                                                        // Otherwise, 0.

        SkBool8                     fUseFragShaderOnly;
        SkBool8                     fEmitsPointSize;

        ColorInput                       fColorInput : 8;
        ColorInput                       fCoverageInput : 8;

        GrOptDrawState::PrimaryOutputType    fPrimaryOutputType : 8;
        GrOptDrawState::SecondaryOutputType  fSecondaryOutputType : 8;

        int8_t                      fPositionAttributeIndex;
        int8_t                      fLocalCoordAttributeIndex;
        int8_t                      fColorAttributeIndex;
        int8_t                      fCoverageAttributeIndex;

        SkBool8                     fHasGeometryProcessor;
        int8_t                      fColorEffectCnt;
        int8_t                      fCoverageEffectCnt;
    };

    // The key, stored in fKey, is composed of five parts:
    // 1. uint32_t for total key length.
    // 2. uint32_t for a checksum.
    // 3. Header struct defined above.
    // 4. An array of offsets to effect keys and their sizes (see 5). uint16_t for each
    //    offset and size.
    // 5. per-effect keys. Each effect's key is a variable length array of uint32_t.
    enum {
        // Part 1.
        kLengthOffset = 0,
        // Part 2.
        kChecksumOffset = kLengthOffset + sizeof(uint32_t),
        // Part 3.
        kHeaderOffset = kChecksumOffset + sizeof(uint32_t),
        kHeaderSize = SkAlign4(sizeof(KeyHeader)),
        // Part 4.
        // This is the offset in the overall key to the array of per-effect offset,length pairs.
        kEffectKeyOffsetsAndLengthOffset = kHeaderOffset + kHeaderSize,
    };

    template<typename T, size_t OFFSET> T* atOffset() {
        return reinterpret_cast<T*>(reinterpret_cast<intptr_t>(fKey.begin()) + OFFSET);
    }

    template<typename T, size_t OFFSET> const T* atOffset() const {
        return reinterpret_cast<const T*>(reinterpret_cast<intptr_t>(fKey.begin()) + OFFSET);
    }

    KeyHeader* header() { return this->atOffset<KeyHeader, kHeaderOffset>(); }

    // a helper class to handle getting an individual processor's key
    template <class ProcessorKeyBuilder>
    static bool BuildStagedProcessorKey(const typename ProcessorKeyBuilder::StagedProcessor& stage,
                                        const GrGLCaps& caps,
                                        bool requiresLocalCoordAttrib,
                                        GrGLProgramDesc* desc,
                                        int* offsetAndSizeIndex);
    void finalize();

    const KeyHeader& getHeader() const { return *this->atOffset<KeyHeader, kHeaderOffset>(); }

    /** Used to provide effects' keys to their emitCode() function. */
    class EffectKeyProvider {
    public:
        enum EffectType {
            kGeometryProcessor_EffectType,
            kColor_EffectType,
            kCoverage_EffectType,
        };

        EffectKeyProvider(const GrGLProgramDesc* desc, EffectType type) : fDesc(desc) {
            switch (type) {
                case kGeometryProcessor_EffectType:
                    // there can be only one
                    fBaseIndex = 0;
                    break;
                case kColor_EffectType:
                    fBaseIndex = desc->hasGeometryProcessor() ? 1 : 0;
                    break;
                case kCoverage_EffectType:
                    fBaseIndex = desc->numColorEffects() + (desc->hasGeometryProcessor() ? 1 : 0);
                    break;
            }
        }

        GrProcessorKey get(int index) const {
            const uint16_t* offsetsAndLengths = reinterpret_cast<const uint16_t*>(
                fDesc->fKey.begin() + kEffectKeyOffsetsAndLengthOffset);
            // We store two uint16_ts per effect, one for the offset to the effect's key and one for
            // its length. Here we just need the offset.
            uint16_t offset = offsetsAndLengths[2 * (fBaseIndex + index) + 0];
            uint16_t length = offsetsAndLengths[2 * (fBaseIndex + index) + 1];
            // Currently effects must add to the key in units of uint32_t.
            SkASSERT(0 == (length % sizeof(uint32_t)));
            return GrProcessorKey(reinterpret_cast<const uint32_t*>(fDesc->fKey.begin() + offset),
                               length / sizeof(uint32_t));
        }
    private:
        const GrGLProgramDesc*  fDesc;
        int                     fBaseIndex;
    };

    enum {
        kMaxPreallocEffects = 8,
        kIntsPerEffect      = 4,    // This is an overestimate of the average effect key size.
        kPreAllocSize = kEffectKeyOffsetsAndLengthOffset +
                        kMaxPreallocEffects * sizeof(uint32_t) * kIntsPerEffect,
    };

    SkSTArray<kPreAllocSize, uint8_t, true> fKey;

    // GrGLProgram and GrGLShaderBuilder read the private fields to generate code. TODO: Split out
    // part of GrGLShaderBuilder that is used by effects so that this header doesn't need to be
    // visible to GrGLProcessors. Then make public accessors as necessary and remove friends.
    friend class GrGLProgram;
    friend class GrGLProgramBuilder;
    friend class GrGLLegacyNvprProgramBuilder;
    friend class GrGLVertexBuilder;
    friend class GrGLFragmentShaderBuilder;
    friend class GrGLGeometryBuilder;
};

#endif
