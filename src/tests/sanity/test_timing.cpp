#include "sanity.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

// Test that relative play speed produces the expected playback duration.
// If the sound has duration T at 1x speed, it should have duration T/speed at speed X.
// Uses a long audio file (10s) to minimize chunk quantization error.
void testRelativePlaySpeedTiming()
{
	float scratch[2048]{};
	SoLoud::result res;
	SoLoud::Soloud soloud;

	const unsigned int backendSampleRate = 44100;
	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND, backendSampleRate, 2048, 2);
	CHECK_RES(res);

	soloud.setMainResampler(SoLoud::Soloud::RESAMPLER_CATMULLROM);

	struct SpeedTest
	{
		float speed;
		const char *name;
	};

	SpeedTest speedTests[] = {
	    {.speed = 1.0f,  .name = "1.0x (normal)"},
        {.speed = 2.0f,  .name = "2.0x (double)"},
        {.speed = 0.5f,  .name = "0.5x (half)"  },
	    {.speed = 1.5f,  .name = "1.5x"         },
        {.speed = 0.75f, .name = "0.75x"        },
        {.speed = 3.0f,  .name = "3.0x (triple)"},
	};

	// Test with different source sample rates to exercise resampling
	unsigned int sourceSampleRates[] = {44100, 48000, 22050, 96000};

	const unsigned int mixChunkSize = 512;
	// Absolute tolerance: 1 chunk worth of time (~11.6ms at 44100Hz)
	// This is the inherent quantization of chunked processing
	const float absoluteToleranceSec = (float)mixChunkSize / (float)backendSampleRate;

	for (unsigned int sourceSampleRate : sourceSampleRates)
	{
		SoLoud::Wav wav;

		const unsigned int sourceDurationSec = 20;
		const unsigned int sourceSamples = sourceSampleRate * sourceDurationSec;
		generateTimingTestWave(wav, sourceSamples, sourceSampleRate);

		float wavLength = wav.getLength();
		CHECK(std::abs(wavLength - (float)sourceDurationSec) < 0.01f);
		PRINTINFO("\nSource: %u Hz, %u samples (%.1fs)\n", sourceSampleRate, sourceSamples, wavLength);

		for (const auto &test : speedTests)
		{
			SoLoud::handle h = soloud.play(wav);
			soloud.setRelativePlaySpeed(h, test.speed);

			// Count full chunks while voice is active.
			// For the final chunk, count non-zero samples (sine wave is non-zero except at zero crossings)
			unsigned int totalSamplesMixed = 0;
			const unsigned int maxIterations = 10000000;

			for (unsigned int iter = 0; iter < maxIterations; iter++)
			{
				if (!soloud.isValidVoiceHandle(h))
					break;

				memset(scratch, 0, sizeof(scratch));
				soloud.mix(scratch, mixChunkSize);

				if (!soloud.isValidVoiceHandle(h))
				{
					// Voice stopped during this mix - find the last non-zero sample
					unsigned int lastNonZero = 0;
					for (unsigned int i = 0; i < mixChunkSize; i++)
					{
						if (scratch[i] != 0.0f || scratch[i + mixChunkSize] != 0.0f)
							lastNonZero = i + 1;
					}
					totalSamplesMixed += lastNonZero;
				}
				else
				{
					totalSamplesMixed += mixChunkSize;
				}
			}

			float expectedDuration = wavLength / test.speed;
			float actualDuration = (float)totalSamplesMixed / (float)backendSampleRate;
			float deviationSec = std::abs(actualDuration - expectedDuration);

			PRINTINFO("  Speed %s: expected %.4fs, actual %.4fs (deviation: %.1fms)\n", test.name, expectedDuration, actualDuration, deviationSec * 1000.0f);

			gTests++;
			if (deviationSec > absoluteToleranceSec)
			{
				gErrorCount++;
				SoLoud::logStdout(__FILE__ ":" STRINGIZE_MACRO(__LINE__) ":%s: Playback timing for %uHz @ %s exceeded tolerance: "
				                                                         "expected %.4fs, got %.4fs (%.1fms > %.1fms tolerance)\n",
				                  PFUNC, sourceSampleRate, test.name, expectedDuration, actualDuration, deviationSec * 1000.0f, absoluteToleranceSec * 1000.0f);
			}

			soloud.stopAll();
		}
	}

	soloud.deinit();
}

void testMixer()
{
	SoLoud::Soloud soloud;
	SoLoud::Wav wav;
	soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER);

	float val[16] = {0.8f, -0.8f, 0.8f, -0.8f, 0.8f, -0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
	wav.loadRawWave(val, 2, 441, 1, true);
	float scratch[2048]{};
	wav.setLooping(true);
	SoLoud::handle h = soloud.play(wav);
	int i;
	for (i = 0; i < 10; i++)
		soloud.mix(scratch + 200 * i, 100);
	int waszero = 0;
	for (i = 0; i < 2000; i++)
		if (scratch[i] < 0.00001)
		{
			if (!waszero)
			{
				SoLoud::logStdout("Zero at index %d", i);
			}
			waszero = 1;
		}
		else
		{
			if (waszero)
			{
				SoLoud::logStdout(" - %d\n", i - 1);
			}
			waszero = 0;
		}
	if (waszero)
	{
		SoLoud::logStdout(" - %d\n", i - 1);
	}

	soloud.stopAll();
	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
