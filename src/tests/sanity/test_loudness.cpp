#include "sanity.h"

#include "soloud_loudness.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

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
