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

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring> // the vendored fft.h uses std::memcpy without including it
#include <limits>
#include "signalsmith-stretch/signalsmith-stretch.h"

namespace SoLoud
{
namespace
{
// spread each block's spectral work over the process() calls of an interval instead of doing it all in the call that completes the block
constexpr bool SPLIT_COMPUTATION = true;
// the library only draws random phases at extreme ratios, but a fixed seed keeps the output deterministic either way
constexpr long RANDOM_SEED = 0x5010;
// source frames per getAudio() call
constexpr unsigned int READ_CHUNK = SAMPLE_GRANULARITY;
// output frames per process() call: the library places transients within a few frames of where they belong when each call covers this
// little, and within tens of them when a call covers a whole mix chunk, at the same total cost
constexpr unsigned int PROCESS_CHUNK = 64;
} // namespace

TimeStretcher::TimeStretcher(unsigned int aChannels, float aSamplerate)
    : mStretch(std::make_unique<Stretch>(RANDOM_SEED)),
      mChannels(aChannels),
      mTempo(1.0),
      mPitch(1.0f),
      mInput(2 * READ_CHUNK * aChannels),
      mCarry(0.0),
      mFed(0),
      mRealFed(0),
      mProduced(0),
      mSpanProduced(0.0),
      mFedAtWrap(0),
      mSegments(),
      mSegmentCount(0),
      mPrimed(false),
      mEnded(false),
      mDrained(false)
{
	mStretch->configure((int)aChannels, (int)(aSamplerate * BLOCK_SECONDS), (int)(aSamplerate * INTERVAL_SECONDS), SPLIT_COMPUTATION);
	// prime with silence once here, so that the buffers the library sizes on its first priming are allocated off the audio thread
	mPrime.init(primeLength() * aChannels);
	mPrime.clear();
	float *channels[MAX_CHANNELS];
	for (unsigned int ch = 0; ch < aChannels; ch++)
		channels[ch] = mPrime.mData + ch * primeLength();
	mStretch->outputSeek(channels, (int)primeLength());
}

TimeStretcher::~TimeStretcher() = default;

void TimeStretcher::setTempo(double aTempo)
{
	if (aTempo == mTempo)
		return;
	mTempo = aTempo;
	// the blocks the library analyses from now on come out outputLatency() later; what it has already analysed keeps the old spacing
	if (mPrimed)
		addSegment(mProduced + (double)outputLatency(), aTempo);
	// the priming buffer grows with the tempo; grow it here, off the audio thread that primes (unless a fader set the tempo)
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
	mSegments[mSegmentCount++] = {aStart, aTempo};
}

double TimeStretcher::outputSpan(double aFrom, double aTo) const
{
	double span = 0.0;
	for (unsigned int i = 0; i < mSegmentCount; i++)
	{
		double begin = i == 0 ? aFrom : std::max(aFrom, mSegments[i].mStart);
		double end = i + 1 < mSegmentCount ? std::min(aTo, mSegments[i + 1].mStart) : aTo;
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
		double end = i + 1 < mSegmentCount ? mSegments[i + 1].mStart : std::numeric_limits<double>::infinity();
		double length = (end - index) * mSegments[i].mTempo;
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

void TimeStretcher::setPitch(float aPitch)
{
	mPitch = aPitch;
	mStretch->setTransposeFactor(aPitch);
}

unsigned int TimeStretcher::outputLatency() const
{
	return (unsigned int)mStretch->outputLatency();
}

unsigned int TimeStretcher::primeLength() const
{
	return (unsigned int)mStretch->outputSeekLength((float)mTempo);
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
		got = Soloud::readSourceFrames_internal(aVoice, mInput.mData, aFrames, aFrames, mInput.mData + READ_CHUNK * mChannels, READ_CHUNK * mChannels, wrapOffset,
		                                        wrapFrame);
		if (wrapOffset != UINT_MAX)
		{
			// the audio from the loop point on lands this many frames past the end of the queue (the frames produced in the current
			// produce() count as not queued yet)
			unsigned int fedAtWrap = mFed + wrapOffset;
			double outputIndex = std::max(outputIndexOf(fedAtWrap) - mProduced, 0.0);
			aVoice->mLoopWrapPending = true;
			aVoice->mLoopWrapIndex = aVoice->mResampleBufferFill + (unsigned int)std::lround(outputIndex);
			aVoice->mLoopWrapFrame = wrapFrame;
			mFedAtWrap = fedAtWrap;
		}
		if (got < aFrames)
			mEnded = true;
	}
	for (unsigned int ch = 0; ch < mChannels; ch++)
		memset(mInput.mData + ch * aFrames + got, 0, (aFrames - got) * sizeof(float));
	mFed += aFrames;
	mRealFed += got;
	return got;
}

void TimeStretcher::prime(AudioSourceInstance *aVoice)
{
	unsigned int length = primeLength();
	SOLOUD_ASSERT(mPrime.mFloats >= length * mChannels);

	// the raw source audio still queued for resampling (there is some when the stage engages on a playing voice) becomes the head of the
	// priming buffer, so that playback continues from where it is without the source having to seek back. the fraction of a frame the
	// resampler was into the first queued frame is dropped; when more is queued than the buffer takes, the first frames are skipped
	unsigned int queued = aVoice->mResampleBufferFill - aVoice->mResampleBufferPos;
	unsigned int skip = queued > length ? queued - length : 0;
	unsigned int reuse = queued - skip;
	unsigned int first = aVoice->mResampleBufferPos + skip;
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
		memcpy(mPrime.mData + ch * length, aVoice->getResampleBuffer(ch) + first, reuse * sizeof(float));
	aVoice->clearResampleBuffer();
	aVoice->mStreamPosition = startFrame / aVoice->mBaseSamplerate;

	mCarry = 0.0;
	mFed = reuse;
	mRealFed = reuse;
	mProduced = 0;
	mSpanProduced = 0.0;
	mSegments[0] = {0.0, mTempo};
	mSegmentCount = 1;
	mPrimed = true;
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
		unsigned int at = mFed;
		unsigned int chunk = std::min(length - at, READ_CHUNK);
		readInput(aVoice, chunk);
		for (unsigned int ch = 0; ch < mChannels; ch++)
			memcpy(mPrime.mData + ch * length + at, mInput.mData + ch * chunk, chunk * sizeof(float));
	}

	float *channels[MAX_CHANNELS];
	for (unsigned int ch = 0; ch < mChannels; ch++)
		channels[ch] = mPrime.mData + ch * length;
	mStretch->outputSeek(channels, (int)length);
}

unsigned int TimeStretcher::produce(AudioSourceInstance *aVoice, float *aBuffer, unsigned int aFrames, unsigned int aStride)
{
	if (!mPrimed)
		prime(aVoice);
	if (mDrained || aFrames == 0)
		return 0;

	double owed = aFrames * mTempo + mCarry;
	unsigned int need = (unsigned int)owed;
	mCarry = owed - need;

	// the source frames come in over reads of at most READ_CHUNK each, with the output split in proportion, and each read's share goes through
	// process() in slices of at most PROCESS_CHUNK output frames: the library places its analysis blocks linearly across each call's input,
	// so a call with no input or no output would bunch them up
	unsigned int reads = std::max(1u, (need + READ_CHUNK - 1) / READ_CHUNK);
	unsigned int fedNow = 0;
	unsigned int producedNow = 0;
	float *inputs[MAX_CHANNELS];
	float *outputs[MAX_CHANNELS];
	for (unsigned int r = 1; r <= reads; r++)
	{
		unsigned int inChunk = (unsigned int)((unsigned long long)need * r / reads) - fedNow;
		unsigned int outChunk = (unsigned int)((unsigned long long)aFrames * r / reads) - producedNow;
		if (inChunk > 0)
			readInput(aVoice, inChunk);
		unsigned int slices = std::max(1u, (outChunk + PROCESS_CHUNK - 1) / PROCESS_CHUNK);
		unsigned int inDone = 0;
		unsigned int outDone = 0;
		for (unsigned int j = 1; j <= slices; j++)
		{
			unsigned int in = (unsigned int)((unsigned long long)inChunk * j / slices) - inDone;
			unsigned int out = (unsigned int)((unsigned long long)outChunk * j / slices) - outDone;
			for (unsigned int ch = 0; ch < mChannels; ch++)
			{
				inputs[ch] = mInput.mData + ch * inChunk + inDone;
				outputs[ch] = aBuffer + ch * aStride + producedNow + outDone;
			}
			mStretch->process(inputs, (int)in, outputs, (int)out);
			inDone += in;
			outDone += out;
		}
		fedNow += inChunk;
		producedNow += outChunk;
	}

	// once the source has ended, what comes out past its last audio is the padding's
	if (mEnded)
	{
		unsigned int realEnd = (unsigned int)std::ceil(outputIndexOf(mRealFed));
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
