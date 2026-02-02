/*
SoLoud audio engine - LUFS calculation (internal types)
Copyright (c) 2013-2020 Jari Komppa
Copyright (c) 2025-2026 William Horvath

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#ifndef SOLOUD_LOUDNESS_INTERNAL_H
#define SOLOUD_LOUDNESS_INTERNAL_H

#include "soloud_cpu.h"

namespace SoLoud::Loudness
{
struct BiquadCoeffs
{
	float b0, b1, b2, a1, a2; // a0 normalized to 1.0
};

struct BiquadState
{
	float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

// K-weight a chunk of audio: apply pre-filter then RLB biquad to all channels.
// Input layout:  aBuffer[ch * aBufStride + sampleIndex]
// Output layout: aOut[ch * aBufStride + sampleIndex]
using KWeightChunkFn = void (*)(const BiquadCoeffs &aPreCoeffs, const BiquadCoeffs &aRlbCoeffs, BiquadState *aPreState, BiquadState *aRlbState, const float *aBuffer,
                                unsigned int aSamplesRead, unsigned int aBufStride, unsigned int aChannels, float *aOut);

void kWeightChunkScalar(const BiquadCoeffs &aPreCoeffs, const BiquadCoeffs &aRlbCoeffs, BiquadState *aPreState, BiquadState *aRlbState, const float *aBuffer,
                        unsigned int aSamplesRead, unsigned int aBufStride, unsigned int aChannels, float *aOut);

#if defined(SOLOUD_SUPPORT_SSE2)
void kWeightChunkSSE(const BiquadCoeffs &aPreCoeffs, const BiquadCoeffs &aRlbCoeffs, BiquadState *aPreState, BiquadState *aRlbState, const float *aBuffer,
                     unsigned int aSamplesRead, unsigned int aBufStride, unsigned int aChannels, float *aOut);
#endif

} // namespace SoLoud::Loudness

#endif
