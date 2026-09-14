/*
SoLoud audio engine - per-voice time-stretch stage
Copyright (c) 2013-2020 Jari Komppa
Copyright (c) 2026 William Horvath

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

#include "soloud_timestretch.h"

#include "soloud.h"
#include "soloud_audiosource.h"
#include "soloud_config.h"
#include "soloud_containers.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring> // the vendored fft.h uses std::memcpy without including it
#include <limits>
#include <random>
#include "signalsmith-stretch/signalsmith-linear/fft.h"
#include "signalsmith-stretch/signalsmith-stretch.h"

namespace SoLoud
{
namespace
{
// a frame position or length on an engine's source or output timeline, counted since priming
using idx = int64_t;

unsigned int frames(double aSeconds, float aSamplerate)
{
	return (unsigned int)std::lround(aSeconds * aSamplerate);
}

unsigned int nextPowerOfTwo(unsigned int aValue)
{
	unsigned int v = 1;
	while (v < aValue)
		v *= 2;
	return v;
}

// channel aChannel of a buffer laid out channel by channel, aFrames per channel
float *lane(const AlignedFloatBuffer &aBuffer, unsigned int aChannel, unsigned int aFrames)
{
	return aBuffer.mData + (size_t)aChannel * aFrames;
}

// reference frames accumulated into the candidates' correlations per pass over them: a pass loads and stores every correlation once, so the
// block sets how many multiply-adds that traffic is spread over; past this the vector registers run out
constexpr unsigned int CORRELATION_BLOCK = 8;

// aScore[j] += the correlation of aReference (aOverlap frames) with aWindow from its frame j on, for aCount candidates j. the loops run across
// candidates for a block of reference frames at a time, which vectorises and keeps each candidate's sum in a register across the block; the
// frames go in the same order as one at a time, so the sums come out the same
void correlate(const float *aWindow, const float *aReference, float *aScore, unsigned int aOverlap, unsigned int aCount)
{
	unsigned int i = 0;
	for (; i + CORRELATION_BLOCK <= aOverlap; i += CORRELATION_BLOCK)
	{
		float r[CORRELATION_BLOCK];
		for (unsigned int k = 0; k < CORRELATION_BLOCK; k++)
			r[k] = aReference[i + k];
		const float *w = aWindow + i;
		for (unsigned int j = 0; j < aCount; j++)
		{
			float s = aScore[j];
			for (unsigned int k = 0; k < CORRELATION_BLOCK; k++)
				s += r[k] * w[j + k];
			aScore[j] = s;
		}
	}
	for (; i < aOverlap; i++)
	{
		const float r = aReference[i];
		const float *w = aWindow + i;
		for (unsigned int j = 0; j < aCount; j++)
			aScore[j] += r * w[j];
	}
}
} // namespace

// What the stage asks of an engine. Source and output frames are counted since priming, and the stage feeds source in proportion to the
// output it asks for (tempo source frames per output frame, tracked to the fraction).
class TimeStretcher::Engine
{
public:
	Engine() = default;
	virtual ~Engine() = default;
	Engine(const Engine &) = delete;
	Engine &operator=(const Engine &) = delete;
	Engine(Engine &&) = delete;
	Engine &operator=(Engine &&) = delete;

	// Source frames to prime with at aTempo
	[[nodiscard]] virtual unsigned int primeLength(double aTempo) const = 0;
	// Start afresh from aFrames source frames (channel-separated): the first output frame produced after this is the first of them
	virtual void prime(float **aChannels, unsigned int aFrames) = 0;
	virtual void setTempo(double aTempo) = 0;
	virtual void setPitch(float aPitch) = 0;
	virtual void process(float **aInputs, unsigned int aIn, float **aOutputs, unsigned int aOut) = 0;
	// Output frames between a tempo change and the first output frame that reflects it
	[[nodiscard]] virtual unsigned int outputLatency() const = 0;
	// Source frames wanted on top of the tempo's worth before the next output (after a tempo change raised what the engine reads ahead)
	[[nodiscard]] virtual unsigned int inputDeficit() const { return 0; }
	// The source is discontinuous at its aIndex-th frame since priming: a loop wrap, or the padding after its end
	virtual void anchor(unsigned int /*aIndex*/) {}
};

// The vendored Signalsmith Stretch. Transparent at unity; the pre-roll its outputSeek() priming synthesises only approximates the source for
// the first outputLatency() frames. The tempo is implicit in the ratio of frames fed to frames asked for.
class TimeStretcher::PhaseVocoderEngine final : public TimeStretcher::Engine
{
public:
	PhaseVocoderEngine(unsigned int aChannels, float aSamplerate)
	{
		mStretch.configure((int)aChannels, (int)(aSamplerate * BLOCK_SECONDS), (int)(aSamplerate * INTERVAL_SECONDS), /*splitComputation=*/true);
	}
	[[nodiscard]] unsigned int primeLength(double aTempo) const override { return (unsigned int)mStretch.outputSeekLength((float)aTempo); }
	void prime(float **aChannels, unsigned int aFrames) override { mStretch.outputSeek(aChannels, (int)aFrames); }
	void setTempo(double /*aTempo*/) override {}
	void setPitch(float aPitch) override { mStretch.setTransposeFactor(aPitch); }
	void process(float **aInputs, unsigned int aIn, float **aOutputs, unsigned int aOut) override { mStretch.process(aInputs, (int)aIn, aOutputs, (int)aOut); }
	[[nodiscard]] unsigned int outputLatency() const override { return (unsigned int)mStretch.outputLatency(); }

private:
	// the library only draws random phases at extreme ratios, but a fixed seed keeps the output deterministic either way
	static constexpr long RANDOM_SEED = 0x5010;

	signalsmith::stretch::SignalsmithStretch<float, std::mt19937> mStretch{RANDOM_SEED};
};

// WSOLA with the source's onsets anchored. The output is a chain of source sequences crossfaded into each other. A normal sequence starts at
// whichever source position within the search window around the nominal one best continues the previous sequence's waveform, so what plays
// between seams is the source's own waveform. The sequence holding an onset is played 1:1 and placed so that the onset lands on its nominal
// output time; onsets come from the detector, and from the loop wraps and the end the stage announces. Sequences are scheduled on the
// engine's own output timeline, so the output doesn't depend on how it is pulled. Everything is allocated in the constructor, except that
// the source ring grows when the tempo rises past what it was sized for.
class TimeStretcher::WsolaEngine final : public TimeStretcher::Engine
{
public:
	WsolaEngine(unsigned int aChannels, float aSamplerate);
	[[nodiscard]] unsigned int primeLength(double aTempo) const override { return lookahead(aTempo); }
	void prime(float **aChannels, unsigned int aFrames) override;
	void setTempo(double aTempo) override;
	void setPitch(float /*aPitch*/) override {} // never primed with a pitch shift
	void process(float **aInputs, unsigned int aIn, float **aOutputs, unsigned int aOut) override;
	// the frames between what was pulled and the next sequence's start are complete and keep the tempo they were made at
	[[nodiscard]] unsigned int outputLatency() const override { return (unsigned int)(mStart - mPulled); }
	[[nodiscard]] unsigned int inputDeficit() const override;
	void anchor(unsigned int aIndex) override { addOnset(aIndex); }

private:
	// a normal sequence advances the output by HOP and is crossfaded into the next over OVERLAP, its source start searched within SEEK of the
	// nominal position
	static constexpr double HOP_SECONDS = 0.009;
	static constexpr double OVERLAP_SECONDS = 0.006;
	static constexpr double SEEK_SECONDS = 0.030;
	// a candidate scores (correlation + SCORE_BIAS) * (1 - CENTRE_BIAS * t^2), t running from -1 to 1 across the window, so that the middle of
	// the window wins ties and the content doesn't drift
	static constexpr double CENTRE_BIAS = 0.25;
	static constexpr double SCORE_BIAS = 0.1;
	static constexpr double ENERGY_EPSILON = 1e-9;
	// an anchored sequence runs 1:1 from PRE before its onset (at least the crossfade, so that the crossfade is over before the onset) to POST
	// after it, cut short only to hand over to the next onset's own sequence; the sequence before it takes a hop of up to MAX_HOP so as to start
	// it PRE_TARGET before the onset; the time the 1:1 region puts the content off the nominal position is worked off over REPAY
	static constexpr double PRE_MIN_SECONDS = 0.007;
	static constexpr double PRE_TARGET_SECONDS = 0.010;
	static constexpr double PRE_MAX_SECONDS = 0.014;
	static constexpr double POST_SECONDS = 0.025;
	static constexpr double POST_MIN_SECONDS = 0.004;
	static constexpr double MAX_HOP_SECONDS = 0.0135;
	static constexpr double REPAY_SECONDS = 0.1;
	// no sequence plays this close to an onset it doesn't hold
	static constexpr double ONSET_MARGIN_SECONDS = 0.001;
	// source frames fed beyond what can be needed, for the rounding of the stage's feeds
	static constexpr unsigned int LOOKAHEAD_SLACK = 16;
	// onsets kept: the ones a search may still reach behind the nominal position, and the ones known ahead of it
	static constexpr unsigned int MAX_ONSETS = 64;
	// onset detection: an STFT with a window of about WINDOW (rounded up to a power of two) hopped by a quarter of it, over the bins below
	// MAX_HZ. A frame's flux is how much louder its bins are than two hops earlier, on a log scale, ignoring what a slight shift in frequency
	// explains. A flux peak (over PEAK_FRAMES either side) that stands above the mean of the PAST_FRAMES before it and the PEAK_FRAMES after
	// by THRESHOLD of the flux's recent maximum (decaying over SCALE_SECONDS) is an onset, placed at the sample where the level of the audio
	// up to it is halfway through its steepest rise (ENVELOPE and SLOPE long respectively). Onsets closer than MIN_GAP count as one.
	static constexpr double WINDOW_SECONDS = 0.018;
	static constexpr double MAX_HZ = 16000.0;
	static constexpr float LOG_GAIN = 500.0f;
	static constexpr unsigned int PEAK_FRAMES = 3;
	static constexpr unsigned int PAST_FRAMES = 8;
	static constexpr unsigned int FLUX_FRAMES = PAST_FRAMES + PEAK_FRAMES + 1;
	static constexpr double THRESHOLD = 0.06;
	static constexpr double SCALE_SECONDS = 2.0;
	static constexpr double MIN_GAP_SECONDS = 0.02;
	static constexpr double ENVELOPE_SECONDS = 0.001;
	static constexpr double SLOPE_SECONDS = 0.002;

	// source frames the engine reads ahead of the nominal position of the sequence it places, and keeps behind it
	[[nodiscard]] unsigned int lookahead(double aTempo) const;
	[[nodiscard]] unsigned int retention(double aTempo) const;
	[[nodiscard]] unsigned int detectorDelay() const { return mWindow + (PEAK_FRAMES + 1) * mAnalysisHop; }
	void resizeRing(double aTempo);
	[[nodiscard]] float *ring(unsigned int aChannel) { return lane(mRing, aChannel, mRingFrames); }
	[[nodiscard]] size_t ringSlot(idx aIndex) const { return (size_t)(aIndex & (mRingFrames - 1)); }
	[[nodiscard]] size_t outSlot(idx aIndex) const { return (size_t)(aIndex & (mOutFrames - 1)); }
	[[nodiscard]] float sourceAt(unsigned int aChannel, idx aIndex) const;
	[[nodiscard]] float monoAt(idx aIndex) const;
	void feed(float **aInputs, unsigned int aFrames);
	void analyse();
	void refine(idx aFrame);
	void addOnset(idx aSource);
	[[nodiscard]] bool onsetIn(idx aFrom, idx aTo) const;
	void commit();
	[[nodiscard]] idx search(idx aCentre, idx aLength, idx aAnchoredHead);
	void write(idx aStart, idx aSource, idx aLength, bool aFadeHead);

	const unsigned int mChannels;
	// geometry, in frames
	const idx mHop;
	const idx mOverlap;
	const idx mHalfSeek;
	const idx mPreMin;
	const idx mPreTarget;
	const idx mPreMax;
	const idx mPost;
	const idx mPostMin;
	const idx mMaxHop;
	const idx mRepay;
	const idx mMargin;
	const idx mMaxLength; // of a sequence
	const idx mMinGap;
	AlignedFloatBuffer mFadeIn;
	AlignedFloatBuffer mFadeOut;
	double mTempo = 1.0;
	// the source since priming, the last mRingFrames of it, by frame
	unsigned int mRingFrames = 0;
	AlignedFloatBuffer mRing;
	idx mFed = 0;
	// the output since priming: complete up to mStart, the next sequence's start, with the previous sequence's fading tail written past it
	const unsigned int mOutFrames;
	AlignedFloatBuffer mOut;
	idx mPulled = 0;
	idx mStart = 0;
	double mNominal = 0.0; // source position of output frame mStart
	bool mFirst = true;
	idx mPrevSource = 0;
	idx mPrevLength = 0;
	double mDebt = 0.0; // content position minus nominal position where the last anchored sequence ended, worked off from mDebtFrom on
	idx mDebtFrom = 0;
	std::array<idx, MAX_ONSETS> mOnsets{}; // source frames, ascending; those before mNextOnset have had their sequence
	unsigned int mOnsetCount = 0;
	unsigned int mNextOnset = 0;
	// detector
	const unsigned int mWindow;
	const unsigned int mAnalysisHop;
	const unsigned int mBins;
	AlignedFloatBuffer mHann;
	AlignedFloatBuffer mFrame;
	AlignedFloatBuffer mSpectrum;                 // real parts, then imaginary parts
	AlignedFloatBuffer mLogMagnitude;             // the last three frames
	signalsmith::linear::Pow2RealFFT<float> mFft; // allocates its tables when constructed
	std::array<double, FLUX_FRAMES> mFlux{};
	idx mAnalysed = 0;        // STFT frames analysed
	double mScale = 0.0;      // the flux's recent maximum
	const double mScaleDecay; // per frame
	const unsigned int mEnvelopeFrames;
	const unsigned int mSlopeFrames;
	AlignedFloatBuffer mMono;
	AlignedFloatBuffer mEnvelope;
	// search scratch: the candidates' source, contiguous per channel, the two references, and the per-candidate correlations and energies
	const unsigned int mSearchFrames;
	AlignedFloatBuffer mSearch;
	AlignedFloatBuffer mReference;
	AlignedFloatBuffer mReference2;
	AlignedFloatBuffer mScore;
	AlignedFloatBuffer mScore2;
	AlignedFloatBuffer mEnergy;
	AlignedFloatBuffer mEnergy2;
};

TimeStretcher::WsolaEngine::WsolaEngine(unsigned int aChannels, float aSamplerate)
    : mChannels(aChannels),
      mHop(frames(HOP_SECONDS, aSamplerate)),
      mOverlap(frames(OVERLAP_SECONDS, aSamplerate)),
      mHalfSeek(frames(SEEK_SECONDS, aSamplerate) / 2),
      mPreMin(frames(PRE_MIN_SECONDS, aSamplerate)),
      mPreTarget(frames(PRE_TARGET_SECONDS, aSamplerate)),
      mPreMax(frames(PRE_MAX_SECONDS, aSamplerate)),
      mPost(frames(POST_SECONDS, aSamplerate)),
      mPostMin(frames(POST_MIN_SECONDS, aSamplerate)),
      mMaxHop(frames(MAX_HOP_SECONDS, aSamplerate)),
      mRepay(frames(REPAY_SECONDS, aSamplerate)),
      mMargin(frames(ONSET_MARGIN_SECONDS, aSamplerate)),
      mMaxLength(mPreMax + mPost + mHop + mOverlap),
      mMinGap(frames(MIN_GAP_SECONDS, aSamplerate)),
      mFadeIn((unsigned int)mOverlap),
      mFadeOut((unsigned int)mOverlap),
      mOutFrames(nextPowerOfTwo((unsigned int)mMaxLength + 2 * SAMPLE_GRANULARITY)),
      mOut(mOutFrames * aChannels),
      mWindow(nextPowerOfTwo(frames(WINDOW_SECONDS, aSamplerate))),
      mAnalysisHop(mWindow / 4),
      mBins(std::min(mWindow / 2, (unsigned int)(MAX_HZ * mWindow / aSamplerate))),
      mHann(mWindow),
      mFrame(mWindow),
      mSpectrum(mWindow),
      mLogMagnitude(3 * mBins),
      mFft(mWindow),
      mScaleDecay(std::exp(-(double)mAnalysisHop / (aSamplerate * SCALE_SECONDS))),
      mEnvelopeFrames(frames(ENVELOPE_SECONDS, aSamplerate)),
      mSlopeFrames(frames(SLOPE_SECONDS, aSamplerate)),
      mMono(mWindow / 2 + 2 * mAnalysisHop + mSlopeFrames + mEnvelopeFrames + 1),
      mEnvelope(mWindow / 2 + 2 * mAnalysisHop + mSlopeFrames + 1),
      mSearchFrames(2 * (unsigned int)mHalfSeek + (unsigned int)mMaxLength),
      mSearch(mSearchFrames * aChannels),
      mReference((unsigned int)mOverlap * aChannels),
      mReference2((unsigned int)mOverlap * aChannels),
      mScore(2 * (unsigned int)mHalfSeek + 1),
      mScore2(2 * (unsigned int)mHalfSeek + 1),
      mEnergy(2 * (unsigned int)mHalfSeek + 1),
      mEnergy2(2 * (unsigned int)mHalfSeek + 1)
{
	mLogMagnitude.clear();
	for (unsigned int j = 0; j < mOverlap; j++)
	{
		// raised cosine: keeps the level where the two sides carry the same waveform, which is what the search aims for
		mFadeIn.mData[j] = (float)(0.5 - 0.5 * std::cos(M_PI * (j + 0.5) / (double)mOverlap));
		mFadeOut.mData[j] = 1.0f - mFadeIn.mData[j];
	}
	for (unsigned int i = 0; i < mWindow; i++)
		mHann.mData[i] = (float)(0.5 - 0.5 * std::cos(2.0 * M_PI * i / mWindow));
	resizeRing(mTempo);
}

unsigned int TimeStretcher::WsolaEngine::lookahead(double aTempo) const
{
	// the onsets consulted when placing a sequence reach to the one after the next anchored one, and the detector reports an onset its delay
	// after it; the search reads candidates up to half the window past the nominal position, each a sequence long, plus the debt an anchored
	// sequence leaves when slowing down
	const double horizon = aTempo * (double)(mPreMax + mPost + mHop + mPreMax) + detectorDelay();
	const double reach = std::max(0.0, 1.0 - aTempo) * (double)(mPreMax + mPost) + (double)(mHalfSeek + mMaxLength);
	return (unsigned int)std::ceil(std::max(horizon, reach)) + LOOKAHEAD_SLACK;
}

unsigned int TimeStretcher::WsolaEngine::retention(double aTempo) const
{
	// a search reads back to half the window behind the nominal position, less the debt an anchored sequence leaves when speeding up, and
	// takes its reference from the previous sequence, up to the longest sequence's worth of source earlier
	return (unsigned int)std::ceil((double)mHalfSeek + std::max(0.0, aTempo - 1.0) * (double)(mPreMax + mPost) + aTempo * (double)mMaxLength) + LOOKAHEAD_SLACK;
}

void TimeStretcher::WsolaEngine::resizeRing(double aTempo)
{
	// what a produce() can leave in the ring: the retention, the lookahead twice over (a raised tempo's is fed on top of what is there) and a
	// mix chunk's worth of source
	const unsigned int needed = nextPowerOfTwo(retention(aTempo) + 2 * lookahead(aTempo) + (unsigned int)std::ceil(aTempo * SAMPLE_GRANULARITY));
	if (needed <= mRingFrames)
		return;
	const unsigned int oldFrames = mRingFrames;
	mRingFrames = needed;
	if (oldFrames == 0)
	{
		mRing.init(needed * mChannels);
		return;
	}
	const AlignedFloatBuffer old(oldFrames * mChannels);
	memcpy(old.mData, mRing.mData, (size_t)oldFrames * mChannels * sizeof(float));
	mRing.init(needed * mChannels);
	for (unsigned int ch = 0; ch < mChannels; ch++)
		for (idx i = std::max(mFed - oldFrames, idx(0)); i < mFed; i++)
			ring(ch)[ringSlot(i)] = lane(old, ch, oldFrames)[(size_t)(i & (oldFrames - 1))];
}

float TimeStretcher::WsolaEngine::sourceAt(unsigned int aChannel, idx aIndex) const
{
	if (aIndex < 0)
		return 0.0f; // before the priming point
	SOLOUD_ASSERT(aIndex < mFed && aIndex + mRingFrames >= mFed);
	return mRing.mData[(size_t)aChannel * mRingFrames + ringSlot(aIndex)];
}

float TimeStretcher::WsolaEngine::monoAt(idx aIndex) const
{
	float sum = 0.0f;
	for (unsigned int ch = 0; ch < mChannels; ch++)
		sum += sourceAt(ch, aIndex);
	return sum / (float)mChannels;
}

void TimeStretcher::WsolaEngine::prime(float **aChannels, unsigned int aFrames)
{
	mFed = 0;
	mPulled = 0;
	mStart = 0;
	mNominal = 0.0;
	mFirst = true;
	mPrevSource = 0;
	mPrevLength = 0;
	mDebt = 0.0;
	mDebtFrom = 0;
	mOnsetCount = 0;
	mNextOnset = 0;
	mAnalysed = 0;
	mScale = 0.0;
	mFlux.fill(0.0);
	mLogMagnitude.clear();
	feed(aChannels, aFrames);
}

void TimeStretcher::WsolaEngine::setTempo(double aTempo)
{
	mTempo = aTempo;
	resizeRing(aTempo);
}

unsigned int TimeStretcher::WsolaEngine::inputDeficit() const
{
	// what is fed ahead of the nominal position of the next frame to be pulled, against what placing sequences from there needs
	const double ahead = (double)mFed - (mNominal - mTempo * (double)(mStart - mPulled));
	const double needed = lookahead(mTempo);
	return ahead < needed ? (unsigned int)std::ceil(needed - ahead) : 0;
}

void TimeStretcher::WsolaEngine::process(float **aInputs, unsigned int aIn, float **aOutputs, unsigned int aOut)
{
	feed(aInputs, aIn);
	while (mStart < mPulled + aOut)
		commit();
	for (unsigned int ch = 0; ch < mChannels; ch++)
	{
		const float *out = lane(mOut, ch, mOutFrames);
		for (unsigned int j = 0; j < aOut; j++)
			aOutputs[ch][j] = out[outSlot(mPulled + j)];
	}
	mPulled += aOut;
}

void TimeStretcher::WsolaEngine::feed(float **aInputs, unsigned int aFrames)
{
	SOLOUD_ASSERT(aFrames <= mRingFrames);
	for (unsigned int ch = 0; ch < mChannels; ch++)
	{
		float *dest = ring(ch);
		for (unsigned int i = 0; i < aFrames; i++)
			dest[ringSlot(mFed + i)] = aInputs[ch][i];
	}
	mFed += aFrames;
	analyse();
}

void TimeStretcher::WsolaEngine::analyse()
{
	while (mAnalysed * mAnalysisHop + mWindow <= mFed)
	{
		const idx n = mAnalysed;
		const idx from = n * mAnalysisHop;
		for (unsigned int i = 0; i < mWindow; i++)
			mFrame.mData[i] = monoAt(from + i) * mHann.mData[i];
		float *real = mSpectrum.mData;
		float *imaginary = mSpectrum.mData + mWindow / 2;
		mFft.fft(mFrame.mData, real, imaginary);
		float *current = lane(mLogMagnitude, (unsigned int)(n % 3), mBins);
		current[0] = std::log1p(LOG_GAIN * std::abs(real[0]) * 2.0f / (float)mWindow); // imaginary[0] is the Nyquist bin
		for (unsigned int k = 1; k < mBins; k++)
			current[k] = std::log1p(LOG_GAIN * std::hypot(real[k], imaginary[k]) * 2.0f / (float)mWindow);
		double flux = 0.0;
		if (n >= 2)
		{
			const float *earlier = lane(mLogMagnitude, (unsigned int)((n - 2) % 3), mBins);
			for (unsigned int k = 0; k < mBins; k++)
			{
				float reference = earlier[k];
				if (k > 0)
					reference = std::max(reference, earlier[k - 1]);
				if (k + 1 < mBins)
					reference = std::max(reference, earlier[k + 1]);
				flux += std::max(0.0f, current[k] - reference);
			}
		}
		mFlux[(size_t)(n % FLUX_FRAMES)] = flux;
		mScale = std::max(flux, mScale * mScaleDecay);
		mAnalysed++;

		// with PEAK_FRAMES frames after it in, the frame that many back can be judged
		const idx m = n - PEAK_FRAMES;
		if (m < PAST_FRAMES)
			continue;
		const double candidate = mFlux[(size_t)(m % FLUX_FRAMES)];
		bool peak = true;
		for (idx q = m - PEAK_FRAMES; q <= m + PEAK_FRAMES && peak; q++)
			peak = mFlux[(size_t)(q % FLUX_FRAMES)] <= candidate;
		if (!peak)
			continue;
		double mean = 0.0;
		for (idx q = m - PAST_FRAMES; q <= m + PEAK_FRAMES; q++)
			mean += mFlux[(size_t)(q % FLUX_FRAMES)];
		mean /= FLUX_FRAMES;
		if (candidate >= mean + THRESHOLD * mScale)
			refine(m);
	}
}

void TimeStretcher::WsolaEngine::refine(idx aFrame)
{
	// the onset is somewhere in the frame's window or the hop before it; find the steepest rise of the level there
	const idx lo = std::max(aFrame * mAnalysisHop - mAnalysisHop, idx(0));
	const idx hi = aFrame * mAnalysisHop + mWindow / 2 + mAnalysisHop;
	const unsigned int span = (unsigned int)(hi - lo);
	float *mono = mMono.mData;
	float *envelope = mEnvelope.mData;
	for (unsigned int i = 0; i < span + mSlopeFrames + mEnvelopeFrames; i++)
		mono[i] = monoAt(lo + i);
	double sum = 0.0;
	for (unsigned int i = 0; i < mEnvelopeFrames; i++)
		sum += (double)mono[i] * mono[i];
	envelope[0] = (float)std::sqrt(sum / mEnvelopeFrames);
	for (unsigned int i = 1; i <= span + mSlopeFrames; i++)
	{
		sum += (double)mono[i + mEnvelopeFrames - 1] * mono[i + mEnvelopeFrames - 1] - (double)mono[i - 1] * mono[i - 1];
		envelope[i] = (float)std::sqrt(std::max(sum, 0.0) / mEnvelopeFrames);
	}
	unsigned int best = 0;
	float bestSlope = -1.0f;
	for (unsigned int i = 0; i <= span; i++)
	{
		const float slope = envelope[i + mSlopeFrames] - envelope[i];
		if (slope > bestSlope)
		{
			bestSlope = slope;
			best = i;
		}
	}
	const float mid = 0.5f * (envelope[best] + envelope[best + mSlopeFrames]);
	unsigned int at = best;
	while (at < best + mSlopeFrames && envelope[at] < mid)
		at++;
	// envelope[at] is the level of the audio up to, and including, the sample mEnvelopeFrames past its index: a lone impulse lands exactly
	addOnset(lo + at + mEnvelopeFrames - 1);
}

void TimeStretcher::WsolaEngine::addOnset(idx aSource)
{
	idx *begin = mOnsets.data();
	idx *end = begin + mOnsetCount;
	idx *at = std::lower_bound(begin, end, aSource);
	if ((at != end && *at - aSource < mMinGap) || (at != begin && aSource - *(at - 1) < mMinGap))
		return;
	if (mOnsetCount == MAX_ONSETS)
	{
		// full: the oldest goes, or this one if it would be the oldest
		if (at == begin)
			return;
		memmove(begin, begin + 1, (size_t)(end - begin - 1) * sizeof(idx));
		at--;
		end--;
		mOnsetCount--;
		if (mNextOnset > 0)
			mNextOnset--;
	}
	memmove(at + 1, at, (size_t)(end - at) * sizeof(idx));
	*at = aSource;
	mOnsetCount++;
	if ((unsigned int)(at - begin) < mNextOnset)
		mNextOnset++; // behind the scheduler already: it plays where it falls
}

bool TimeStretcher::WsolaEngine::onsetIn(idx aFrom, idx aTo) const
{
	const idx *end = mOnsets.data() + mOnsetCount;
	const idx *at = std::lower_bound(mOnsets.data(), end, aFrom);
	return at != end && *at < aTo;
}

void TimeStretcher::WsolaEngine::commit()
{
	// onsets no search can reach any more are forgotten
	const double keepFrom = mNominal - retention(mTempo);
	while (mNextOnset > 0 && (double)mOnsets[0] < keepFrom)
	{
		memmove(mOnsets.data(), mOnsets.data() + 1, (size_t)(mOnsetCount - 1) * sizeof(idx));
		mOnsetCount--;
		mNextOnset--;
	}

	const idx start = mStart;
	auto outputTimeOf = [&](idx aSource) { return start + std::llround(((double)aSource - mNominal) / mTempo); };
	bool anchored = false;
	idx source = 0;
	idx length = 0;
	while (mNextOnset < mOnsetCount)
	{
		const idx onset = mOnsets[mNextOnset];
		const idx distance = outputTimeOf(onset) - start;
		if (distance < mOverlap)
		{
			// too close for a sequence of its own: it plays where it falls
			mNextOnset++;
			continue;
		}
		if (distance > mPreMax)
			break;
		const idx pre = distance;
		source = onset - pre;
		length = pre + mPost + mOverlap;
		if (mNextOnset + 1 < mOnsetCount)
		{
			// hand over to the next onset's own sequence where it can still get a pre-roll of at least the crossfade
			const idx next = outputTimeOf(mOnsets[mNextOnset + 1]);
			const idx lo = std::max(start + pre + mPostMin, next - mPreMax);
			const idx hi = std::min(start + pre + mPost + mHop, next - mOverlap);
			if (lo <= hi)
				length = std::clamp(next - mPreTarget, lo, hi) - start + mOverlap;
		}
		anchored = true;
		mNextOnset++;
		break;
	}
	if (!anchored)
	{
		// a normal sequence, its hop chosen so that the next sequence starts where the next onset wants its anchored one to
		idx hop = mHop;
		idx anchoredHead = -1;
		if (mNextOnset < mOnsetCount)
		{
			const idx onset = mOnsets[mNextOnset];
			const idx want = outputTimeOf(onset) - mPreTarget - start;
			if (want <= mMaxHop)
			{
				hop = std::clamp(want, mOverlap, mMaxHop);
				const idx distance = outputTimeOf(onset) - (start + hop);
				if (distance >= mOverlap && distance <= mPreMax)
					anchoredHead = onset - distance;
			}
		}
		length = hop + mOverlap;
		const double debt = mDebt * std::max(0.0, 1.0 - (double)(start - mDebtFrom) / (double)mRepay);
		source = mFirst ? 0 : search(std::llround(mNominal + debt), length, anchoredHead);
	}
	write(start, source, length, !mFirst);
	const idx advance = length - mOverlap;
	if (anchored)
	{
		mDebtFrom = start + advance;
		mDebt = (double)(source + advance) - (mNominal + mTempo * (double)advance);
	}
	mNominal += mTempo * (double)advance;
	mStart += advance;
	mPrevSource = source;
	mPrevLength = length;
	mFirst = false;
}

idx TimeStretcher::WsolaEngine::search(idx aCentre, idx aLength, idx aAnchoredHead)
{
	const unsigned int count = 2 * (unsigned int)mHalfSeek + 1;
	const idx lo = aCentre - mHalfSeek;
	const unsigned int span = count - 1 + (unsigned int)aLength;
	const unsigned int overlap = (unsigned int)mOverlap;
	const unsigned int tailOffset = (unsigned int)aLength - overlap;
	const bool twoSided = aAnchoredHead >= 0;
	for (unsigned int ch = 0; ch < mChannels; ch++)
	{
		float *window = lane(mSearch, ch, mSearchFrames);
		for (unsigned int i = 0; i < span; i++)
			window[i] = sourceAt(ch, lo + i);
		float *reference = lane(mReference, ch, overlap);
		float *reference2 = lane(mReference2, ch, overlap);
		for (unsigned int i = 0; i < overlap; i++)
		{
			reference[i] = sourceAt(ch, mPrevSource + mPrevLength - overlap + i);
			reference2[i] = twoSided ? sourceAt(ch, aAnchoredHead + i) : 0.0f;
		}
	}
	// each candidate's correlation with the previous sequence's continuation over its first overlap frames, normalised by the candidate's
	// energy there (the reference's is the same for all), plus the same between its last overlap frames and the anchored sequence's head when
	// one follows
	float *score = mScore.mData;
	float *score2 = mScore2.mData;
	float *energy = mEnergy.mData;
	float *energy2 = mEnergy2.mData;
	std::fill(score, score + count, 0.0f);
	std::fill(score2, score2 + count, 0.0f);
	std::fill(energy, energy + count, 0.0f);
	std::fill(energy2, energy2 + count, 0.0f);
	for (unsigned int ch = 0; ch < mChannels; ch++)
	{
		const float *window = lane(mSearch, ch, mSearchFrames);
		correlate(window, lane(mReference, ch, overlap), score, overlap, count);
		if (twoSided)
			correlate(window + tailOffset, lane(mReference2, ch, overlap), score2, overlap, count);
		double sum = 0.0;
		double sum2 = 0.0;
		for (unsigned int i = 0; i < overlap; i++)
		{
			sum += (double)window[i] * window[i];
			sum2 += (double)window[tailOffset + i] * window[tailOffset + i];
		}
		energy[0] += (float)sum;
		energy2[0] += (float)sum2;
		for (unsigned int j = 1; j < count; j++)
		{
			sum += (double)window[j + overlap - 1] * window[j + overlap - 1] - (double)window[j - 1] * window[j - 1];
			sum2 += (double)window[tailOffset + j + overlap - 1] * window[tailOffset + j + overlap - 1] -
			        (double)window[tailOffset + j - 1] * window[tailOffset + j - 1];
			energy[j] += (float)sum;
			energy2[j] += (float)sum2;
		}
	}
	double best = -std::numeric_limits<double>::infinity();
	double bestAny = best;
	unsigned int bestAt = (unsigned int)mHalfSeek;
	unsigned int bestAnyAt = bestAt;
	for (unsigned int j = 0; j < count; j++)
	{
		double s = score[j] / std::sqrt(energy[j] + ENERGY_EPSILON) + SCORE_BIAS;
		if (twoSided)
			s += score2[j] / std::sqrt(energy2[j] + ENERGY_EPSILON) + SCORE_BIAS;
		const double t = ((double)j - (double)mHalfSeek) / (double)mHalfSeek;
		s *= 1.0 - CENTRE_BIAS * t * t;
		if (s > bestAny)
		{
			bestAny = s;
			bestAnyAt = j;
		}
		// a candidate may not play an onset it doesn't hold (replaying one, or pre-playing the next), unless none can avoid it
		const idx candidate = lo + j;
		if (s > best && !onsetIn(candidate + overlap / 2 - mMargin, candidate + aLength - overlap / 2 + mMargin))
		{
			best = s;
			bestAt = j;
		}
	}
	return lo + (best > -std::numeric_limits<double>::infinity() ? bestAt : bestAnyAt);
}

void TimeStretcher::WsolaEngine::write(idx aStart, idx aSource, idx aLength, bool aFadeHead)
{
	const idx tail = aLength - mOverlap;
	for (unsigned int ch = 0; ch < mChannels; ch++)
	{
		float *out = lane(mOut, ch, mOutFrames);
		for (idx j = 0; j < aLength; j++)
		{
			const float v = sourceAt(ch, aSource + j);
			float &slot = out[outSlot(aStart + j)];
			if (j < mOverlap && aFadeHead)
				slot += v * mFadeIn.mData[j]; // onto the previous sequence's fading tail
			else if (j >= tail)
				slot = v * mFadeOut.mData[j - tail];
			else
				slot = v;
		}
	}
}

TimeStretcher::TimeStretcher(unsigned int aChannels, float aSamplerate)
    : mPhaseVocoder(std::make_unique<PhaseVocoderEngine>(aChannels, aSamplerate)),
      mWsola(std::make_unique<WsolaEngine>(aChannels, aSamplerate)),
      mChannels(aChannels),
      mInput(2 * READ_CHUNK * aChannels),
      mPrime(primeLength() * aChannels)
{
	// prime the phase vocoder with silence once here, so that the buffers the library sizes on its first priming are allocated off the audio
	// thread
	mPrime.clear();
	float *channels[MAX_CHANNELS];
	for (unsigned int ch = 0; ch < aChannels; ch++)
		channels[ch] = mPrime.mData + (size_t)ch * primeLength();
	mPhaseVocoder->prime(channels, primeLength());
}

TimeStretcher::~TimeStretcher() = default;

TimeStretcher::Engine *TimeStretcher::wantedEngine() const
{
	if (mPitch == 1.0f && mTempo > 1.0)
		return mWsola.get();
	return mPhaseVocoder.get();
}

bool TimeStretcher::isTimeDomain() const
{
	return mPrimed && mEngine == mWsola.get();
}

bool TimeStretcher::enginePending() const
{
	return mPrimed && mEngine != wantedEngine();
}

void TimeStretcher::setTempo(double aTempo)
{
	if (aTempo == mTempo)
		return;
	mTempo = aTempo;
	// what the engine has already made keeps the old spacing
	if (mPrimed)
		addSegment(mProduced + (double)mEngine->outputLatency(), aTempo);
	mPhaseVocoder->setTempo(aTempo);
	mWsola->setTempo(aTempo);
	// the priming buffer grows with the tempo; grow it here, off the audio thread that primes (unless a fader set the tempo)
	if (mPrime.mFloats < primeLength() * mChannels)
		mPrime.init(primeLength() * mChannels);
}

void TimeStretcher::setPitch(float aPitch)
{
	mPitch = aPitch;
	mPhaseVocoder->setPitch(aPitch);
	mWsola->setPitch(aPitch);
	if (mPrime.mFloats < primeLength() * mChannels)
		mPrime.init(primeLength() * mChannels);
}

void TimeStretcher::addSegment(double aStart, double aTempo)
{
	if (mSegmentCount > 0 && (aStart - mSegments[mSegmentCount - 1].mStart < SEGMENT_MERGE_FRAMES || mSegmentCount == MAX_SEGMENTS))
	{
		mSegments[mSegmentCount - 1].mTempo = aTempo;
		return;
	}
	mSegments[mSegmentCount++] = {.mStart = aStart, .mTempo = aTempo};
}

double TimeStretcher::outputSpan(double aFrom, double aTo) const
{
	double span = 0.0;
	for (unsigned int i = 0; i < mSegmentCount; i++)
	{
		const double begin = i == 0 ? aFrom : std::max(aFrom, mSegments[i].mStart);
		const double end = i + 1 < mSegmentCount ? std::min(aTo, mSegments[i + 1].mStart) : aTo;
		if (end > begin)
			span += (end - begin) * mSegments[i].mTempo;
	}
	return span;
}

double TimeStretcher::outputIndexOf(double aFedIndex) const
{
	// the frames in flight come out from mProduced on, spanning the segments from there
	double remaining = std::max(aFedIndex - mSpanProduced, 0.0);
	double index = mProduced;
	unsigned int i = 0;
	while (i + 1 < mSegmentCount && mSegments[i + 1].mStart <= index)
		i++;
	for (;; i++)
	{
		const double end = i + 1 < mSegmentCount ? mSegments[i + 1].mStart : std::numeric_limits<double>::infinity();
		const double length = (end - index) * mSegments[i].mTempo;
		if (remaining <= length)
			return index + remaining / mSegments[i].mTempo;
		remaining -= length;
		index = end;
	}
}

double TimeStretcher::queuedSpan(unsigned int aFill, double aFrom, double aTo) const
{
	// the last queued frame is the last one produced
	return outputSpan(mProduced - ((double)aFill - aFrom), mProduced - ((double)aFill - aTo));
}

unsigned int TimeStretcher::outputLatency() const
{
	return (mEngine ? mEngine : wantedEngine())->outputLatency();
}

unsigned int TimeStretcher::primeLength() const
{
	return wantedEngine()->primeLength(mTempo);
}

void TimeStretcher::invalidate()
{
	mPrimed = false;
	mEnded = false;
	mDrained = false;
}

// Reads aFrames source frames into mInput at stride aFrames, zero-filling past the end of a source that has ended, and accounts for them as
// fed. Returns how many of them were actual source audio.
unsigned int TimeStretcher::readInput(AudioSourceInstance *aVoice, unsigned int aFrames)
{
	unsigned int got = 0;
	if (!mEnded)
	{
		unsigned int wrapOffset = UINT_MAX;
		double wrapFrame = 0.0;
		got = Soloud::readSourceFrames_internal(aVoice, mInput.mData, aFrames, aFrames, mInput.mData + (size_t)READ_CHUNK * mChannels, READ_CHUNK * mChannels,
		                                        wrapOffset, wrapFrame);
		if (wrapOffset != UINT_MAX)
		{
			// the audio from the loop point on lands this many frames past the end of the queue (the frames produced in the current
			// produce() count as not queued yet)
			const unsigned int fedAtWrap = mFed + wrapOffset;
			const double outputIndex = std::max(outputIndexOf(fedAtWrap) - mProduced, 0.0);
			aVoice->mLoopWrapPending = true;
			aVoice->mLoopWrapIndex = aVoice->mResampleBufferFill + (unsigned int)std::lround(outputIndex);
			aVoice->mLoopWrapFrame = wrapFrame;
			mFedAtWrap = fedAtWrap;
			if (mPrimed)
				mEngine->anchor(fedAtWrap);
		}
		if (got < aFrames)
		{
			mEnded = true;
			if (mPrimed)
				mEngine->anchor(mFed + got);
		}
	}
	for (unsigned int ch = 0; ch < mChannels; ch++)
		memset(mInput.mData + (size_t)ch * aFrames + got, 0, (aFrames - got) * sizeof(float));
	mFed += aFrames;
	mRealFed += got;
	return got;
}

void TimeStretcher::prime(AudioSourceInstance *aVoice)
{
	const unsigned int length = primeLength();
	SOLOUD_ASSERT(mPrime.mFloats >= length * mChannels);

	// the raw source audio still queued for resampling (there is some when the stage engages on a playing voice) becomes the head of the
	// priming buffer, so that playback continues from where it is without the source having to seek back. the fraction of a frame the
	// resampler was into the first queued frame is dropped; when more is queued than the buffer takes, the first frames are skipped
	const unsigned int queued = aVoice->mResampleBufferFill - aVoice->mResampleBufferPos;
	const unsigned int skip = queued > length ? queued - length : 0;
	const unsigned int reuse = queued - skip;
	const unsigned int first = aVoice->mResampleBufferPos + skip;
	double startFrame = aVoice->mStreamPosition * aVoice->mBaseSamplerate - aVoice->mPreciseSrcPosition + skip;
	bool carryWrap = false;
	unsigned int carriedFedAtWrap = 0;
	if (aVoice->mLoopWrapPending)
	{
		if (aVoice->mLoopWrapIndex <= first)
			startFrame = aVoice->mLoopWrapFrame + (first - aVoice->mLoopWrapIndex);
		else
		{
			carryWrap = true;
			carriedFedAtWrap = aVoice->mLoopWrapIndex - first;
		}
	}
	for (unsigned int ch = 0; ch < mChannels; ch++)
		memcpy(mPrime.mData + (size_t)ch * length, aVoice->getResampleBuffer(ch) + first, reuse * sizeof(float));
	aVoice->clearResampleBuffer();
	aVoice->mStreamPosition = startFrame / aVoice->mBaseSamplerate;

	mCarry = 0.0;
	mFed = reuse;
	mRealFed = reuse;
	mProduced = 0;
	mSpanProduced = 0.0;
	mSegments[0] = {.mStart = 0.0, .mTempo = mTempo};
	mSegmentCount = 1;
	mPrimed = false;
	mEnded = false;
	mDrained = false;
	if (carryWrap)
	{
		mFedAtWrap = carriedFedAtWrap;
		aVoice->mLoopWrapPending = true;
		aVoice->mLoopWrapIndex = (unsigned int)std::lround(outputIndexOf(carriedFedAtWrap));
	}

	while (mFed < length)
	{
		const unsigned int at = mFed;
		const unsigned int chunk = std::min(length - at, READ_CHUNK);
		readInput(aVoice, chunk);
		for (unsigned int ch = 0; ch < mChannels; ch++)
			memcpy(mPrime.mData + (size_t)ch * length + at, mInput.mData + (size_t)ch * chunk, chunk * sizeof(float));
	}

	float *channels[MAX_CHANNELS];
	for (unsigned int ch = 0; ch < mChannels; ch++)
		channels[ch] = mPrime.mData + (size_t)ch * length;
	mEngine = wantedEngine();
	mEngine->prime(channels, length);
	// the discontinuities the priming buffer holds, announced now that the engine starts afresh
	if (aVoice->mLoopWrapPending)
		mEngine->anchor(mFedAtWrap);
	if (mEnded)
		mEngine->anchor(mRealFed);
	mPrimed = true;
}

unsigned int TimeStretcher::produce(AudioSourceInstance *aVoice, float *aBuffer, unsigned int aFrames, unsigned int aStride)
{
	if (!mPrimed)
		prime(aVoice);
	if (mDrained || aFrames == 0)
		return 0;

	const double owed = aFrames * mTempo + mCarry;
	unsigned int need = (unsigned int)owed;
	mCarry = owed - need;
	need += mEngine->inputDeficit();

	// the source frames come in over reads of at most READ_CHUNK each, with the output split in proportion, and each read's share goes through
	// process() in slices of at most PROCESS_CHUNK output frames: the phase vocoder places its analysis blocks linearly across each call's
	// input, so a call with no input or no output would bunch them up
	const unsigned int reads = std::max(1u, (need + READ_CHUNK - 1) / READ_CHUNK);
	unsigned int fedNow = 0;
	unsigned int producedNow = 0;
	float *inputs[MAX_CHANNELS];
	float *outputs[MAX_CHANNELS];
	for (unsigned int r = 1; r <= reads; r++)
	{
		const unsigned int inChunk = (unsigned int)((uint64_t)need * r / reads) - fedNow;
		const unsigned int outChunk = (unsigned int)((uint64_t)aFrames * r / reads) - producedNow;
		if (inChunk > 0)
			readInput(aVoice, inChunk);
		const unsigned int slices = std::max(1u, (outChunk + PROCESS_CHUNK - 1) / PROCESS_CHUNK);
		unsigned int inDone = 0;
		unsigned int outDone = 0;
		for (unsigned int j = 1; j <= slices; j++)
		{
			const unsigned int in = (unsigned int)((uint64_t)inChunk * j / slices) - inDone;
			const unsigned int out = (unsigned int)((uint64_t)outChunk * j / slices) - outDone;
			for (unsigned int ch = 0; ch < mChannels; ch++)
			{
				inputs[ch] = mInput.mData + (size_t)ch * inChunk + inDone;
				outputs[ch] = aBuffer + (size_t)ch * aStride + producedNow + outDone;
			}
			mEngine->process(inputs, in, outputs, out);
			inDone += in;
			outDone += out;
		}
		fedNow += inChunk;
		producedNow += outChunk;
	}

	// once the source has ended, what comes out past its last audio is the padding's
	if (mEnded)
	{
		const unsigned int realEnd = (unsigned int)std::ceil(outputIndexOf(mRealFed));
		if (mProduced + producedNow >= realEnd)
		{
			producedNow = realEnd > mProduced ? realEnd - mProduced : 0;
			mDrained = true;
		}
	}
	mSpanProduced += outputSpan(mProduced, mProduced + producedNow);
	mProduced += producedNow;
	// segments only the queue could still refer to are kept
	while (mSegmentCount > 1 && mSegments[1].mStart + AudioSourceInstance::RESAMPLE_BUFFER_SIZE < mProduced)
	{
		for (unsigned int i = 1; i < mSegmentCount; i++)
			mSegments[i - 1] = mSegments[i];
		mSegmentCount--;
	}
	return producedNow;
}
} // namespace SoLoud
