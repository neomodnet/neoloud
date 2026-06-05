/*
SoLoud audio engine - LUFS calculation utility
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

#include "soloud_audiosource.h"
#include "soloud_error.h"
#include "soloud_loudness.h"
#include "soloud_loudness_internal.h"

#include <cmath>
#include <memory>
#include <vector>

namespace SoLoud::Loudness
{
namespace
{
// ITU-R BS.1770-4 K-weighting filter parameters (from the standard + Audio EQ Cookbook)
constexpr double PRE_FILTER_FC = 1681.974;    // pre-filter center frequency (Hz)
constexpr double PRE_FILTER_GAIN_DB = 3.9998; // pre-filter shelf gain (dB)
constexpr double PRE_FILTER_Q = 0.7072;       // pre-filter quality factor
constexpr double RLB_FILTER_FC = 38.135;      // revised low-frequency B-curve cutoff (Hz)
constexpr double RLB_FILTER_Q = 0.5003;       // RLB quality factor

// BS.1770-4 gating parameters
// constexpr double BLOCK_DURATION = 0.4;         // gating block duration (seconds)
constexpr double BLOCK_OVERLAP_STEP = 0.1;     // step between overlapping blocks (seconds)
constexpr double ABSOLUTE_GATE_LUFS = -70.0;   // absolute gating threshold (LUFS)
constexpr double RELATIVE_GATE_OFFSET = -10.0; // relative gate is mean loudness + this (LUFS)
constexpr double LOUDNESS_OFFSET = -0.691;     // constant offset in LUFS formula

// BS.1770-4 channel weights
constexpr float WEIGHT_FRONT = 1.0f;     // L, R, C
constexpr float WEIGHT_SURROUND = 1.41f; // Ls, Rs (approx. +1.5 dB)
constexpr float WEIGHT_LFE = 0.0f;       // LFE excluded from measurement

// K-weighting pre-filter: high shelf modeling head-related acoustic effects
// Coefficients computed in double, then narrowed to float for runtime use.
BiquadCoeffs computePreFilterCoeffs(double sampleRate)
{
	double A = std::pow(10.0, PRE_FILTER_GAIN_DB / 40.0); // sqrt of linear gain
	double w0 = 2.0 * M_PI * PRE_FILTER_FC / sampleRate;
	double cosw0 = std::cos(w0);
	double sinw0 = std::sin(w0);
	double alpha = sinw0 / (2.0 * PRE_FILTER_Q);

	double sqrtA = std::sqrt(A);
	double a0 = (A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha;
	double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
	double a2 = (A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha;
	double b0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha);
	double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
	double b2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha);

	return {.b0 = (float)(b0 / a0), .b1 = (float)(b1 / a0), .b2 = (float)(b2 / a0), .a1 = (float)(a1 / a0), .a2 = (float)(a2 / a0)};
}

// K-weighting RLB filter: high pass for revised low-frequency B-curve
BiquadCoeffs computeRLBCoeffs(double sampleRate)
{
	double w0 = 2.0 * M_PI * RLB_FILTER_FC / sampleRate;
	double cosw0 = std::cos(w0);
	double sinw0 = std::sin(w0);
	double alpha = sinw0 / (2.0 * RLB_FILTER_Q);

	double a0 = 1.0 + alpha;
	double a1 = -2.0 * cosw0;
	double a2 = 1.0 - alpha;
	double b0 = (1.0 + cosw0) / 2.0;
	double b1 = -(1.0 + cosw0);
	double b2 = (1.0 + cosw0) / 2.0;

	return {.b0 = (float)(b0 / a0), .b1 = (float)(b1 / a0), .b2 = (float)(b2 / a0), .a1 = (float)(a1 / a0), .a2 = (float)(a2 / a0)};
}

// Channel weights per ITU-R BS.1770-4
// Returns 0.0 for channels to exclude (e.g. LFE)
float channelWeight(unsigned int aChannel, unsigned int aChannelCount)
{
	switch (aChannelCount)
	{
	case 1: // mono
	case 2: // stereo: L, R
		return WEIGHT_FRONT;
	case 4:
		// quad: FL, FR, RL, RR
		return (aChannel >= 2) ? WEIGHT_SURROUND : WEIGHT_FRONT;
	case 6:
		// 5.1: L, R, C, LFE, Ls, Rs
		if (aChannel == 3)
			return WEIGHT_LFE;
		if (aChannel >= 4)
			return WEIGHT_SURROUND;
		return WEIGHT_FRONT;
	case 8:
		// 7.1: L, R, C, LFE, SL, SR, BL, BR
		if (aChannel == 3)
			return WEIGHT_LFE;
		if (aChannel == 4 || aChannel == 5)
			return WEIGHT_SURROUND;
		return WEIGHT_FRONT;
	default:
		return WEIGHT_FRONT;
	}
}
} // namespace

void kWeightChunkScalar(const BiquadCoeffs &aPreCoeffs, const BiquadCoeffs &aRlbCoeffs, BiquadState *aPreState, BiquadState *aRlbState, const float *aBuffer,
                        unsigned int aSamplesRead, unsigned int aBufStride, unsigned int aChannels, float *aOut)
{
	float temp = 0.f;
	for (unsigned int ch = 0; ch < aChannels; ch++)
	{
		const float *src = aBuffer + (size_t)(ch * aBufStride);
		float *dst = aOut + (size_t)(ch * aBufStride);
		for (unsigned int i = 0; i < aSamplesRead; i++)
		{
			float sample = src[i];
#define BIQUAD_PROCESS(sample_, c, s) \
	temp = (c).b0 * (sample_) + (c).b1 * (s).x1 + (c).b2 * (s).x2 - (c).a1 * (s).y1 - (c).a2 * (s).y2; \
	(s).x2 = (s).x1; \
	(s).x1 = (sample_); \
	(s).y2 = (s).y1; \
	(s).y1 = temp; \
	(sample_) = temp;
			BIQUAD_PROCESS(sample, aPreCoeffs, aPreState[ch]);
			BIQUAD_PROCESS(sample, aRlbCoeffs, aRlbState[ch]);
#undef BIQUAD_PROCESS
			dst[i] = sample;
		}
	}
}

result integratedLoudness(AudioSource &aSource, float &aLoudness)
{
	std::unique_ptr<AudioSourceInstance> instance{aSource.createInstance()};

	if (!instance)
		return INVALID_PARAMETER;

	instance->init(aSource, 0);
	instance->mFlags &= ~AudioSourceInstance::LOOPING;

	unsigned int channels = instance->mChannels;
	float sampleRate = instance->mBaseSamplerate;

	if (channels == 0 || channels > MAX_CHANNELS || sampleRate <= 0.0f)
		return INVALID_PARAMETER;

	// select K-weighting function: SSE2 for 2+ channels, scalar otherwise
	KWeightChunkFn kWeightChunk = kWeightChunkScalar;
#if defined(SOLOUD_SUPPORT_SSE2)
	if (channels >= 2 && (detectCPUextensions() & CPUFEATURE_SSE2))
		kWeightChunk = kWeightChunkSSE;
#endif

	// K-weighting filter coefficients and per-channel state
	BiquadCoeffs preCoeffs = computePreFilterCoeffs(sampleRate);
	BiquadCoeffs rlbCoeffs = computeRLBCoeffs(sampleRate);

	BiquadState preState[MAX_CHANNELS]{};
	BiquadState rlbState[MAX_CHANNELS]{};

	// Step-based block parameters: 4 steps per block (400ms block, 100ms step)
	constexpr unsigned int STEPS_PER_BLOCK = 4;
	unsigned int stepSize = (unsigned int)(sampleRate * BLOCK_OVERLAP_STEP);

	if (stepSize == 0)
		return INVALID_PARAMETER;

	unsigned int blockSize = STEPS_PER_BLOCK * stepSize;

	// Channel weights
	float weights[MAX_CHANNELS];
	for (unsigned int ch = 0; ch < channels; ch++)
		weights[ch] = channelWeight(ch, channels);

	// Step-based accumulation: current step sum-of-squares and ring of completed step sums
	double stepSumSq[MAX_CHANNELS]{};
	double stepSums[STEPS_PER_BLOCK][MAX_CHANNELS]{};
	unsigned int stepPos = 0;        // samples processed in current step
	unsigned int completedSteps = 0; // total steps completed
	unsigned int stepRingPos = 0;    // position in stepSums ring
	unsigned int totalSamples = 0;

	// Per-block per-channel mean square for gating. Flat layout: [block * channels + ch]
	std::vector<float> blockMeanSq;

	// Read audio in SAMPLE_GRANULARITY chunks
	float buffer[SAMPLE_GRANULARITY * MAX_CHANNELS];
	float kWeightedChunk[MAX_CHANNELS * SAMPLE_GRANULARITY];

	for (;;)
	{
		unsigned int samplesRead = instance->getAudio(buffer, SAMPLE_GRANULARITY, SAMPLE_GRANULARITY);
		if (samplesRead == 0 && instance->hasEnded())
			break;

		// pass 1: K-weight the entire chunk
		kWeightChunk(preCoeffs, rlbCoeffs, preState, rlbState, buffer, samplesRead, SAMPLE_GRANULARITY, channels, kWeightedChunk);

		// pass 2: square-and-accumulate with step boundary tracking
		unsigned int processed = 0;
		while (processed < samplesRead)
		{
			unsigned int n = samplesRead - processed;
			unsigned int remaining = stepSize - stepPos;
			if (n > remaining)
				n = remaining;

			for (unsigned int ch = 0; ch < channels; ch++)
			{
				const float *src = kWeightedChunk + (size_t)(ch * SAMPLE_GRANULARITY) + processed;
				double acc = 0.0;
				for (unsigned int i = 0; i < n; i++)
					acc += (double)src[i] * src[i];
				stepSumSq[ch] += acc;
			}

			stepPos += n;
			processed += n;
			totalSamples += n;

			if (stepPos >= stepSize)
			{
				// step complete: store into ring
				for (unsigned int ch = 0; ch < channels; ch++)
				{
					stepSums[stepRingPos][ch] = stepSumSq[ch];
					stepSumSq[ch] = 0.0;
				}
				stepRingPos = (stepRingPos + 1) % STEPS_PER_BLOCK;
				stepPos = 0;
				completedSteps++;

				// emit a block once we have enough steps
				if (completedSteps >= STEPS_PER_BLOCK)
				{
					for (unsigned int ch = 0; ch < channels; ch++)
					{
						double sum = 0.0;
						for (unsigned int s = 0; s < STEPS_PER_BLOCK; s++)
							sum += stepSums[s][ch];
						blockMeanSq.push_back((float)(sum / blockSize));
					}
				}
			}
		}

		if (instance->hasEnded())
			break;
	}

	size_t numBlocks = blockMeanSq.size() / channels;

	// Handle short audio (< one full block): use all available samples
	if (numBlocks == 0 && totalSamples > 0)
	{
		// sum all completed step sums + partial current step
		for (unsigned int ch = 0; ch < channels; ch++)
		{
			double sum = stepSumSq[ch];
			for (unsigned int s = 0; s < completedSteps && s < STEPS_PER_BLOCK; s++)
				sum += stepSums[s][ch];
			blockMeanSq.push_back((float)(sum / totalSamples));
		}
		numBlocks = 1;
	}

	if (numBlocks == 0)
	{
		aLoudness = (float)-HUGE_VAL;
		return SO_NO_ERROR;
	}

	// Compute per-block loudness and apply absolute gate
	std::vector<double> blockLoudness(numBlocks);

	// Pass 1: absolute gate
	unsigned int countAbove = 0;
	double chanSumAbs[MAX_CHANNELS]{};

	for (size_t b = 0; b < numBlocks; b++)
	{
		double weightedSum = 0.0;
		for (unsigned int ch = 0; ch < channels; ch++)
			weightedSum += weights[ch] * blockMeanSq[b * channels + ch];

		if (weightedSum <= 0.0)
		{
			blockLoudness[b] = -HUGE_VAL;
			continue;
		}

		blockLoudness[b] = LOUDNESS_OFFSET + 10.0 * std::log10(weightedSum);

		if (blockLoudness[b] >= ABSOLUTE_GATE_LUFS)
		{
			for (unsigned int ch = 0; ch < channels; ch++)
				chanSumAbs[ch] += blockMeanSq[b * channels + ch];
			countAbove++;
		}
	}

	if (countAbove == 0)
	{
		aLoudness = (float)-HUGE_VAL;
		return SO_NO_ERROR;
	}

	// Compute relative threshold: mean loudness of blocks above absolute gate, plus offset
	double gammaAWeightedSum = 0.0;
	for (unsigned int ch = 0; ch < channels; ch++)
		gammaAWeightedSum += weights[ch] * (chanSumAbs[ch] / countAbove);

	double gammaA = LOUDNESS_OFFSET + 10.0 * std::log10(gammaAWeightedSum);
	double relativeThreshold = gammaA + RELATIVE_GATE_OFFSET;

	// Pass 2: relative gate
	double chanSumRel[MAX_CHANNELS]{};
	unsigned int countRel = 0;

	for (size_t b = 0; b < numBlocks; b++)
	{
		if (blockLoudness[b] >= ABSOLUTE_GATE_LUFS && blockLoudness[b] >= relativeThreshold)
		{
			for (unsigned int ch = 0; ch < channels; ch++)
				chanSumRel[ch] += blockMeanSq[b * channels + ch];
			countRel++;
		}
	}

	if (countRel == 0)
	{
		aLoudness = (float)-HUGE_VAL;
		return SO_NO_ERROR;
	}

	// Final integrated loudness
	double finalWeightedSum = 0.0;
	for (unsigned int ch = 0; ch < channels; ch++)
		finalWeightedSum += weights[ch] * (chanSumRel[ch] / countRel);

	if (finalWeightedSum <= 0.0)
	{
		aLoudness = (float)-HUGE_VAL;
		return SO_NO_ERROR;
	}

	aLoudness = (float)(LOUDNESS_OFFSET + 10.0 * std::log10(finalWeightedSum));
	return SO_NO_ERROR;
}
} // namespace SoLoud::Loudness
