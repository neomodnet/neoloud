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
constexpr double BLOCK_DURATION = 0.4;         // gating block duration (seconds)
constexpr double BLOCK_OVERLAP_STEP = 0.1;     // step between overlapping blocks (seconds)
constexpr double ABSOLUTE_GATE_LUFS = -70.0;   // absolute gating threshold (LUFS)
constexpr double RELATIVE_GATE_OFFSET = -10.0; // relative gate is mean loudness + this (LUFS)
constexpr double LOUDNESS_OFFSET = -0.691;     // constant offset in LUFS formula

// BS.1770-4 channel weights
constexpr double WEIGHT_FRONT = 1.0;     // L, R, C
constexpr double WEIGHT_SURROUND = 1.41; // Ls, Rs (approx. +1.5 dB)
constexpr double WEIGHT_LFE = 0.0;       // LFE excluded from measurement

struct BiquadCoeffs
{
	double b0, b1, b2, a1, a2; // a0 normalized to 1.0
};

struct BiquadState
{
	double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

inline double biquadProcess(const BiquadCoeffs &c, BiquadState &s, double x)
{
	double y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
	s.x2 = s.x1;
	s.x1 = x;
	s.y2 = s.y1;
	s.y1 = y;
	return y;
}

// K-weighting pre-filter: high shelf modeling head-related acoustic effects
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

	return {.b0 = b0 / a0, .b1 = b1 / a0, .b2 = b2 / a0, .a1 = a1 / a0, .a2 = a2 / a0};
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

	return {.b0 = b0 / a0, .b1 = b1 / a0, .b2 = b2 / a0, .a1 = a1 / a0, .a2 = a2 / a0};
}

// Channel weights per ITU-R BS.1770-4
// Returns 0.0 for channels to exclude (e.g. LFE)
double channelWeight(unsigned int aChannel, unsigned int aChannelCount)
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

result integratedLoudness(AudioSource &aSource, float &aLoudness)
{
	std::unique_ptr<AudioSourceInstance> instance{aSource.createInstance()};

	if (!instance)
		return INVALID_PARAMETER;

	instance->init(aSource, 0);
	instance->mFlags &= ~AudioSourceInstance::LOOPING;

	unsigned int channels = instance->mChannels;
	float sampleRate = instance->mBaseSamplerate;

	if (channels == 0 || sampleRate <= 0.0f)
		return INVALID_PARAMETER;

	// K-weighting filter coefficients and state
	BiquadCoeffs preCoeffs = computePreFilterCoeffs(sampleRate);
	BiquadCoeffs rlbCoeffs = computeRLBCoeffs(sampleRate);

	std::vector<BiquadState> preState(channels);
	std::vector<BiquadState> rlbState(channels);

	// Block parameters
	unsigned int blockSize = (unsigned int)(sampleRate * BLOCK_DURATION);
	unsigned int stepSize = (unsigned int)(sampleRate * BLOCK_OVERLAP_STEP);

	if (blockSize == 0 || stepSize == 0)
		return INVALID_PARAMETER;

	// Channel weights
	std::vector<double> weights(channels);
	for (unsigned int ch = 0; ch < channels; ch++)
		weights[ch] = channelWeight(ch, channels);

	// Ring buffer for K-weighted samples (we need blockSize samples to compute a block)
	std::vector<std::vector<double>> kWeighted(channels, std::vector<double>(blockSize, 0.0));
	unsigned int ringPos = 0;
	unsigned int totalSamples = 0;

	// Store per-block per-channel mean square values for gating
	std::vector<std::vector<double>> blockMeanSq; // [block][channel]

	// Read audio in SAMPLE_GRANULARITY chunks
	unsigned int bufSize = SAMPLE_GRANULARITY;
	std::vector<float> buffer((size_t)bufSize * channels, 0.0f);

	for (;;)
	{
		unsigned int samplesRead = instance->getAudio(buffer.data(), bufSize, bufSize);
		if (samplesRead == 0 && instance->hasEnded())
			break;

		for (unsigned int i = 0; i < samplesRead; i++)
		{
			for (unsigned int ch = 0; ch < channels; ch++)
			{
				double sample = (double)buffer[ch * bufSize + i];

				// apply K-weighting: pre-filter then RLB
				sample = biquadProcess(preCoeffs, preState[ch], sample);
				sample = biquadProcess(rlbCoeffs, rlbState[ch], sample);

				kWeighted[ch][ringPos] = sample;
			}
			ringPos = (ringPos + 1) % blockSize;
			totalSamples++;

			// emit a block every stepSize samples, once we have at least blockSize
			if (totalSamples >= blockSize && ((totalSamples - blockSize) % stepSize == 0))
			{
				std::vector<double> meanSq(channels, 0.0);
				for (unsigned int ch = 0; ch < channels; ch++)
				{
					double sum = 0.0;
					for (unsigned int j = 0; j < blockSize; j++)
						sum += kWeighted[ch][j] * kWeighted[ch][j];
					meanSq[ch] = sum / blockSize;
				}
				blockMeanSq.push_back(std::move(meanSq));
			}
		}

		if (instance->hasEnded())
			break;
	}

	// Handle short audio (< 400ms): use all available samples as one block
	if (blockMeanSq.empty() && totalSamples > 0)
	{
		std::vector<double> meanSq(channels, 0.0);
		for (unsigned int ch = 0; ch < channels; ch++)
		{
			double sum = 0.0;
			for (unsigned int j = 0; j < totalSamples; j++)
				sum += kWeighted[ch][j] * kWeighted[ch][j];
			meanSq[ch] = sum / totalSamples;
		}
		blockMeanSq.push_back(std::move(meanSq));
	}

	if (blockMeanSq.empty())
	{
		aLoudness = (float)-HUGE_VAL;
		return SO_NO_ERROR;
	}

	// Compute per-block loudness and apply absolute gate
	size_t numBlocks = blockMeanSq.size();
	std::vector<double> blockLoudness(numBlocks);

	// Pass 1: absolute gate
	unsigned int countAbove = 0;
	std::vector<double> chanSumAbs(channels, 0.0);

	for (size_t b = 0; b < numBlocks; b++)
	{
		double weightedSum = 0.0;
		for (unsigned int ch = 0; ch < channels; ch++)
			weightedSum += weights[ch] * blockMeanSq[b][ch];

		if (weightedSum <= 0.0)
		{
			blockLoudness[b] = -HUGE_VAL;
			continue;
		}

		blockLoudness[b] = LOUDNESS_OFFSET + 10.0 * std::log10(weightedSum);

		if (blockLoudness[b] >= ABSOLUTE_GATE_LUFS)
		{
			for (unsigned int ch = 0; ch < channels; ch++)
				chanSumAbs[ch] += blockMeanSq[b][ch];
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
	std::vector<double> chanSumRel(channels, 0.0);
	unsigned int countRel = 0;

	for (size_t b = 0; b < numBlocks; b++)
	{
		if (blockLoudness[b] >= ABSOLUTE_GATE_LUFS && blockLoudness[b] >= relativeThreshold)
		{
			for (unsigned int ch = 0; ch < channels; ch++)
				chanSumRel[ch] += blockMeanSq[b][ch];
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
