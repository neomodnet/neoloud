#include "sanity.h"

#include "soloud_loudness.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

// Benchmark: measure throughput of integratedLoudness in audio-seconds per wall-second.
// Simulates a realistic workload: many stereo 44100Hz tracks of ~3 minutes each.
// Uses loadRawWave with ownership transfer to avoid WAV decoding overhead,
// isolating the LUFS computation cost.
void benchLoudness()
{
	constexpr unsigned int sampleRate = 44100;
	constexpr unsigned int channels = 2;
	constexpr unsigned int trackDurationSec = 180; // 3 minutes per track
	constexpr unsigned int samplesPerTrack = sampleRate * trackDurationSec;
	constexpr unsigned int totalFloats = samplesPerTrack * channels;

	// target: process enough audio to get a stable measurement
	// 100 tracks * 180s = 18,000 seconds of audio
	constexpr int numTracks = 100;

	// pre-generate one track of sample data.
	// realistic content: sum of a few sine waves at different frequencies, similar spectral profile to music.
	auto *buf = new float[totalFloats];
	for (unsigned int i = 0; i < samplesPerTrack; i++)
	{
		float t = (float)i / (float)sampleRate;
		float sample = 0.3f * std::sin(2.0f * (float)M_PI * 440.0f * t) + 0.2f * std::sin(2.0f * (float)M_PI * 880.0f * t) +
		               0.15f * std::sin(2.0f * (float)M_PI * 220.0f * t) + 0.1f * std::sin(2.0f * (float)M_PI * 1760.0f * t);
		// stereo: identical channels (channel layout doesn't affect perf)
		buf[i] = sample;
		buf[samplesPerTrack + i] = sample;
	}

	// wav instance to run loudness on
	SoLoud::Wav wav;
	wav.loadRawWave(buf, totalFloats, (float)sampleRate, channels, /*aCopy=*/true);

	double totalAudioSec = 0.0;
	long startMs = getmsec();

	for (int t = 0; t < numTracks; t++)
	{
		float lufs = 0.0f;
		SoLoud::Loudness::integratedLoudness(wav, lufs);
		totalAudioSec += trackDurationSec;
	}

	long elapsedMs = getmsec() - startMs;
	double elapsedSec = elapsedMs / 1000.0;
	double throughput = totalAudioSec / elapsedSec;

	// extrapolate: how long for 30,000,000 seconds of audio?
	double targetAudioSec = 30000000.0;
	double estimatedSec = targetAudioSec / throughput;

	SoLoud::logStdout("\nLoudness benchmark: %d tracks x %ds = %.0f audio-seconds\n", numTracks, trackDurationSec, totalAudioSec);
	SoLoud::logStdout("  Wall time: %.2f sec\n", elapsedSec);
	SoLoud::logStdout("  Throughput: %.0f audio-sec / wall-sec (%.0fx realtime)\n", throughput, throughput);
	SoLoud::logStdout("  Projected for 30M audio-sec: %.0f sec (%.1f min)\n", estimatedSec, estimatedSec / 60.0);

	delete[] buf;
}

void testLoudness()
{
	SoLoud::Wav wav;
	generateTestWave(wav);

	// test with generated audio: should return a finite LUFS value
	float lufs = 0.0f;
	SoLoud::result res = SoLoud::Loudness::integratedLoudness(wav, lufs);
	CHECK(res == SoLoud::SO_NO_ERROR);
	CHECK(lufs != (float)-HUGE_VAL);
	CHECK(lufs > -100.0f && lufs < 0.0f);
	PRINTINFO("Loudness of test wave: %.2f LUFS\n", lufs);

	// test determinism: same source should give same result
	float lufs2 = 0.0f;
	res = SoLoud::Loudness::integratedLoudness(wav, lufs2);
	CHECK(res == SoLoud::SO_NO_ERROR);
	CHECK(std::abs(lufs - lufs2) < 0.001f);

	// test with a known sine wave for a more predictable range
	SoLoud::Wav sineWav;
	generateTimingTestWave(sineWav, 44100 * 2, 44100); // 2 seconds of 440Hz sine at 44100Hz
	float sineLufs = 0.0f;
	res = SoLoud::Loudness::integratedLoudness(sineWav, sineLufs);
	CHECK(res == SoLoud::SO_NO_ERROR);
	CHECK(sineLufs != (float)-HUGE_VAL);
	CHECK(sineLufs > -40.0f && sineLufs < 0.0f);
	PRINTINFO("Loudness of 440Hz sine (16-bit, amp ~0.49): %.2f LUFS\n", sineLufs);

	// test silent audio: loadRawWave with zeros
	SoLoud::Wav silentWav;
	float silentData[1024]{};
	silentWav.loadRawWave(silentData, 1024, 44100, 1, true);
	float silentLufs = 0.0f;
	res = SoLoud::Loudness::integratedLoudness(silentWav, silentLufs);
	CHECK(res == SoLoud::SO_NO_ERROR);
	CHECK(silentLufs == (float)-HUGE_VAL);

	// test short audio (< 400ms): should still produce a result
	SoLoud::Wav shortWav;
	generateTimingTestWave(shortWav, 4410, 44100); // 100ms
	float shortLufs = 0.0f;
	res = SoLoud::Loudness::integratedLoudness(shortWav, shortLufs);
	CHECK(res == SoLoud::SO_NO_ERROR);
	CHECK(shortLufs != (float)-HUGE_VAL);
	PRINTINFO("Loudness of 100ms sine: %.2f LUFS\n", shortLufs);
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
