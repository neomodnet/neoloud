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

// the planar mix has to return the same samples as the interleaved one, channel by channel, in every output format
void testPlanarMix()
{
	using namespace SoLoud::mixing;
	constexpr unsigned int SAMPLES = 1000;
	constexpr unsigned int CHANNELS = 2;
	constexpr unsigned int MAX_BYTES_PER_SAMPLE = 4;
	constexpr long long CONVERSION_TOLERANCE = 1; // the simd interleave rounds to nearest where the scalar conversion truncates

	// a mix consumes what it returns, so each variant gets an engine of its own, playing the same thing
	SoLoud::Soloud soloud, planarSoloud;
	SoLoud::Wav wav, planarWav;
	SoLoud::result res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND, SoLoud::Soloud::AUTO, SoLoud::Soloud::AUTO, CHANNELS);
	CHECK_RES(res);
	res = planarSoloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND, SoLoud::Soloud::AUTO, SoLoud::Soloud::AUTO, CHANNELS);
	CHECK_RES(res);
	generateTestWave(wav);
	generateTestWave(planarWav);
	// panned so the channels differ
	soloud.play(wav, 1.0f, -0.5f);
	planarSoloud.play(planarWav, 1.0f, -0.5f);

	unsigned char interleaved[SAMPLES * CHANNELS * MAX_BYTES_PER_SAMPLE]{};
	unsigned char planar[CHANNELS][SAMPLES * MAX_BYTES_PER_SAMPLE]{};
	void *planarBuffers[CHANNELS] = {planar[0], planar[1]};

	// signed 32 as is, and the bit pattern for float 32 (both variants copy floats verbatim)
	auto decode = [](const unsigned char *aSample, SAMPLE_FORMAT aFormat) -> long long {
		int sample = 0;
		switch (aFormat)
		{
		case SAMPLE_UNSIGNED8:
			return aSample[0];
		case SAMPLE_SIGNED16:
			return (short)(aSample[0] | (aSample[1] << 8));
		case SAMPLE_SIGNED24:
			return (int)((unsigned int)(aSample[0] | (aSample[1] << 8) | (aSample[2] << 16)) << 8) >> 8;
		default:
			memcpy(&sample, aSample, sizeof(sample));
			return sample;
		}
	};

	const SAMPLE_FORMAT formats[] = {SAMPLE_FLOAT32, SAMPLE_UNSIGNED8, SAMPLE_SIGNED16, SAMPLE_SIGNED24, SAMPLE_SIGNED32};
	const unsigned int bytesPerSample[] = {4, 1, 2, 3, 4};
	for (unsigned int f = 0; f < sizeof(formats) / sizeof(formats[0]); f++)
	{
		soloud.mix(interleaved, SAMPLES, formats[f]);
		planarSoloud.mixPlanar(planarBuffers, SAMPLES, formats[f]);
		CHECK_BUF_NONZERO(planar[0], (int)(SAMPLES * bytesPerSample[f]));

		int diff = 0;
		for (unsigned int c = 0; c < CHANNELS; c++)
		{
			for (unsigned int i = 0; i < SAMPLES; i++)
			{
				long long delta = decode(interleaved + (i * CHANNELS + c) * bytesPerSample[f], formats[f]) - decode(planar[c] + i * bytesPerSample[f], formats[f]);
				if (delta > CONVERSION_TOLERANCE || delta < -CONVERSION_TOLERANCE)
					diff++;
			}
		}
		PRINTINFO("Planar vs interleaved mix, format %d: %d samples differ\n", formats[f], diff);
		CHECK(diff == 0);
	}

	soloud.deinit();
	planarSoloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
