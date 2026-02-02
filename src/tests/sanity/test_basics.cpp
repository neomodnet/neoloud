#include "sanity.h"

#include "soloud_biquadresonantfilter.h"
#include "soloud_bus.h"
#include "soloud_sfxr.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

// Some info tests
void testMisc()
{
	float scratch[2048]{};
	short scratch_i16[2048]{};
	SoLoud::result res;
	SoLoud::Soloud soloud;
	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND);
	CHECK_RES(res);
	SoLoud::Wav wav;
	generateTestWave(wav);
	CHECK(wav.getLength() != 0);
	unsigned int ver = soloud.getVersion();
	CHECK(ver == SOLOUD_VERSION);
	PRINTINFO("SoLoud version %d\n", ver);
	CHECK(soloud.getErrorString(0) != nullptr);
	PRINTINFO("Backend %d: %s, %d channels, %d samplerate, %d buffersize\n",
	          soloud.getBackendId(),
	          soloud.getBackendString(),
	          soloud.getBackendChannels(),
	          soloud.getBackendSamplerate(),
	          soloud.getBackendBufferSize());
	CHECK(soloud.getBackendId() != 0);
	CHECK(soloud.getBackendString() != nullptr);
	CHECK(soloud.getBackendChannels() != 0);
	CHECK(soloud.getBackendSamplerate() != 0);
	CHECK(soloud.getBackendBufferSize() != 0);

	soloud.mix(scratch, 1000);
	soloud.mix(scratch_i16, 1000, SoLoud::mixing::SAMPLE_SIGNED16);
	CHECK_BUF_ZERO(scratch, 2000);
	CHECK_BUF_ZERO(scratch_i16, 2000);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	soloud.mix(scratch_i16, 1000, SoLoud::mixing::SAMPLE_SIGNED16);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECK_BUF_NONZERO(scratch_i16, 2000);

	SoLoud::Misc::Prg prg;
	prg.srand(0x1337);
	unsigned int a = prg.rand();
	prg.srand(0x1337);
	unsigned int b = prg.rand();
	CHECK(a == b);
	a = 0;
	unsigned int prev = prg.rand();
	for (b = 0; b < 100; b++)
	{
		unsigned int next = prg.rand();
		if (prev != next)
			a = 1;
		prev = next;
	}
	CHECK(a == 1);

	soloud.deinit();
}

// Test parameter getters
void testGetters()
{
	float scratch[2048]{};
	SoLoud::result res;
	SoLoud::Soloud soloud;
	SoLoud::Sfxr sfxr;
	SoLoud::BiquadResonantFilter filter;
	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND);
	CHECK_RES(res);
	res = sfxr.loadPreset(4, 0);
	CHECK_RES(res);
	sfxr.setFilter(0, &filter);

	CHECK(soloud.getActiveVoiceCount() == 0);
	CHECK(soloud.getVoiceCount() == 0);

	CHECK(soloud.isValidVoiceHandle((SoLoud::handle)0xbaadf00d) == 0);
	SoLoud::handle h = soloud.play(sfxr);
	CHECK(soloud.isValidVoiceHandle(h));

	CHECK(soloud.getActiveVoiceCount() == 1);
	CHECK(soloud.getVoiceCount() == 1);

	float v_in, v_out;
	v_in = 0.7447f;
	soloud.setFilterParameter(h, 0, 0, v_in);
	v_out = soloud.getFilterParameter(h, 0, 0);
	CHECK(std::abs(v_in - v_out) < 0.00001);

	CHECK(soloud.getStreamTime(h) < 0.00001);
	soloud.mix(scratch, 1000);
	CHECK(soloud.getStreamTime(h) > 0.00001);

	CHECK(soloud.getPause(h) == 0);
	soloud.setPause(h, true);
	CHECK(soloud.getPause(h) != 0);

	float oldvol = soloud.getOverallVolume(h);
	soloud.setVolume(h, v_in);
	v_out = soloud.getVolume(h);
	CHECK(std::abs(v_in - v_out) < 0.00001);
	CHECK(std::abs(oldvol - v_out) > 0.00001);

	soloud.setPan(h, v_in);
	CHECK(std::abs(v_in - soloud.getPan(h)) < 0.00001);

	soloud.setSamplerate(h, v_in);
	CHECK(std::abs(v_in - soloud.getSamplerate(h)) < 0.00001);

	CHECK(soloud.getProtectVoice(h) == 0);
	soloud.setProtectVoice(h, true);
	CHECK(soloud.getProtectVoice(h) != 0);

	soloud.setRelativePlaySpeed(h, v_in);
	CHECK(std::abs(v_in - soloud.getRelativePlaySpeed(h)) < 0.00001);

	soloud.setPostClipScaler(v_in);
	CHECK(std::abs(v_in - soloud.getPostClipScaler()) < 0.00001);

	soloud.setGlobalVolume(v_in);
	CHECK(std::abs(v_in - soloud.getGlobalVolume()) < 0.00001);

	CHECK(soloud.getLooping(h) == 0);
	soloud.setLooping(h, true);
	CHECK(soloud.getLooping(h) != 0);

	CHECK(soloud.getMaxActiveVoiceCount() > 0);
	soloud.setMaxActiveVoiceCount(123);
	CHECK(soloud.getMaxActiveVoiceCount() == 123);

	soloud.set3dSoundSpeed(123);
	CHECK(soloud.get3dSoundSpeed() == 123);

	soloud.deinit();
}

// Visualization API tests
void testVis()
{
	float scratch[2048]{};
	SoLoud::result res;
	SoLoud::Soloud soloud;
	SoLoud::Sfxr sfxr;
	SoLoud::Bus bus;
	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND);
	CHECK_RES(res);
	res = sfxr.loadPreset(4, 0);
	CHECK_RES(res);

	int bush = soloud.play(bus);
	SoLoud::handle h = bus.play(sfxr);
	soloud.setVisualizationEnable(true);
	bus.setVisualizationEnable(true);

	soloud.mix(scratch, 1000);
	float approxvol = soloud.getApproximateVolume(0);
	CHECK(approxvol != 0);

	float *w = soloud.getWave();
	CHECK(w != NULL);
	if (w)
	{
		int i;
		int nonzero = 0;
		for (i = 0; i < 256; i++)
			if (w[i] != 0)
				nonzero = 1;
		CHECK(nonzero != 0);
	}

	approxvol = bus.getApproximateVolume(0);
	CHECK(approxvol != 0);

	w = bus.getWave();
	CHECK(w != NULL);
	if (w)
	{
		int i;
		int nonzero = 0;
		for (i = 0; i < 256; i++)
			if (w[i] != 0)
				nonzero = 1;
		CHECK(nonzero != 0);
	}

	w = soloud.calcFFT();
	CHECK(w != NULL);
	if (w)
	{
		int i;
		int nonzero = 0;
		for (i = 0; i < 256; i++)
			if (w[i] != 0)
				nonzero = 1;
		CHECK(nonzero != 0);
	}

	w = bus.calcFFT();
	CHECK(w != NULL);
	if (w)
	{
		int i;
		int nonzero = 0;
		for (i = 0; i < 256; i++)
			if (w[i] != 0)
				nonzero = 1;
		CHECK(nonzero != 0);
	}

	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
