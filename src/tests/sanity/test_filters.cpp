#include "sanity.h"

#include "soloud_bassboostfilter.h"
#include "soloud_biquadresonantfilter.h"
#include "soloud_dcremovalfilter.h"
#include "soloud_echofilter.h"
#include "soloud_flangerfilter.h"
#include "soloud_lofifilter.h"
#include "soloud_robotizefilter.h"
#include "soloud_waveshaperfilter.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

// Test various filter options
void testFilters()
{
	float scratch[2048]{};
	float ref[2048]{};
	float ref2[2048]{};
	SoLoud::result res;
	SoLoud::Soloud soloud;
	SoLoud::Wav wav;
	generateTestWave(wav);
	SoLoud::BiquadResonantFilter biquad;
	SoLoud::LofiFilter lofi;
	SoLoud::EchoFilter echo;
	SoLoud::BassboostFilter bass;
	SoLoud::FlangerFilter flang;
	SoLoud::DCRemovalFilter dc;
	SoLoud::FFTFilter fft;
	SoLoud::RobotizeFilter rob;
	SoLoud::WaveShaperFilter wshap;

	res = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, BACKEND);
	CHECK_RES(res);

	soloud.play(wav);
	soloud.mix(ref, 1000);
	soloud.stopAll();
	CHECKLASTKNOWN(ref, 2000);

	lofi.setParams(4000, 5);
	wav.setFilter(0, &lofi);
	soloud.play(wav);
	soloud.mix(ref2, 1000);
	CHECKLASTKNOWN(ref2, 2000);
	CHECK_BUF_DIFF(ref, ref2, 2000);
	soloud.stopAll();
	lofi.setParams(2000, 3);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref2, scratch, 2000);
	soloud.stopAll();
	lofi.setParams(4000, 5);
	SoLoud::handle h = soloud.play(wav);
	soloud.setFilterParameter(h, 0, 0, 0.5f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	lofi.setParams(4000, 5);
	h = soloud.play(wav);
	soloud.fadeFilterParameter(h, 0, 0, 0.5f, 1.0f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();
	lofi.setParams(4000, 5);
	h = soloud.play(wav);
	soloud.oscillateFilterParameter(h, 0, 0, 0.25f, 0.75f, 0.5f);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref, scratch, 2000);
	soloud.stopAll();

	biquad.setParams(SoLoud::BiquadResonantFilter::LOWPASS, 2000, 5);
	wav.setFilter(0, &biquad);
	soloud.play(wav);
	soloud.mix(ref2, 1000);
	CHECKLASTKNOWN(ref2, 2000);
	CHECK_BUF_DIFF(ref, ref2, 2000);
	soloud.stopAll();
	biquad.setParams(SoLoud::BiquadResonantFilter::HIGHPASS, 1000, 5);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref2, scratch, 2000);
	soloud.stopAll();

	echo.setParams(0.05f, 0.5f, 0.5f);
	wav.setFilter(0, &echo);
	soloud.play(wav);
	soloud.mix(ref2, 1000);
	CHECKLASTKNOWN(ref2, 2000);
	CHECK_BUF_DIFF(ref, ref2, 2000);
	soloud.stopAll();
	echo.setParams(0.01f, 0.75f, 0.1f);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref2, scratch, 2000);
	soloud.stopAll();

	bass.setParams(2);
	wav.setFilter(0, &bass);
	soloud.play(wav);
	soloud.mix(ref2, 1000);
	CHECKLASTKNOWN(ref2, 2000);
	CHECK_BUF_DIFF(ref, ref2, 2000);
	soloud.stopAll();
	bass.setParams(10);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref2, scratch, 2000);
	soloud.stopAll();

	dc.setParams(0.1f);
	wav.setFilter(0, &dc);
	soloud.play(wav);
	soloud.mix(ref2, 1000);
	CHECKLASTKNOWN(ref2, 2000);
	CHECK_BUF_DIFF(ref, ref2, 2000);
	soloud.stopAll();
	dc.setParams(0.5f);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref2, scratch, 2000);
	soloud.stopAll();

	flang.setParams(0.05f, 10);
	wav.setFilter(0, &flang);
	soloud.play(wav);
	soloud.mix(ref2, 1000);
	CHECKLASTKNOWN(ref2, 2000);
	CHECK_BUF_DIFF(ref, ref2, 2000);
	soloud.stopAll();
	flang.setParams(0.005f, 5);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref2, scratch, 2000);
	soloud.stopAll();
	wav.setFilter(0, 0);
	soloud.setGlobalFilter(0, &flang);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref2, scratch, 2000);
	soloud.stopAll();

	wav.setFilter(0, &fft);
	soloud.play(wav);
	soloud.mix(ref2, 1000);
	CHECKLASTKNOWN(ref2, 2000);
	CHECK_BUF_DIFF(ref, ref2, 2000);
	soloud.stopAll();
	wav.setFilter(0, 0);

	wav.setFilter(0, &rob);
	soloud.play(wav);
	soloud.mix(ref2, 1000);
	CHECKLASTKNOWN(ref2, 2000);
	CHECK_BUF_DIFF(ref, ref2, 2000);
	soloud.stopAll();
	wav.setFilter(0, 0);

	wshap.setParams(0.05f);
	wav.setFilter(0, &wshap);
	soloud.play(wav);
	soloud.mix(ref2, 1000);
	CHECKLASTKNOWN(ref2, 2000);
	CHECK_BUF_DIFF(ref, ref2, 2000);
	soloud.stopAll();
	wshap.setParams(0.005f);
	soloud.play(wav);
	soloud.mix(scratch, 1000);
	CHECKLASTKNOWN(scratch, 2000);
	CHECK_BUF_DIFF(ref2, scratch, 2000);
	soloud.stopAll();
	wav.setFilter(0, 0);

	soloud.deinit();
}

// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
