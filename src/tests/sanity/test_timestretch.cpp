#include "sanity.h"

#include "soloud_wavstream.h"

#include <cstring> // the vendored fft.h uses std::memcpy without including it
#include "signalsmith-stretch/signalsmith-stretch.h"

#include <algorithm>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

namespace
{
constexpr unsigned int RATE = 48000;
constexpr unsigned int BUF = 512;
// an impulse train is what survives a phase vocoder with its timing intact: a single-sample click comes out as a short burst whose peak sits
// within a few samples of where the click belongs, so it decodes the source position of the audio that is actually playing
constexpr unsigned int IMPULSE_PERIOD = RATE / 2;
constexpr unsigned int IMPULSE_OFFSET = RATE / 8;
constexpr unsigned int IMPULSE_FRAMES = RATE * 12;
constexpr float IMPULSE_THRESHOLD = 0.3f;
// in source frames: the library places a transient within a few output samples once it has settled, and within tens of them in the first
// quarter second after it is primed (at play, and at every seek)
constexpr double ALIGN_TOLERANCE = 24.0;
constexpr double SETTLING_TOLERANCE = 64.0;
constexpr size_t SETTLING_FRAMES = RATE / 4;
constexpr double TEMPOS[] = {0.5, 0.75, 1.25, 1.5, 2.0};

double impulseFrame(int aIndex)
{
	return (double)aIndex * IMPULSE_PERIOD + IMPULSE_OFFSET;
}

// float WAV file in memory (interleaved when aChannels > 1), for the codec seek path
std::vector<unsigned char> makeWavFile(const std::vector<float> &aSamples, unsigned short aChannels = 1)
{
	std::vector<unsigned char> file(44 + aSamples.size() * sizeof(float));
	auto put32 = [&](size_t aAt, unsigned int aValue) { memcpy(&file[aAt], &aValue, 4); };
	auto put16 = [&](size_t aAt, unsigned short aValue) { memcpy(&file[aAt], &aValue, 2); };
	memcpy(&file[0], "RIFF", 4);
	put32(4, (unsigned int)file.size() - 8);
	memcpy(&file[8], "WAVEfmt ", 8);
	put32(16, 16);
	put16(20, 3); // IEEE float
	put16(22, aChannels);
	put32(24, RATE);
	put32(28, RATE * sizeof(float) * aChannels);
	put16(32, sizeof(float) * aChannels);
	put16(34, 32);
	memcpy(&file[36], "data", 4);
	put32(40, (unsigned int)(aSamples.size() * sizeof(float)));
	memcpy(&file[44], aSamples.data(), aSamples.size() * sizeof(float));
	return file;
}

// the left channel of a stretch of playback, with the source frame the engine reported at the start of each buffer
struct Capture
{
	std::vector<float> samples;
	std::vector<double> positions;

	[[nodiscard]] double sourceFrame(size_t aSample, double aTempo) const { return positions[aSample / BUF] + (double)(aSample % BUF) * aTempo; }
};

Capture capture(SoLoud::Soloud &aSoloud, SoLoud::handle aHandle, unsigned int aBuffers)
{
	Capture c;
	float out[BUF * 2];
	for (unsigned int b = 0; b < aBuffers; b++)
	{
		c.positions.push_back(aSoloud.getStreamPosition(aHandle) * RATE);
		aSoloud.mix(out, BUF);
		for (unsigned int i = 0; i < BUF; i++)
			c.samples.push_back(out[2 * i]);
	}
	return c;
}

struct ImpulseStats
{
	double maxError = 0.0;         // of impulses past the settling window
	double maxSettlingError = 0.0; // of those in it
	int impulses = 0;
	int wraps = 0;    // loop wraps the reported positions showed
	int spurious = 0; // samples above the threshold where no impulse belongs
};

// Every impulse the capture should contain (by what the engine reported was playing) has to be there, with its peak where the report says,
// and nothing else may come out. The mapping from output to source frames restarts at every loop wrap the positions show, skipping the
// buffer the wrap falls in, and the same impulse can come round more than once.
void checkImpulses(const Capture &aCapture, double aTempo, ImpulseStats &aStats)
{
	const double margin = 2000.0; // source frames around an impulse its peak is looked for in
	const size_t n = aCapture.samples.size();
	std::vector<double> source(n, -1.0); // -1: not mapped
	for (size_t b = 0; b + 1 < aCapture.positions.size(); b++)
	{
		double next = aCapture.positions[b] + BUF * aTempo;
		if (aCapture.positions[b + 1] < aCapture.positions[b])
			aStats.wraps++;
		else if (std::abs(aCapture.positions[b + 1] - next) < 1.0)
			for (size_t i = 0; i < BUF; i++)
				source[b * BUF + i] = aCapture.positions[b] + (double)i * aTempo;
	}
	// a full window around an impulse, in output samples; shorter stretches are cut off by an unmapped buffer or the capture's end
	const size_t fullWindow = (size_t)(2.0 * margin / aTempo) - 2;
	size_t k = 0;
	while (k < n)
	{
		if (source[k] < 0)
		{
			k++;
			continue;
		}
		int m = (int)std::lround((source[k] - IMPULSE_OFFSET) / IMPULSE_PERIOD);
		if (m < 0 || std::abs(source[k] - impulseFrame(m)) >= margin)
		{
			if (std::abs(aCapture.samples[k]) >= IMPULSE_THRESHOLD)
				aStats.spurious++;
			k++;
			continue;
		}
		size_t begin = k;
		size_t peak = k;
		float peakValue = 0.0f;
		while (k < n && source[k] >= 0 && std::abs(source[k] - impulseFrame(m)) < margin)
		{
			if (std::abs(aCapture.samples[k]) > peakValue)
			{
				peakValue = std::abs(aCapture.samples[k]);
				peak = k;
			}
			k++;
		}
		if (k - begin < fullWindow)
			continue;
		gTests++;
		if (peakValue < IMPULSE_THRESHOLD)
		{
			gErrorCount++;
			SoLoud::logStdout(__FILE__ ":" STRINGIZE_MACRO(__LINE__) ":%s: impulse at source frame %.0f missing at tempo %.2f (peak %.3f)\n", PFUNC, impulseFrame(m),
			                  aTempo, peakValue);
			continue;
		}
		aStats.impulses++;
		double error = source[peak] - impulseFrame(m);
		bool settling = peak < SETTLING_FRAMES;
		if (gVerbose > 1)
			SoLoud::logStdout("    impulse %d: %+.1f source frames (peak %.2f)%s\n", m, error, peakValue, settling ? " settling" : "");
		double &worst = settling ? aStats.maxSettlingError : aStats.maxError;
		worst = std::max(worst, std::abs(error));
	}
}

float sineSample(double aFrame)
{
	return (float)(0.5 * std::sin(2.0 * M_PI * 440.0 * aFrame / RATE));
}

// the impulse train, or a sine, from a source with only the generic tape seek, which can't go backwards
class GenericSourceInstance : public SoLoud::AudioSourceInstance
{
public:
	bool mSine;
	unsigned int mOffset = 0;
	explicit GenericSourceInstance(bool aSine)
	    : mSine(aSine)
	{
	}
	unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int /*aBufferSize*/) override
	{
		unsigned int n = std::min(aSamplesToRead, IMPULSE_FRAMES - mOffset);
		for (unsigned int i = 0; i < n; i++)
			aBuffer[i] = mSine ? sineSample(mOffset + i) : (((mOffset + i) % IMPULSE_PERIOD == IMPULSE_OFFSET) ? 1.0f : 0.0f);
		mOffset += n;
		return n;
	}
	bool hasEnded() override { return mOffset >= IMPULSE_FRAMES; }
};

class GenericSource : public SoLoud::AudioSource
{
public:
	bool mSine;
	explicit GenericSource(bool aSine)
	    : mSine(aSine)
	{
		mBaseSamplerate = (float)RATE;
		mChannels = 1;
	}
	SoLoud::AudioSourceInstance *createInstance() override { return new GenericSourceInstance(mSine); }
};

SoLoud::handle start(SoLoud::Soloud &aSoloud, SoLoud::AudioSource &aSource, double aTempo, double aPitch)
{
	// start paused so pan and volume snap into place instead of ramping over the first buffer
	SoLoud::handle h = aSoloud.play(aSource, 1.0f, 0.0f, true);
	aSoloud.setPanAbsolute(h, 1.0f, 1.0f);
	aSoloud.setVolume(h, 1.0f);
	if (aTempo != 1.0)
		aSoloud.setTempo(h, (float)aTempo);
	if (aPitch != 1.0)
		aSoloud.setPitchShift(h, (float)aPitch);
	aSoloud.setPause(h, false);
	return h;
}

// The vendored stretcher, primed with outputSeek(), passes audio through untouched at unity ratio
void checkLibraryUnity()
{
	constexpr unsigned int CHANNELS = 2;
	constexpr unsigned int FRAMES = RATE * 2;
	signalsmith::stretch::SignalsmithStretch<float, std::mt19937> stretch(1);
	stretch.configure(CHANNELS, (int)(RATE * 0.12), (int)(RATE * 0.03), true);

	std::vector<float> input(CHANNELS * FRAMES);
	std::vector<float> output(CHANNELS * FRAMES, 0.0f);
	for (unsigned int c = 0; c < CHANNELS; c++)
		for (unsigned int i = 0; i < FRAMES; i++)
			input[c * FRAMES + i] = 0.5f * (float)std::sin(2.0 * M_PI * 440.0 * (c + 1) * i / RATE);

	long startTime = getmsec();
	// after priming, the next output frame is the first frame of the priming buffer
	unsigned int primeLength = (unsigned int)stretch.outputSeekLength(1.0f);
	float *primeInput[CHANNELS];
	for (unsigned int c = 0; c < CHANNELS; c++)
		primeInput[c] = input.data() + c * FRAMES;
	stretch.outputSeek(primeInput, (int)primeLength);

	unsigned int fed = primeLength;
	unsigned int produced = 0;
	while (fed < FRAMES)
	{
		unsigned int n = std::min(BUF, FRAMES - fed);
		float *in[CHANNELS];
		float *out[CHANNELS];
		for (unsigned int c = 0; c < CHANNELS; c++)
		{
			in[c] = input.data() + c * FRAMES + fed;
			out[c] = output.data() + c * FRAMES + produced;
		}
		stretch.process(in, (int)n, out, (int)n);
		fed += n;
		produced += n;
	}
	long elapsed = getmsec() - startTime;

	// output frame k is input frame k, except for the pre-roll outputSeek() synthesises for the first outputLatency() frames
	unsigned int skip = (unsigned int)stretch.outputLatency();
	double signal = 0;
	double error = 0;
	for (unsigned int c = 0; c < CHANNELS; c++)
	{
		for (unsigned int i = skip; i < produced; i++)
		{
			double s = input[c * FRAMES + i];
			double d = output[c * FRAMES + i] - s;
			signal += s * s;
			error += d * d;
		}
	}
	double snr = 10.0 * std::log10(signal / std::max(error, 1e-30));
	PRINTINFO("Unity pass-through: %.1f dB SNR over %u frames after a %u frame pre-roll, %ld ms for %u frames\n", snr, produced - skip, skip, elapsed, produced);
	CHECK(snr > 100.0);
}

// A voice plays for its length divided by the tempo
void checkDuration(SoLoud::Soloud &aSoloud)
{
	const unsigned int sourceRates[] = {44100, 48000};
	const unsigned int sourceSeconds = 20;
	// one mix chunk of slack, the quantisation of the voice's end
	const double toleranceSec = (double)BUF / RATE;
	float out[BUF * 2];
	for (unsigned int sourceRate : sourceRates)
	{
		SoLoud::Wav wav;
		generateTimingTestWave(wav, sourceRate * sourceSeconds, sourceRate);
		for (double tempo : TEMPOS)
		{
			SoLoud::handle h = start(aSoloud, wav, tempo, 1.0);
			CHECK(aSoloud.getTempo(h) == (float)tempo);
			unsigned int mixed = 0;
			for (unsigned int iter = 0; iter < 1000000 && aSoloud.isValidVoiceHandle(h); iter++)
			{
				memset(out, 0, sizeof(out));
				aSoloud.mix(out, BUF);
				if (aSoloud.isValidVoiceHandle(h))
				{
					mixed += BUF;
					continue;
				}
				// the voice stopped during this mix: count up to the last non-zero sample
				for (unsigned int i = 0; i < BUF; i++)
					if (out[2 * i] != 0.0f || out[2 * i + 1] != 0.0f)
						mixed = mixed - (mixed % BUF) + i + 1; // NOLINT(clang-analyzer-deadcode.DeadStores)
			}
			double expected = (double)sourceSeconds / tempo;
			double actual = (double)mixed / RATE;
			PRINTINFO("Duration: %u Hz source at tempo %.2f: expected %.4fs, actual %.4fs (%.1f ms off)\n", sourceRate, tempo, expected, actual,
			          (actual - expected) * 1000.0);
			CHECK(std::abs(actual - expected) <= toleranceSec);
			aSoloud.stopAll();
		}
	}
}

// A stage dropped before it primed leaves the voice untouched, and a stage kept at unity (on a source that can't seek back) is transparent
// and keeps the position exact through the change
void checkTransparency(SoLoud::Soloud &aSoloud)
{
	SoLoud::Wav wav;
	generateTimingTestWave(wav, RATE * 3, RATE);
	const unsigned int buffers = 2 * RATE / BUF;
	SoLoud::handle h = start(aSoloud, wav, 1.0, 1.0);
	Capture raw = capture(aSoloud, h, buffers);
	aSoloud.stopAll();
	// engaged before playing, and back to unity before the first mix: dropped again
	h = aSoloud.play(wav, 1.0f, 0.0f, true);
	aSoloud.setPanAbsolute(h, 1.0f, 1.0f);
	aSoloud.setVolume(h, 1.0f);
	aSoloud.setTempo(h, 1.5f);
	aSoloud.setTempo(h, 1.0f);
	CHECK(aSoloud.getTempo(h) == 1.0f);
	aSoloud.setPause(h, false);
	Capture engaged = capture(aSoloud, h, buffers);
	aSoloud.stopAll();

	// the first 90 ms come from the priming pre-roll, which only approximates the source
	const size_t settled = RATE / 5;
	float maxDiff = 0.0f;
	float preRollDiff = 0.0f;
	for (size_t k = 0; k < raw.samples.size(); k++)
	{
		float d = std::abs(raw.samples[k] - engaged.samples[k]);
		if (k < settled)
			preRollDiff = std::max(preRollDiff, d);
		else
			maxDiff = std::max(maxDiff, d);
	}
	PRINTINFO("Engaged and dropped before playing: max difference %.2e after the pre-roll (%.2e during it)\n", maxDiff, preRollDiff);
	CHECK(maxDiff < 1e-4f);
	for (size_t b = 0; b < raw.positions.size(); b++)
	{
		CHECK(std::abs(raw.positions[b] - engaged.positions[b]) < 1.0);
	}

	// stretched, then back to unity on a source that can't seek back: the stage stays, and once the change has reached the output, what
	// comes out is the source again, at its level and pitch (the phase vocoder keeps the phases coherent, not where the source had them)
	GenericSource sine(true);
	h = start(aSoloud, sine, 1.5, 1.0);
	capture(aSoloud, h, 100);
	aSoloud.setTempo(h, 1.0f);
	CHECK(aSoloud.getTempo(h) == 1.0f);
	capture(aSoloud, h, 50);
	Capture unity = capture(aSoloud, h, 100);
	aSoloud.stopAll();
	int crossings = 0;
	double energy = 0.0;
	for (size_t k = 1; k < unity.samples.size(); k++)
	{
		if ((unity.samples[k - 1] < 0.0f) != (unity.samples[k] < 0.0f))
			crossings++;
		energy += (double)unity.samples[k] * unity.samples[k];
	}
	double frequency = crossings / 2.0 / ((double)unity.samples.size() / RATE);
	double rms = std::sqrt(energy / (double)unity.samples.size());
	PRINTINFO("Kept at unity after a live change: %.1f Hz at %.3f RMS (source: 440 Hz at %.3f)\n", frequency, rms, 0.5 / std::sqrt(2.0));
	CHECK(std::abs(frequency - 440.0) < 4.4);
	CHECK(std::abs(rms - 0.5 / std::sqrt(2.0)) < 0.02);
}

// The pitch shift changes the frequency and not the length, the tempo changes the length and not the frequency
void checkPitch(SoLoud::Soloud &aSoloud)
{
	SoLoud::Wav wav;
	generateTimingTestWave(wav, RATE * 4, RATE); // 440 Hz
	struct Case
	{
		double tempo;
		double pitch;
		double frequency;
	};
	const Case cases[] = {
	    {1.0,  2.0, 880.0},
        {1.5,  1.0, 440.0},
        {0.75, 0.5, 220.0},
        {1.5,  2.0, 880.0}
    };
	for (const Case &c : cases)
	{
		SoLoud::handle h = start(aSoloud, wav, c.tempo, c.pitch);
		CHECK(aSoloud.getPitchShift(h) == (float)c.pitch);
		Capture cap = capture(aSoloud, h, 2 * RATE / BUF);
		aSoloud.stopAll();
		// zero crossings over the second second
		int crossings = 0;
		for (size_t k = RATE + 1; k < cap.samples.size(); k++)
			if ((cap.samples[k - 1] < 0.0f) != (cap.samples[k] < 0.0f))
				crossings++;
		double frequency = crossings / 2.0;
		PRINTINFO("Pitch: tempo %.2f, pitch shift %.2f: %.1f Hz (expected %.0f)\n", c.tempo, c.pitch, frequency, c.frequency);
		CHECK(std::abs(frequency - c.frequency) < c.frequency * 0.03);
	}
}

// After play and after every kind of seek, the audio that comes out is what the reported position says it is
void checkAlignment(SoLoud::Soloud &aSoloud, SoLoud::AudioSource &aSource, double aTempo, const char *aName)
{
	ImpulseStats stats;
	SoLoud::handle h = start(aSoloud, aSource, aTempo, 1.0);
	checkImpulses(capture(aSoloud, h, 200), aTempo, stats);

	auto checkSeek = [&](double aSeconds, bool aPaused) {
		if (aPaused)
			aSoloud.setPause(h, true);
		CHECK(aSoloud.seek(h, aSeconds) == SoLoud::SO_NO_ERROR);
		if (aPaused)
			aSoloud.setPause(h, false);
		CHECK(std::abs(aSoloud.getStreamPosition(h) - aSeconds) * RATE < 1.0);
		checkImpulses(capture(aSoloud, h, 200), aTempo, stats);
	};
	checkSeek(2.4, false);
	checkSeek(0.7, false);
	checkSeek(1.3, true);
	// a small forward seek, into the audio the stretcher already holds
	checkSeek(aSoloud.getStreamPosition(h) + 0.002, false);
	aSoloud.stopAll();
	PRINTINFO("Alignment: %s at tempo %.2f: %d impulses, worst %.1f source frames off (%.1f while settling), %d spurious\n", aName, aTempo, stats.impulses,
	          stats.maxError, stats.maxSettlingError, stats.spurious);
	CHECK(stats.impulses >= 8);
	CHECK(stats.maxError <= ALIGN_TOLERANCE);
	CHECK(stats.maxSettlingError <= SETTLING_TOLERANCE);
	CHECK(stats.spurious == 0);
}

// Engaging on a playing voice carries on from the audio already queued, without a gap or a jump in the reported position
void checkEngageLive(SoLoud::Soloud &aSoloud, SoLoud::AudioSource &aSource)
{
	ImpulseStats raw;
	SoLoud::handle h = start(aSoloud, aSource, 1.0, 1.0);
	checkImpulses(capture(aSoloud, h, 60), 1.0, raw);
	CHECK(raw.maxError < 1.0 && raw.maxSettlingError < 1.0 && raw.spurious == 0);
	double before = aSoloud.getStreamPosition(h);
	aSoloud.setTempo(h, 1.5f);
	CHECK(std::abs(aSoloud.getStreamPosition(h) - before) * RATE < 1.0);
	Capture cap = capture(aSoloud, h, 200);
	// the stage takes over at the start of the next mix, dropping less than a frame
	CHECK(std::abs(cap.positions[0] - before * RATE) < 1.0);
	CHECK(std::abs(cap.positions[1] - (cap.positions[0] + BUF * 1.5)) < 1.0);
	ImpulseStats stats;
	checkImpulses(cap, 1.5, stats);
	aSoloud.stopAll();
	PRINTINFO("Engage on a live voice: %d impulses, worst %.1f source frames off (%.1f while settling), %d spurious\n", stats.impulses, stats.maxError,
	          stats.maxSettlingError, stats.spurious);
	CHECK(stats.impulses >= 5);
	CHECK(stats.maxError <= ALIGN_TOLERANCE);
	CHECK(stats.maxSettlingError <= SETTLING_TOLERANCE);
	CHECK(stats.spurious == 0);
}

// Back at unity tempo and pitch the stage is dropped, the source seeked back to the play position; a source that can't seek backwards keeps
// its stage at unity instead
void checkDisengage(SoLoud::Soloud &aSoloud, SoLoud::AudioSource &aSource, bool aCanSeekBack, const char *aName)
{
	ImpulseStats stretched;
	SoLoud::handle h = start(aSoloud, aSource, 1.5, 1.0);
	checkImpulses(capture(aSoloud, h, 100), 1.5, stretched);
	double before = aSoloud.getStreamPosition(h);
	aSoloud.setTempo(h, 1.0f);
	CHECK(aSoloud.getTempo(h) == 1.0f);
	CHECK(std::abs(aSoloud.getStreamPosition(h) - before) * RATE < 1.0);
	Capture cap = capture(aSoloud, h, 200);
	CHECK(std::abs(cap.positions[0] - before * RATE) < 1.0);
	// dropped, the voice plays on raw at once; kept, it plays the old spacing out first
	double firstStep = cap.positions[1] - cap.positions[0];
	CHECK(aCanSeekBack ? std::abs(firstStep - BUF) < 1.0 : (firstStep >= BUF - 1.0 && firstStep <= BUF * 1.5 + 1.0));
	ImpulseStats raw;
	checkImpulses(cap, 1.0, raw);
	aSoloud.stopAll();
	PRINTINFO("Disengage on %s: %d impulses, worst %.1f source frames off (%.1f while settling), %d spurious\n", aName, raw.impulses, raw.maxError,
	          raw.maxSettlingError, raw.spurious);
	CHECK(raw.impulses >= 4);
	CHECK(raw.spurious == 0);
	if (aCanSeekBack)
	{
		// the plain path is exact
		CHECK(raw.maxError < 1.0 && raw.maxSettlingError < 1.0);
	}
	else
	{
		CHECK(raw.maxError <= ALIGN_TOLERANCE);
	}
}

// Looping: the position follows the wrap when it plays, and the audio around a wrap is the loop's, nothing else
void checkLoop(SoLoud::Soloud &aSoloud, SoLoud::AudioSource &aSource, double aTempo, const char *aName)
{
	aSource.setLooping(true);
	aSource.setLoopPoint(0.25);
	SoLoud::handle h = start(aSoloud, aSource, aTempo, 1.0);
	// 7 seconds of source: through the end twice
	Capture cap = capture(aSoloud, h, (unsigned int)(7.0 * RATE / aTempo / BUF));
	aSoloud.stopAll();
	aSource.setLooping(false);
	ImpulseStats stats;
	checkImpulses(cap, aTempo, stats);
	PRINTINFO("Loop: %s at tempo %.2f: %d wraps, %d impulses, worst %.1f source frames off (%.1f while settling), %d spurious\n", aName, aTempo, stats.wraps,
	          stats.impulses, stats.maxError, stats.maxSettlingError, stats.spurious);
	CHECK(stats.wraps == 2);
	CHECK(stats.impulses >= 12);
	CHECK(stats.maxError <= ALIGN_TOLERANCE);
	CHECK(stats.maxSettlingError <= SETTLING_TOLERANCE);
	CHECK(stats.spurious == 0);
}

// A voice that is inaudible but must tick keeps its place while muted
void checkInaudibleTick(SoLoud::Soloud &aSoloud, SoLoud::AudioSource &aSource)
{
	SoLoud::handle h = start(aSoloud, aSource, 1.5, 1.0);
	aSoloud.setInaudibleBehavior(h, true, false);
	ImpulseStats before;
	checkImpulses(capture(aSoloud, h, 100), 1.5, before);
	// only the 3d update flags voices inaudible, so flag it by hand
	SoLoud::AudioSourceInstance *voice = aSoloud.mVoice[aSoloud.getVoiceFromHandle_internal(h)];
	voice->mFlags |= SoLoud::AudioSourceInstance::INAUDIBLE;
	double muted = aSoloud.getStreamPosition(h);
	Capture silence = capture(aSoloud, h, 100);
	CHECK(std::abs(aSoloud.getStreamPosition(h) - (muted + 100.0 * BUF * 1.5 / RATE)) * RATE < 1.0);
	CHECK(*std::max_element(silence.samples.begin(), silence.samples.end()) == 0.0f);
	voice->mFlags &= ~SoLoud::AudioSourceInstance::INAUDIBLE;
	ImpulseStats after;
	checkImpulses(capture(aSoloud, h, 200), 1.5, after);
	aSoloud.stopAll();
	PRINTINFO("Inaudible tick: %d impulses after, worst %.1f source frames off, %d spurious\n", after.impulses, after.maxError, after.spurious);
	CHECK(after.impulses >= 5);
	CHECK(after.maxError <= ALIGN_TOLERANCE && after.maxSettlingError <= SETTLING_TOLERANCE);
	CHECK(after.spurious == 0);
}

// A tempo change on a playing voice keeps the reported position exact: the audio the stretcher had analysed keeps its old spacing, and the
// position follows that
void checkLiveChange(SoLoud::Soloud &aSoloud, SoLoud::AudioSource &aSource, const char *aName)
{
	SoLoud::handle h = start(aSoloud, aSource, 1.25, 1.0);
	capture(aSoloud, h, 100);
	ImpulseStats stats;
	for (double tempo : {1.5, 0.75, 2.0})
	{
		aSoloud.setTempo(h, (float)tempo);
		Capture cap = capture(aSoloud, h, 200);
		// the buffers until the change has reached the output don't span the new tempo's worth and are left out
		checkImpulses(cap, tempo, stats);
		for (size_t b = 0; b + 1 < cap.positions.size(); b++)
		{
			double step = cap.positions[b + 1] - cap.positions[b];
			CHECK(step > 0.0 && step <= BUF * 2.0 + 1.0);
		}
	}
	aSoloud.stopAll();
	PRINTINFO("Live tempo changes on %s: %d impulses, worst %.1f source frames off (%.1f while settling), %d spurious\n", aName, stats.impulses, stats.maxError,
	          stats.maxSettlingError, stats.spurious);
	CHECK(stats.impulses >= 8);
	CHECK(stats.maxError <= ALIGN_TOLERANCE);
	CHECK(stats.maxSettlingError <= SETTLING_TOLERANCE);
	CHECK(stats.spurious == 0);
}

// Tempo faders: the position stays exact through a fade, a fade to unity drops the stage, and an oscillation stays in its range
void checkFade(SoLoud::Soloud &aSoloud, SoLoud::AudioSource &aSource)
{
	SoLoud::handle h = start(aSoloud, aSource, 1.5, 1.0);
	capture(aSoloud, h, 50);
	aSoloud.fadeTempo(h, 0.75f, 1.0);
	Capture fading = capture(aSoloud, h, 250);
	CHECK(aSoloud.getTempo(h) == 0.75f);
	ImpulseStats stats;
	checkImpulses(fading, 0.75, stats);
	for (size_t b = 0; b + 1 < fading.positions.size(); b++)
	{
		double step = fading.positions[b + 1] - fading.positions[b];
		CHECK(step >= BUF * 0.75 - 1.0 && step <= BUF * 1.5 + 1.0);
	}
	aSoloud.fadeTempo(h, 1.0f, 0.5);
	Capture back = capture(aSoloud, h, 150);
	CHECK(aSoloud.getTempo(h) == 1.0f);
	ImpulseStats raw;
	checkImpulses(back, 1.0, raw);
	aSoloud.oscillateTempo(h, 0.9f, 1.1f, 0.5);
	float low = 10.0f;
	float high = 0.0f;
	for (int b = 0; b < 100; b++)
	{
		capture(aSoloud, h, 1);
		low = std::min(low, aSoloud.getTempo(h));
		high = std::max(high, aSoloud.getTempo(h));
	}
	CHECK(low >= 0.9f - 1e-4f && high <= 1.1f + 1e-4f && high > low);
	aSoloud.setTempo(h, 1.0f);
	CHECK(aSoloud.getTempo(h) == 1.0f);
	aSoloud.stopAll();
	PRINTINFO("Fade: %d impulses at the fade's end, worst %.1f source frames off, %d spurious; %d after fading back, worst %.1f\n", stats.impulses, stats.maxError,
	          stats.spurious, raw.impulses, raw.maxError);
	CHECK(stats.impulses >= 3);
	CHECK(stats.maxError <= ALIGN_TOLERANCE && stats.maxSettlingError <= SETTLING_TOLERANCE);
	CHECK(stats.spurious == 0);
	CHECK(raw.impulses >= 2);
	CHECK(raw.maxError <= ALIGN_TOLERANCE && raw.maxSettlingError <= SETTLING_TOLERANCE);
	CHECK(raw.spurious == 0);
}

// A 5.1 source goes through the stage like any other
void checkMultichannel(SoLoud::Soloud &aSoloud, const std::vector<float> &aImpulses)
{
	const unsigned short channels = 6;
	std::vector<float> interleaved(aImpulses.size() * channels);
	for (size_t i = 0; i < aImpulses.size(); i++)
		for (unsigned short c = 0; c < channels; c++)
			interleaved[i * channels + c] = aImpulses[i];
	std::vector<unsigned char> file = makeWavFile(interleaved, channels);
	SoLoud::WavStream stream;
	CHECK(stream.loadMem(file.data(), (unsigned int)file.size(), false, false) == SoLoud::SO_NO_ERROR);
	SoLoud::handle h = start(aSoloud, stream, 1.5, 1.0);
	ImpulseStats stats;
	checkImpulses(capture(aSoloud, h, 200), 1.5, stats);
	aSoloud.stopAll();
	PRINTINFO("Six channels: %d impulses, worst %.1f source frames off (%.1f while settling), %d spurious\n", stats.impulses, stats.maxError, stats.maxSettlingError,
	          stats.spurious);
	CHECK(stats.impulses >= 3);
	CHECK(stats.maxError <= ALIGN_TOLERANCE && stats.maxSettlingError <= SETTLING_TOLERANCE);
	CHECK(stats.spurious == 0);
}

// With auto-stop off a voice stays at its end once the stage has drained, and stops as soon as auto-stop is back on
void checkAutoStop(SoLoud::Soloud &aSoloud)
{
	SoLoud::Wav wav;
	generateTimingTestWave(wav, RATE * 3, RATE);
	SoLoud::handle h = start(aSoloud, wav, 2.0, 1.0);
	aSoloud.setAutoStop(h, false);
	Capture cap = capture(aSoloud, h, 2 * RATE / BUF); // 1.5 s of source would play in 0.75 s
	CHECK(aSoloud.isValidVoiceHandle(h));
	// the resampler stops its lookahead short of the last frame
	CHECK(std::abs(aSoloud.getStreamPosition(h) - 3.0) * RATE < 16.0);
	CHECK(*std::max_element(cap.samples.begin() + RATE * 8 / 5, cap.samples.end()) == 0.0f);
	aSoloud.setAutoStop(h, true);
	capture(aSoloud, h, 2);
	CHECK(!aSoloud.isValidVoiceHandle(h));
	aSoloud.stopAll();
}

// The same sequence produces the same output, bit for bit
void checkDeterminism(SoLoud::Soloud &aSoloud, SoLoud::AudioSource &aSource)
{
	auto run = [&]() {
		SoLoud::handle h = start(aSoloud, aSource, 1.5, 1.0);
		Capture a = capture(aSoloud, h, 100);
		aSoloud.seek(h, 1.7);
		Capture b = capture(aSoloud, h, 100);
		aSoloud.stopAll();
		a.samples.insert(a.samples.end(), b.samples.begin(), b.samples.end());
		return a.samples;
	};
	std::vector<float> first = run();
	std::vector<float> second = run();
	CHECK(first.size() == second.size() && memcmp(first.data(), second.data(), first.size() * sizeof(float)) == 0);
}
} // namespace

void testTimeStretch()
{
	checkLibraryUnity();

	SoLoud::Soloud soloud;
	// no roundoff clipping, so what comes out is what went in
	SoLoud::result res = soloud.init(0, BACKEND, RATE, BUF, 2);
	CHECK_RES(res);
	soloud.setPostClipScaler(1.0f);

	std::vector<float> impulses(IMPULSE_FRAMES, 0.0f);
	for (unsigned int i = IMPULSE_OFFSET; i < IMPULSE_FRAMES; i += IMPULSE_PERIOD)
		impulses[i] = 1.0f;
	std::vector<unsigned char> wavFile = makeWavFile(impulses);
	SoLoud::Wav wav; // generic tape seek
	wav.loadRawWave(impulses.data(), IMPULSE_FRAMES, (float)RATE, 1, true, false);
	SoLoud::WavStream stream; // codec seek
	res = stream.loadMem(wavFile.data(), (unsigned int)wavFile.size(), false, false);
	CHECK_RES(res);

	CHECK(soloud.setTempo(soloud.play(wav, 1.0f, 0.0f, true), 0.0f) == SoLoud::INVALID_PARAMETER);
	CHECK(soloud.setPitchShift(soloud.play(wav, 1.0f, 0.0f, true), -1.0f) == SoLoud::INVALID_PARAMETER);
	soloud.stopAll();

	checkDuration(soloud);
	checkTransparency(soloud);
	checkPitch(soloud);
	for (double tempo : TEMPOS)
	{
		checkAlignment(soloud, wav, tempo, "Wav");
		checkAlignment(soloud, stream, tempo, "WavStream");
	}
	checkEngageLive(soloud, wav);
	checkEngageLive(soloud, stream);
	checkDeterminism(soloud, stream);

	GenericSource generic(false);
	checkDisengage(soloud, wav, true, "Wav");
	checkDisengage(soloud, stream, true, "WavStream");
	checkDisengage(soloud, generic, false, "a source without rewind");
	checkLiveChange(soloud, wav, "Wav");
	checkLiveChange(soloud, generic, "a source without rewind");
	checkFade(soloud, wav);
	// a 3 second source for the loop test, so that the loop comes round twice within a few seconds
	SoLoud::Wav loopWav;
	loopWav.loadRawWave(impulses.data(), RATE * 3, (float)RATE, 1, true, false);
	std::vector<unsigned char> loopWavFile = makeWavFile(std::vector<float>(impulses.begin(), impulses.begin() + RATE * 3));
	SoLoud::WavStream loopStream;
	res = loopStream.loadMem(loopWavFile.data(), (unsigned int)loopWavFile.size(), false, false);
	CHECK_RES(res);
	for (double tempo : {0.75, 1.5})
	{
		checkLoop(soloud, loopWav, tempo, "Wav");
		checkLoop(soloud, loopStream, tempo, "WavStream");
	}
	checkInaudibleTick(soloud, wav);
	checkAutoStop(soloud);
	checkMultichannel(soloud, impulses);

	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
