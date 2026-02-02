#include "sanity.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

// Test various core functionality
void testCore()
{
	float scratch[2048]{};
	float ref[2048]{};
	SoLoud::result res;
	SoLoud::Soloud soloud;
	SoLoud::Wav wav;
	generateTestWave(wav);
	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND);
	CHECK_RES(res);

	soloud.setPostClipScaler(1.0f);
	soloud.setGlobalVolume(1.0f);
	soloud.play(wav);
	soloud.mix(ref, 1000);
	soloud.stopAll();

	soloud.setGlobalVolume(0.5f);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_GTE(ref, scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.setGlobalVolume(1.0f);

	soloud.setPostClipScaler(0.5f);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_GTE(ref, scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.setPostClipScaler(1.0f);

	SoLoud::handle h = soloud.play(wav);
	soloud.setPause(h, true);
	soloud.mix(scratch, 1000);
	CHECK_BUF_ZERO(scratch, 2000);
	soloud.stopAll();

	soloud.play(wav);
	soloud.setPauseAll(true);
	soloud.mix(scratch, 1000);
	CHECK_BUF_ZERO(scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.setRelativePlaySpeed(h, 0.3f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.setSamplerate(h, 12345);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.setPan(h, 0.75f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.setPanAbsolute(h, 0.75f, 0.25f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	wav.setVolume(0.5f);
	h = soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_GTE(ref, scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	wav.setVolume(1.0f);

	h = soloud.play(wav);
	soloud.setVolume(h, 0.5f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_GTE(ref, scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.fadeVolume(h, 0.5f, 1.0f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_GTE(ref, scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.fadePan(h, 0.75f, 1.0f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.fadeRelativePlaySpeed(h, 0.5f, 1.0f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	soloud.play(wav);
	soloud.fadeGlobalVolume(0.5f, 1.0f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_GTE(ref, scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.setGlobalVolume(1.0f);

	h = soloud.play(wav);
	soloud.schedulePause(h, 0.015f); // note: pausescheduler works on mix granularity
	soloud.mix(scratch, 500);
	soloud.mix(scratch + 1000, 500);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	CHECK(soloud.getVoiceCount() > 0);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.scheduleStop(h, 0.015f); // note: stopscheduler works on mix granularity
	soloud.mix(scratch, 500);
	soloud.mix(scratch + 1000, 500);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	CHECK(soloud.getVoiceCount() == 0);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.oscillateVolume(h, 0.25f, 0.75f, 0.2f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_GTE(ref, scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.oscillatePan(h, 0.25f, 0.75f, 0.2f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.oscillateRelativePlaySpeed(h, 0.75f, 1.25f, 0.2f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	soloud.play(wav);
	soloud.oscillateGlobalVolume(0.25f, 0.75f, 0.2f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_GTE(ref, scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.setGlobalVolume(1.0f);

	h = soloud.play(wav);
	soloud.setLooping(h, false);
	int i;
	for (i = 0; i < 100; i++)
		soloud.mix(scratch, 1000);
	CHECK(soloud.getVoiceCount() == 0);
	soloud.stopAll();

	h = soloud.play(wav);
	soloud.setLooping(h, true);
	CHECK(soloud.getLoopCount(h) == 0);
	for (i = 0; i < 100; i++)
		soloud.mix(scratch, 1000);
	CHECK(soloud.getVoiceCount() != 0);
	CHECK(soloud.getLoopCount(h) != 0);
	soloud.stopAll();

	int vg = soloud.createVoiceGroup();
	CHECK(soloud.isVoiceGroup(vg) != 0);
	CHECK(soloud.isVoiceGroupEmpty(vg) != 0);
	h = soloud.play(wav);
	soloud.addVoiceToGroup(vg, h);
	CHECK(soloud.isVoiceGroup(h) == 0);
	CHECK(soloud.isVoiceGroupEmpty(vg) == 0);
	soloud.destroyVoiceGroup(vg);
	CHECK(soloud.isVoiceGroup(vg) == 0);

	h = soloud.play(wav);
	CHECK(soloud.getStreamPosition(h) == 0);
	soloud.setLooping(h, true);
	for (i = 0; i < 100; i++)
		soloud.mix(scratch, 1000);
	CHECK(soloud.getStreamPosition(h) != 0);
	CHECK(soloud.countAudioSource(wav) != 0);
	soloud.stopAll();
	CHECK(soloud.countAudioSource(wav) == 0);

	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
