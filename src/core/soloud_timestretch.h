/*
SoLoud audio engine - per-voice time-stretch stage (internal)
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

#ifndef SOLOUD_TIMESTRETCH_H
#define SOLOUD_TIMESTRETCH_H

#include "soloud_containers.h"

#include <array>
#include <memory>
#include <random>

namespace signalsmith::stretch
{
template <typename Sample, class RandomEngine>
struct SignalsmithStretch;
}

namespace SoLoud
{
class AudioSourceInstance;

// Plays a voice's source back at mTempo with the pitch preserved (and optionally shifted by mPitch), between the loop-aware source read and the
// voice's filters, so that the filters, resampler and position tracking all see the stretched stream: every frame it queues for resampling
// spans mTempo source frames. Engaged lazily by Soloud::setTempo and setPitchShift, see there for the threading contract.
//
// Before it produces anything the stretcher is primed with primeLength() source frames, which aligns its first output frame with the first
// primed frame, so the voice's position needs no latency term. Priming happens in the audio thread's first produce() after the stage is engaged
// or invalidated by a seek, so that it runs with the tempo in force then. The source frames the stretcher holds in flight are tracked as
// mFed - mTempo * mProduced.
class TimeStretcher
{
public:
	TimeStretcher(unsigned int aChannels, float aSamplerate);
	~TimeStretcher();

	TimeStretcher(const TimeStretcher &) = delete;
	TimeStretcher &operator=(const TimeStretcher &) = delete;
	TimeStretcher(TimeStretcher &&) = delete;
	TimeStretcher &operator=(TimeStretcher &&) = delete;

	// Fill aBuffer (channel-separated at stride aStride) with aFrames stretched frames of aVoice's source, priming first when needed. Returns
	// the frames produced: fewer than asked only once the source has ended and its last audio has come out, 0 from then on.
	unsigned int produce(AudioSourceInstance *aVoice, float *aBuffer, unsigned int aFrames, unsigned int aStride);
	// Forget the audio in flight: the source was seeked, so the next produce() primes again from its new position
	void invalidate();

	// A tempo set while the stretcher is primed reaches its output outputLatency() frames later; the frames until then keep the old spacing
	void setTempo(double aTempo);
	void setPitch(float aPitch);
	[[nodiscard]] double getTempo() const { return mTempo; }
	[[nodiscard]] float getPitch() const { return mPitch; }

	// Whether the frames queued for resampling are stretched output (true) or raw source audio from before the stage engaged (false)
	[[nodiscard]] bool isPrimed() const { return mPrimed; }
	// Source frames spanned by the stretched frames between queue indices aFrom and aTo of a queue filled to aFill
	[[nodiscard]] double queuedSpan(unsigned int aFill, double aFrom, double aTo) const;
	// Source frames fed to the stretcher that haven't come out of it yet
	[[nodiscard]] double inFlightSourceFrames() const { return (double)mFed - mSpanProduced; }
	// Source frames fed since the voice's pending loop wrap was read
	[[nodiscard]] unsigned int fedSinceWrap() const { return mFed - mFedAtWrap; }
	// Whether the source has ended and its last audio has been produced
	[[nodiscard]] bool drained() const { return mDrained; }
	// Stretched frames between a ratio change and the first output frame that reflects it
	[[nodiscard]] unsigned int outputLatency() const;

private:
	using Stretch = signalsmith::stretch::SignalsmithStretch<float, std::mt19937>;

	// The tempo in force from an output frame index (counted since priming) on
	struct Segment
	{
		double mStart;
		double mTempo;
	};
	// as many as a fader changing the tempo every mix can have in flight; changes closer together than SEGMENT_MERGE_FRAMES share a segment
	static constexpr unsigned int MAX_SEGMENTS = 64;
	static constexpr double SEGMENT_MERGE_FRAMES = 128.0;

	// Source frames the stretcher is primed with at the current tempo
	[[nodiscard]] unsigned int primeLength() const;
	void prime(AudioSourceInstance *aVoice);
	unsigned int readInput(AudioSourceInstance *aVoice, unsigned int aFrames);
	void addSegment(double aStart, double aTempo);
	// Source frames spanned by the output frames aFrom to aTo
	[[nodiscard]] double outputSpan(double aFrom, double aTo) const;
	// Output frame index at which the source frame fed aFedIndex-th since priming comes out (for frames still in flight)
	[[nodiscard]] double outputIndexOf(double aFedIndex) const;

	std::unique_ptr<Stretch> mStretch;
	unsigned int mChannels;
	double mTempo;
	float mPitch;
	AlignedFloatBuffer mInput;                   // the source frames of one process() call, followed by the loop path's scratch, see readInput()
	AlignedFloatBuffer mPrime;                   // the primeLength() source frames the stretcher is primed with
	double mCarry;                               // fraction of a source frame owed to the stretcher, so that the frames fed add up to produced * tempo
	unsigned int mFed;                           // source frames fed since priming, the priming frames and the zero padding after the source ended included
	unsigned int mRealFed;                       // those that were actual source audio
	unsigned int mProduced;                      // stretched frames produced since priming
	double mSpanProduced;                        // source frames those span
	unsigned int mFedAtWrap;                     // mFed at the voice's pending loop wrap
	std::array<Segment, MAX_SEGMENTS> mSegments; // in order of mStart; the first one also covers everything before it
	unsigned int mSegmentCount;
	bool mPrimed;
	bool mEnded;   // the source ran out; what's fed from now on is padding
	bool mDrained; // and its last audio has come out
};
} // namespace SoLoud

#endif
