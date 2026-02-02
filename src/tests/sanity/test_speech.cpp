#include "sanity.h"

#include "soloud_speech.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

// Test speech audio source
//
// Speech.setText
// Speech.setParams
// Speech.setLooping
// Speech.setLoopPoint
// Speech.getLoopPoint
// Speech.stop
// Soloud.getLoopPoint
// Soloud.setLoopPoint
void testSpeech()
{
	float scratch[2048]{};
	float ref[2048]{};
	memset(scratch, 0, sizeof(scratch));
	memset(ref, 0, sizeof(ref));
	SoLoud::result res;
	SoLoud::Soloud soloud;
	SoLoud::Speech speech;
	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND);
	CHECK_RES(res);

	speech.setParams(1330, 10, 0.5f, 1);
	speech.setText("Why is it so loud");
	soloud.play(speech);
	soloud.mix(ref, 1000);
	CHECK_BUF_NONZERO(ref, 2000);
	CHECKLASTKNOWN(ref, 2000);
	soloud.stopAll();

	speech.setParams(1330, 10, 0.5f, 1);
	speech.setText("Why is it so loud");
	soloud.play(speech);
	soloud.mix(scratch, 1000);
	CHECK_BUF_SAME(scratch, ref, 2000);
	CHECKLASTKNOWN(scratch, 2000);
	soloud.stopAll();

	speech.setText("a different text");
	soloud.play(speech);
	soloud.mix(scratch, 1000);
	CHECK_BUF_DIFF(scratch, ref, 2000);
	CHECKLASTKNOWN(scratch, 2000);
	soloud.stopAll();
	speech.setText("Why is it so loud");

	speech.setParams(1000, 5, 0, 2);
	soloud.play(speech);
	soloud.mix(scratch, 1000);
	CHECK_BUF_DIFF(scratch, ref, 2000);
	CHECKLASTKNOWN(scratch, 2000);
	soloud.stopAll();
	speech.setParams(1330, 10, 0.5f, 1);

	speech.setLooping(false);
	soloud.play(speech);
	int i;
	for (i = 0; i < 100; i++)
		soloud.mix(scratch, 1000);
	CHECK(soloud.getActiveVoiceCount() == 0);
	soloud.stopAll();

	speech.setLooping(true);
	soloud.play(speech);
	for (i = 0; i < 100; i++)
		soloud.mix(scratch, 1000);
	soloud.mix(ref, 1000);
	CHECK_BUF_DIFF(scratch, ref, 2000);
	CHECKLASTKNOWN(ref, 1000);
	CHECK(soloud.getActiveVoiceCount() != 0);
	speech.stop();
	CHECK(soloud.getActiveVoiceCount() == 0);
	soloud.stopAll();

	// Crazy x87 FPU things
	const volatile double zero_one = 0.1;
	const volatile double zero_five = 0.5;

	speech.setLoopPoint(zero_one);
	CHECK(speech.getLoopPoint() == zero_one);
	speech.setLoopPoint(zero_five);
	CHECK(speech.getLoopPoint() != zero_one);
	SoLoud::handle handle = soloud.play(speech);
	soloud.setLoopPoint(handle, zero_one);
	CHECK(soloud.getLoopPoint(handle) == zero_one);
	soloud.setLoopPoint(handle, zero_five);
	CHECK(soloud.getLoopPoint(handle) != zero_one);

	for (i = 0; i < 100; i++)
		soloud.mix(scratch, 1000);
	soloud.mix(scratch, 1000);
	CHECK_BUF_DIFF(ref, scratch, 1000);
	CHECKLASTKNOWN(ref, 1000);

	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
