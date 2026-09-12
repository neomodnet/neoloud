#include "sanity.h"

#include "soloud_wavstream.h"

#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

namespace
{
// A 44.1 kHz ramp played through a 48 kHz engine: every output sample decodes back to the source frame it came from,
// so the audio that actually comes out can be compared with what getStreamPosition() claims is playing.
constexpr unsigned int SRC_RATE = 44100;
constexpr unsigned int ENGINE_RATE = 48000;
constexpr unsigned int BUF = 512;
constexpr unsigned int FRAMES = SRC_RATE * 3;

double decodeFrame(float aSample)
{
	return ((double)aSample - 0.1) / 0.8 * FRAMES;
}

// the same ramp as a float WAV file in memory, for the codec seek path
std::vector<unsigned char> makeWavFile(const std::vector<float> &aRamp)
{
	std::vector<unsigned char> file(44 + aRamp.size() * sizeof(float));
	auto put32 = [&](size_t aAt, unsigned int aValue) { memcpy(&file[aAt], &aValue, 4); };
	auto put16 = [&](size_t aAt, unsigned short aValue) { memcpy(&file[aAt], &aValue, 2); };
	memcpy(&file[0], "RIFF", 4);
	put32(4, (unsigned int)file.size() - 8);
	memcpy(&file[8], "WAVEfmt ", 8);
	put32(16, 16);
	put16(20, 3); // IEEE float
	put16(22, 1);
	put32(24, SRC_RATE);
	put32(28, SRC_RATE * sizeof(float));
	put16(32, sizeof(float));
	put16(34, 32);
	memcpy(&file[36], "data", 4);
	put32(40, (unsigned int)(aRamp.size() * sizeof(float)));
	memcpy(&file[44], aRamp.data(), aRamp.size() * sizeof(float));
	return file;
}
} // namespace

// Stream position vs. what actually plays, across seeks and loop wraps
void testPosition()
{
	std::vector<float> ramp(FRAMES);
	for (unsigned int i = 0; i < FRAMES; i++)
		ramp[i] = (float)(0.1 + 0.8 * (double)i / FRAMES);
	std::vector<unsigned char> wavFile = makeWavFile(ramp);

	SoLoud::Soloud soloud;
	// no roundoff clipping, so the ramp comes through untouched
	SoLoud::result res = soloud.init(0, BACKEND, ENGINE_RATE, BUF, 2);
	CHECK_RES(res);
	soloud.setPostClipScaler(1.0f);

	SoLoud::Wav wav; // generic tape seek
	wav.loadRawWave(ramp.data(), FRAMES, (float)SRC_RATE, 1, true, false);
	SoLoud::WavStream stream; // codec seek
	res = stream.loadMem(wavFile.data(), (unsigned int)wavFile.size(), false, false);
	CHECK_RES(res);

	float out[BUF * 2];
	// the source frame the next buffer starts at according to the engine, and the one that actually comes out
	auto reported = [&](SoLoud::handle h) { return soloud.getStreamPosition(h) * SRC_RATE; };
	auto mixAndDecode = [&]() {
		soloud.mix(out, BUF);
		return decodeFrame(out[0]);
	};
	auto start = [&](SoLoud::AudioSource &aSource) {
		// start paused so pan and volume snap into place instead of ramping over the first buffer
		SoLoud::handle h = soloud.play(aSource, 1.0f, 0.0f, true);
		soloud.setPanAbsolute(h, 1.0f, 1.0f);
		soloud.setVolume(h, 1.0f);
		soloud.setPause(h, false);
		return h;
	};
	auto checkSeek = [&](SoLoud::handle h, double aSeconds, bool aPaused) {
		if (aPaused)
			soloud.setPause(h, true);
		res = soloud.seek(h, aSeconds);
		CHECK_RES(res);
		if (aPaused)
			soloud.setPause(h, false);
		// the first sample out is the target, and playback stays in step with the position afterwards
		double expected = reported(h);
		CHECK(std::abs(expected - aSeconds * SRC_RATE) < 1.0);
		CHECK(std::abs(mixAndDecode() - expected) < 1.0);
		for (int i = 0; i < 20; i++)
			soloud.mix(out, BUF);
		expected = reported(h);
		CHECK(std::abs(mixAndDecode() - expected) < 1.0);
	};

	SoLoud::AudioSource *sources[2] = {&wav, &stream};
	for (SoLoud::AudioSource *source : sources)
	{
		SoLoud::handle h = start(*source);
		for (int i = 0; i < 30; i++)
			soloud.mix(out, BUF);
		double expected = reported(h);
		CHECK(std::abs(mixAndDecode() - expected) < 1.0);

		checkSeek(h, 2.0, false);
		checkSeek(h, 0.5, false);
		checkSeek(h, 1.0, true);
		// a small forward seek lands inside the audio already queued for resampling
		checkSeek(h, reported(h) / SRC_RATE + 0.002, false);
		// seek targets are in source time regardless of play speed
		soloud.setRelativePlaySpeed(h, 1.5f);
		checkSeek(h, 2.5, false);
		soloud.setRelativePlaySpeed(h, 1.0f);
		// unpausing resumes exactly where playback stopped, without a gap
		soloud.setPause(h, true);
		soloud.setPause(h, false);
		expected = reported(h);
		CHECK(std::abs(mixAndDecode() - expected) < 1.0);
		soloud.stopAll();

		// looping: the position follows the wrap when it plays (not when the loop point is read), and the wrap is a clean crossfade
		source->setLooping(true);
		source->setLoopPoint(0.25);
		h = start(*source);
		double prev = -1.0;
		int wraps = 0, buffersSinceWrap = 100, mismatches = 0, hardJumps = 0;
		for (unsigned int b = 0; b < 7 * ENGINE_RATE / BUF; b++)
		{
			expected = reported(h);
			double played = mixAndDecode();
			bool wrapInBuffer = false;
			for (size_t k = 0; k < BUF; k++)
			{
				double frame = decodeFrame(out[2 * k]);
				if (prev >= 0 && std::abs(frame - prev) > 2000.0)
					hardJumps++;
				if (prev >= FRAMES / 2.0 && frame < FRAMES / 2.0)
					wrapInBuffer = true;
				prev = frame;
			}
			if (wrapInBuffer)
				wraps++;
			buffersSinceWrap = wrapInBuffer ? 0 : buffersSinceWrap + 1;
			// outside the two buffers around a wrap, where the first sample may sit inside the crossfade
			if (buffersSinceWrap > 1 && std::abs(played - expected) >= 1.0)
				mismatches++;
		}
		CHECK(wraps == 2);
		CHECK(mismatches == 0);
		CHECK(hardJumps == 0);
		source->setLooping(false);
		soloud.stopAll();
	}

	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
