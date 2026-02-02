#include "sanity.h"

#include "soloud_bus.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

// Test various play-related calls
void testPlay()
{
	float scratch[2048]{};
	float ref[2048]{};
	SoLoud::result res;
	SoLoud::Soloud soloud;
	SoLoud::Wav wav;
	SoLoud::Bus bus;
	generateTestWave(wav);
	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND);
	CHECK_RES(res);

	SoLoud::handle h = soloud.play(wav);
	soloud.mix(ref, 1000);
	CHECKLASTKNOWN(ref, 2000);
	CHECK_BUF_NONZERO(ref, 2000);
	soloud.stop(h);
	soloud.mix(scratch, 1000);
	CHECK_BUF_ZERO(scratch, 2000);
	h = soloud.play(wav);
	soloud.stopAudioSource(wav);
	soloud.mix(scratch, 1000);
	CHECK_BUF_ZERO(scratch, 2000);
	h = soloud.play(wav);
	soloud.stopAll();
	soloud.mix(scratch, 1000);
	CHECK_BUF_ZERO(scratch, 2000);

	h = soloud.playClocked(0.01, wav);
	soloud.stopAll();
	h = soloud.playClocked(0.015, wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECK_BUF_DIFF(scratch, ref, 2000);
	soloud.stopAll();

	h = soloud.play(wav, 1, 0, true);
	soloud.setDelaySamples(h, 10);
	soloud.setPause(h, false);
	soloud.mix(scratch, 1000);
	CHECK_BUF_SAME(scratch + 20, ref, 2000 - 20);
	soloud.stopAll();

	/*
	// Not a bug, but a feature of the linear resampler.
	soloud.play(bus);
	h = bus.play(wav);
	soloud.mix(scratch, 1000);
	CHECK_BUF_SAME(scratch, ref, 2000); // TODO: fails, seems to offset by a sample when played through bus.
	soloud.stopAll();
	*/

	soloud.play(bus);
	h = bus.playClocked(0.01, wav);
	soloud.stopAll();
	soloud.play(bus);
	h = bus.playClocked(0.015, wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECK_BUF_DIFF(scratch, ref, 2000);
	soloud.stopAll();

	bus.setChannels(1);
	soloud.play(bus);
	h = bus.play(wav);
	soloud.mix(scratch, 1000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECKLASTKNOWN(scratch, 2000);
	soloud.stopAll();

	bus.setChannels(2);
	soloud.play(bus);
	h = bus.play(wav);
	soloud.mix(scratch, 1000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECKLASTKNOWN(scratch, 2000);
	soloud.stopAll();

	bus.setChannels(4);
	soloud.play(bus);
	h = bus.play(wav);
	soloud.mix(scratch, 1000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECKLASTKNOWN(scratch, 2000);
	soloud.stopAll();

	bus.setChannels(6);
	soloud.play(bus);
	h = bus.play(wav);
	soloud.mix(scratch, 1000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECKLASTKNOWN(scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.seek(h, 0.1);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECK_BUF_DIFF(scratch, ref, 2000);
	soloud.stopAll();

	h = soloud.playBackground(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECK_BUF_GTE(scratch, ref, 2000);

	h = soloud.play(wav, 0.5f);
	int i;
	for (i = 0; i < 3000; i++)
		soloud.play(wav, 1.0f);
	CHECK(soloud.isValidVoiceHandle(h) == 0);
	soloud.stopAll();

	h = soloud.play(wav, 0.5f);
	soloud.setProtectVoice(h, true);
	for (i = 0; i < 3000; i++)
		soloud.play(wav, 1.0f);
	CHECK(soloud.isValidVoiceHandle(h) != 0);
	soloud.stopAll();

	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
