#include "sanity.h"

#include "soloud_bus.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

class customAttenuatorCollider : public SoLoud::AudioCollider, public SoLoud::AudioAttenuator
{
public:
	virtual float collide(SoLoud::Soloud *aSoloud, SoLoud::AudioSourceInstance3dData *aAudioInstance3dData, int aUserData) { return 0.5f; }
	virtual float attenuate(float aDistance, float aMinDistance, float aMaxDistance, float aRolloffFactor) { return 0.5f; }
};

// Test various 3d functions
void test3d()
{
	customAttenuatorCollider customAC;
	float scratch[2048]{};
	float ref[2048]{};
	SoLoud::result res;
	SoLoud::Soloud soloud;
	SoLoud::Bus bus;
	SoLoud::Wav wav;
	generateTestWave(wav);
	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND);
	CHECK_RES(res);
	soloud.set3dListenerParameters(0, 5, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0);
	soloud.set3dSoundSpeed(343);
	soloud.setSpeakerPosition(0, 2, 0, 1);
	soloud.setSpeakerPosition(0, -2, 0, 1);
	wav.set3dMinMaxDistance(1, 200);
	wav.set3dAttenuation(SoLoud::AudioSource::EXPONENTIAL_DISTANCE, 0.5);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(ref, 1000);
	CHECK_BUF_NONZERO(ref, 2000);
	CHECKLASTKNOWN(ref, 2000);
	soloud.stopAll();

	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_SAME(ref, scratch, 2000);
	soloud.stopAll();

	wav.set3dAttenuation(SoLoud::AudioSource::EXPONENTIAL_DISTANCE, 0.25);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	wav.set3dAttenuation(SoLoud::AudioSource::EXPONENTIAL_DISTANCE, 0.5);

	wav.set3dMinMaxDistance(1, 20);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	wav.set3dMinMaxDistance(1, 200);

	soloud.play3dClocked(0.01f, wav, 10, 20, 30, 1, 1, 1);
	soloud.stopAll();
	soloud.play3dClocked(0.02f, wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	CHECK_BUF_NONZERO(scratch, 2000);
	soloud.stopAll();

	soloud.set3dSoundSpeed(34);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.set3dSoundSpeed(343);

	soloud.set3dListenerParameters(1, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.set3dListenerParameters(0, 5, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0);

	soloud.set3dListenerPosition(1, 2, 3);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.set3dListenerParameters(0, 5, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0);

	soloud.set3dListenerAt(1, 2, 3);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.set3dListenerParameters(0, 5, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0);

	soloud.set3dListenerUp(1, 2, 3);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.set3dListenerParameters(0, 5, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0);

	soloud.set3dListenerVelocity(1, -1, 0);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.set3dListenerParameters(0, 5, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0);

	SoLoud::handle h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.set3dSourceAttenuation(h, SoLoud::AudioSource::LINEAR_DISTANCE, 0.5f);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.set3dSourceDopplerFactor(h, 2.5f);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.set3dSourceMinMaxDistance(h, 1, 20);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.set3dSourcePosition(h, 20, 10, 30);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.set3dSourceVelocity(h, 2, 1, 3);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.set3dSourceParameters(h, 1, 2, 3, 4, 5, 6);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.set3dSourceParameters(h, 1, 2, 3, 4, 5, 6);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	soloud.setSpeakerPosition(0, 3, -1, -1);
	soloud.setSpeakerPosition(0, -3, -1, 1);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	soloud.setSpeakerPosition(0, 2, 0, 1);
	soloud.setSpeakerPosition(0, -2, 0, 1);

	wav.set3dListenerRelative(true);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	wav.set3dListenerRelative(false);

	wav.set3dDistanceDelay(true);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000); // skip time to get to when the sound starts
	soloud.mix(scratch, 1000);
	soloud.mix(scratch, 1000);
	soloud.mix(scratch, 1000);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	wav.set3dDistanceDelay(false);

	wav.set3dDopplerFactor(15.0f);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	wav.set3dDopplerFactor(1.0f);

	wav.set3dAttenuator(&customAC);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	wav.set3dAttenuator(0);

	wav.set3dCollider(&customAC);
	soloud.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	wav.set3dCollider(0);

	wav.setInaudibleBehavior(false, false); // don't tick, but don't kill if inaudible
	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1, 0.0f);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECK(soloud.isValidVoiceHandle(h) == 1);
	soloud.stopAll();

	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1, 0.0f);
	soloud.setInaudibleBehavior(h, false, true); // don't tick, kill if inaudible
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECK(soloud.isValidVoiceHandle(h) == 0);
	soloud.stopAll();

	wav.setInaudibleBehavior(false, true); // don't tick, kill if inaudible
	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1, 0.0f);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECK(soloud.isValidVoiceHandle(h) == 0);
	soloud.stopAll();

	wav.setInaudibleBehavior(false, false); // don't tick, but don't kill if inaudible
	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1, 0.0f);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	soloud.setVolume(h, 1.0f);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECK_BUF_GTE(ref, scratch, 2000); // fails, looks like 3d audio has some initial volume ramp problem..
	soloud.stopAll();

	wav.setInaudibleBehavior(true, false);
	h = soloud.play3d(wav, 10, 20, 30, 1, 1, 1, 0.0f);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	soloud.setVolume(h, 1.0f);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECKLASTKNOWN(scratch, 2000);
	soloud.stopAll();

	soloud.play(bus);
	bus.play3d(wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(ref, 1000);
	CHECK_BUF_NONZERO(ref, 2000);
	CHECKLASTKNOWN(ref, 2000);
	soloud.stopAll();

	soloud.play(bus);
	bus.play3dClocked(2.01f, wav, 10, 20, 30, 1, 1, 1);
	soloud.stopAll();
	soloud.play(bus);
	bus.play3dClocked(2.02f, wav, 10, 20, 30, 1, 1, 1);
	soloud.update3dAudio();
	soloud.mix(scratch, 1000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	CHECK_BUF_NONZERO(scratch, 2000);
	CHECKLASTKNOWN(scratch, 2000);
	soloud.stopAll();

	float x, y, z;
	soloud.setSpeakerPosition(0, 1, 2, 3);
	soloud.getSpeakerPosition(0, x, y, z);
	CHECK(x == 1 && y == 2 && z == 3);
	soloud.setSpeakerPosition(0, 4, 5, 6);
	soloud.getSpeakerPosition(0, x, y, z);
	CHECK(x != 1 && y != 2 && z != 3);

	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
