#pragma once

// Digital console channel EQ, laid out like the Behringer WING channel EQ:
//   input filters: LO CUT (6/12/18/24 dB/oct), HI CUT (6/12 dB/oct), TILT
//   EQ: six bands L, 1-4, H (L and H: shelf or parametric), MIX scales all band gains, ON/OFF
//   band solo: hear only the frequency range of one band
//
// The UI draws its curve with the same formulas (ui/response.js), so keep the two in sync.

#include "webclap/dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace digital_console_eq {

using namespace webclap::dsp;

constexpr int bandCount = 6; // L, 1, 2, 3, 4, H
constexpr double shelfQ = 0.7071067811865476;
constexpr double butterworthQ = 0.7071067811865476;

struct Settings {
	bool lcOn = false, hcOn = false, tiltOn = false;
	double lcFreq = 100, hcFreq = 10000;
	int lcSlope = 1; // 0-3: 6, 12, 18, 24 dB/oct
	int hcSlope = 1; // 0-1: 6, 12 dB/oct
	double tiltGain = 0, tiltFreq = 1000;
	bool eqOn = true;
	double mix = 100; // %
	struct Band {
		double gain = 0, freq = 1000, q = 1;
		bool parametric = true; // L and H can be shelves
	} bands[bandCount];
	int solo = 0; // 0 = off, 1-6 = band L..H
};

class DigitalConsoleEq {
public:
	void prepare(double newSampleRate) {
		sampleRate = newSampleRate;
		// ~15ms smoothing, updated every `chunk` samples
		smoothingCoeff = 1 - std::exp(-double(chunk)/(0.015*sampleRate));
	}

	void reset(const Settings &settings) {
		lc1.reset();
		hc1.reset();
		tilt.reset();
		for (auto *f : {&lc2, &lc3, &hc2, &lShelf, &hShelf, &solo}) f->reset();
		for (auto &f : bells) f.reset();
		targets(settings, current); // no gliding after a reset
		updateFilters(settings);
	}

	void process(const Settings &settings, const float *inL, const float *inR, float *outL, float *outR, uint32_t frames) {
		double target[count];
		targets(settings, target);

		const float *in[2] = {inL, inR};
		float *out[2] = {outL, outR};
		for (uint32_t start = 0; start < frames; start += chunk) {
			uint32_t end = std::min(frames, start + chunk);
			for (int i = 0; i < count; ++i) current[i] += (target[i] - current[i])*smoothingCoeff;
			updateFilters(settings);

			const double lcMix = current[sLcMix], hcMix = current[sHcMix], eqMix = current[sEqMix], soloMix = current[sSoloMix];
			const double lBellMix = current[sBand + 0*perBand + bParametric], hBellMix = current[sBand + 5*perBand + bParametric];
			for (int c = 0; c < 2; ++c) {
				for (uint32_t i = start; i < end; ++i) {
					double x = in[c][i];

					// input slot filters
					double lc = lc3.process(c, lc2.process(c, lc1.process(c, x)));
					x += lcMix*(lc - x);
					double hc = hc2.process(c, hc1.process(c, x));
					x += hcMix*(hc - x);
					x = tilt.process(c, x);

					// EQ slot
					double dry = x;
					double shelf = lShelf.process(c, x), bell = bells[0].process(c, x);
					x = shelf + lBellMix*(bell - shelf);
					for (int b = 1; b <= 4; ++b) x = bells[b].process(c, x);
					shelf = hShelf.process(c, x);
					bell = bells[5].process(c, x);
					x = shelf + hBellMix*(bell - shelf);
					x = dry + eqMix*(x - dry);

					// band solo: just the selected band's range, from the signal entering the EQ
					double soloed = solo.process(c, dry);
					x += soloMix*(soloed - x);

					out[c][i] = float(x);
				}
			}
		}
		lc1.flushDenormals();
		hc1.flushDenormals();
		tilt.flushDenormals();
		for (auto *f : {&lc2, &lc3, &hc2, &lShelf, &hShelf, &solo}) f->flushDenormals();
		for (auto &f : bells) f.flushDenormals();
	}

private:
	static constexpr uint32_t chunk = 16;
	double sampleRate = 48000, smoothingCoeff = 1;

	// Smoothed values: frequencies and Qs as log2 (so they glide evenly), gains in dB, switches as 0-1 crossfades
	enum {bGain, bFreq, bQ, bParametric, perBand};
	enum {sLcMix, sLcFreq, sHcMix, sHcFreq, sTiltGain, sTiltFreq, sEqMix, sMix, sSoloMix,
		sBand, count = sBand + bandCount*perBand};
	double current[count] = {};
	int soloBand = 0; // last band soloed, kept while the solo fades out

	OnePole lc1, hc1, tilt;
	Svf lc2, lc3, hc2, lShelf, hShelf, solo;
	Svf bells[bandCount]; // 0 and 5: L and H as parametric bands

	static void targets(const Settings &s, double *t) {
		t[sLcMix] = s.lcOn;
		t[sLcFreq] = std::log2(s.lcFreq);
		t[sHcMix] = s.hcOn;
		t[sHcFreq] = std::log2(s.hcFreq);
		t[sTiltGain] = s.tiltOn ? s.tiltGain : 0;
		t[sTiltFreq] = std::log2(s.tiltFreq);
		t[sEqMix] = s.eqOn;
		t[sMix] = s.mix/100;
		t[sSoloMix] = s.solo > 0;
		for (int b = 0; b < bandCount; ++b) {
			double *bt = t + sBand + b*perBand;
			bt[bGain] = s.bands[b].gain;
			bt[bFreq] = std::log2(s.bands[b].freq);
			bt[bQ] = std::log2(s.bands[b].q);
			bt[bParametric] = s.bands[b].parametric;
		}
	}

	void updateFilters(const Settings &s) {
		const double *c = current;
		const double sr = sampleRate;

		// LO CUT: 6 = 1-pole, 12 = 2-pole Butterworth, 18 = 1-pole + 2-pole (Q 1), 24 = two 2-poles (4th-order Butterworth)
		const double lcFreq = std::exp2(c[sLcFreq]);
		if (s.lcSlope == 0 || s.lcSlope == 2) lc1.setHighpass(lcFreq, sr); else lc1.setPassThrough();
		if (s.lcSlope == 0) lc2.set(passThrough());
		else if (s.lcSlope == 1) lc2.set(highpass(lcFreq, butterworthQ, sr));
		else if (s.lcSlope == 2) lc2.set(highpass(lcFreq, 1.0, sr));
		else lc2.set(highpass(lcFreq, 0.5411961001461970, sr));
		if (s.lcSlope == 3) lc3.set(highpass(lcFreq, 1.3065629648763766, sr)); else lc3.set(passThrough());

		// HI CUT: 6 = 1-pole, 12 = 2-pole Butterworth
		const double hcFreq = std::exp2(c[sHcFreq]);
		if (s.hcSlope == 0) {
			hc1.setLowpass(hcFreq, sr);
			hc2.set(passThrough());
		} else {
			hc1.setPassThrough();
			hc2.set(lowpass(hcFreq, butterworthQ, sr));
		}

		// TILT: 1-pole shelf, -gain/2 at the bottom and +gain/2 at the top, crossing at the tilt frequency
		const double tiltHalf = std::pow(10.0, c[sTiltGain]/40);
		tilt.setShelf(std::exp2(c[sTiltFreq]), 1/tiltHalf, tiltHalf, sr);

		// bands: MIX scales every band's gain
		for (int b = 0; b < bandCount; ++b) {
			const double *bc = c + sBand + b*perBand;
			const double gain = bc[bGain]*c[sMix], freq = std::exp2(bc[bFreq]), q = std::exp2(bc[bQ]);
			bells[b].set(bell(freq, gain, q, sr));
			if (b == 0) lShelf.set(lowShelf(freq, gain, shelfQ, sr));
			if (b == bandCount - 1) hShelf.set(highShelf(freq, gain, shelfQ, sr));
		}

		// band solo
		if (s.solo > 0) soloBand = s.solo;
		if (soloBand > 0) {
			const int b = soloBand - 1;
			const double *bc = c + sBand + b*perBand;
			const double freq = std::exp2(bc[bFreq]), q = std::exp2(bc[bQ]);
			if (b == 0 && !s.bands[0].parametric) solo.set(lowpass(freq, shelfQ, sr));
			else if (b == bandCount - 1 && !s.bands[b].parametric) solo.set(highpass(freq, shelfQ, sr));
			else solo.set(bandpass(freq, q, sr));
		}
	}
};

} // namespace
