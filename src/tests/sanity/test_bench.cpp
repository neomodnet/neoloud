#include "sanity.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

// base
// avg 0.459, med 0.467 +- 0.033 (0.450 - 0.483)
// avg 0.462, med 0.477 + -0.063 (0.446 - 0.509)
// avg 0.466, med 0.485 +- 0.056 (0.457 - 0.513)

// resamplers
// avg 0.474, med 0.477 +- 0.037 (0.458 - 0.495)
// avg 0.474, med 0.479 +- 0.029 (0.465 - 0.494)
// avg 0.470, med 0.474 +- 0.033 (0.457 - 0.490)

void testSpeedThings()
{
	float scratch[2048]{};
	SoLoud::result res;
	SoLoud::Soloud soloud;
	SoLoud::Wav wav;
	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND);
	CHECK_RES(res);
	soloud.setMainResampler(SoLoud::Soloud::RESAMPLER_CATMULLROM);
	int j;
	generateTestWave(wav);
	int k;
	long t = 0, c = 0, mn = 1000000, mx = 0;
	long fst = getmsec();
	for (k = 0; k < 100; k++)
	{
		long st = getmsec();
		for (j = 0; j < 500; j++)
		{
			int i;
			for (i = 0; i < 10; i++)
			{
				soloud.play(wav);
			}
			for (i = 0; i < 20; i++)
				soloud.mix(scratch, 1000);
		}
		long et = getmsec();
		c++;
		long d = et - st;
		t += d;
		if (mn > d)
			mn = d;
		if (mx < d)
			mx = d;

		SoLoud::logStdout("Mix loop took %3.3f sec, avg %3.3f, med %3.3f +- %3.3f (%3.3f - %3.3f)\n",
		                  (et - st) / 1000.0f,
		                  t / (c * 1000.0f),
		                  (mn + mx) / 2000.0f,
		                  (mx - mn) / 1000.0f,
		                  mn / 1000.0f,
		                  mx / 1000.0f);
	}
	long fet = getmsec();
	SoLoud::logStdout("Total %3.3f sec\n", (fet - fst) / 1000.0f);
	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
