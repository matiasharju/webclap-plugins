#pragma once

// British-console-style channel EQ: HPF + LPF, then LF shelf, LMF and HMF bells, HF shelf.
// The mid bands have "proportional Q", and MIX scales all band gains.
//
// The filters come from shared/webclap/dsp.h.
//
// The UI draws its curve with the same formulas (ui/response.js), so keep the two in sync.

#include "webclap/dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace console_eq {

using namespace webclap::dsp;

// Shelves are fixed at Q 0.707 (no overshoot)
constexpr double shelfQ = 0.7071067811865476;
// HPF: 3rd-order Butterworth (one-pole + 2-pole with Q 1) = 18 dB/oct.  LPF: 2-pole Butterworth = 12 dB/oct.
constexpr double hpfQ = 1.0, lpfQ = 0.7071067811865476;

// "Proportional Q": gentle boosts/cuts are broad, big ones get narrower (like many classic console EQs)
inline double proportionalQFactor(double gainDb) {
	return 0.5 + std::abs(gainDb)/15;
}

// ---- the EQ ----

struct Settings {
	bool hpfOn = false, lpfOn = false;
	double hpfFreq = 80, lpfFreq = 12000;
	double hfGain = 0, hfFreq = 8000;
	double hmfGain = 0, hmfFreq = 3000, hmfQ = 0.8;
	double lmfGain = 0, lmfFreq = 1000, lmfQ = 0.8;
	double lfGain = 0, lfFreq = 200;
	bool eqIn = true;
	double mix = 100; // %
};

class ConsoleEq {
public:
	void prepare(double newSampleRate) {
		sampleRate = newSampleRate;
		// ~15ms smoothing, updated every `chunk` samples
		smoothingCoeff = 1 - std::exp(-double(chunk)/(0.015*sampleRate));
	}

	void reset(const Settings &settings) {
		for (auto *f : {&hpf2, &lpf, &lfShelf, &lmf, &hmf, &hfShelf}) f->reset();
		hpf1.reset();
		targets(settings, current); // no gliding after a reset
		updateFilters();
	}

	void process(const Settings &settings, const float *inL, const float *inR, float *outL, float *outR, uint32_t frames) {
		double target[count];
		targets(settings, target);

		const float *in[2] = {inL, inR};
		float *out[2] = {outL, outR};
		for (uint32_t start = 0; start < frames; start += chunk) {
			uint32_t end = std::min(frames, start + chunk);
			for (int i = 0; i < count; ++i) current[i] += (target[i] - current[i])*smoothingCoeff;
			updateFilters();

			const double hpfMix = current[sHpfMix], lpfMix = current[sLpfMix], eqMix = current[sEqMix];
			for (int c = 0; c < 2; ++c) {
				for (uint32_t i = start; i < end; ++i) {
					double x = in[c][i];

					double hp = hpf2.process(c, hpf1.process(c, x));
					x += hpfMix*(hp - x);
					double lp = lpf.process(c, x);
					x += lpfMix*(lp - x);

					double dry = x;
					x = lfShelf.process(c, x);
					x = lmf.process(c, x);
					x = hmf.process(c, x);
					x = hfShelf.process(c, x);
					x = dry + eqMix*(x - dry);

					out[c][i] = float(x);
				}
			}
		}
		for (auto *f : {&hpf2, &lpf, &lfShelf, &lmf, &hmf, &hfShelf}) f->flushDenormals();
		hpf1.flushDenormals();
	}

private:
	static constexpr uint32_t chunk = 16;
	double sampleRate = 48000, smoothingCoeff = 1;

	// Smoothed values: frequencies and Qs as log2 (so they glide evenly), gains in dB, switches as 0-1 crossfades
	enum {sHpfMix, sHpfFreq, sLpfMix, sLpfFreq,
		sHfGain, sHfFreq, sHmfGain, sHmfFreq, sHmfQ, sLmfGain, sLmfFreq, sLmfQ, sLfGain, sLfFreq,
		sEqMix, sMix, count};
	double current[count] = {};

	OnePoleHighpass hpf1;
	Svf hpf2, lpf, lfShelf, lmf, hmf, hfShelf;

	static void targets(const Settings &s, double *t) {
		t[sHpfMix] = s.hpfOn;
		t[sHpfFreq] = std::log2(s.hpfFreq);
		t[sLpfMix] = s.lpfOn;
		t[sLpfFreq] = std::log2(s.lpfFreq);
		t[sHfGain] = s.hfGain;
		t[sHfFreq] = std::log2(s.hfFreq);
		t[sHmfGain] = s.hmfGain;
		t[sHmfFreq] = std::log2(s.hmfFreq);
		t[sHmfQ] = std::log2(s.hmfQ);
		t[sLmfGain] = s.lmfGain;
		t[sLmfFreq] = std::log2(s.lmfFreq);
		t[sLmfQ] = std::log2(s.lmfQ);
		t[sLfGain] = s.lfGain;
		t[sLfFreq] = std::log2(s.lfFreq);
		t[sEqMix] = s.eqIn;
		t[sMix] = s.mix/100;
	}

	void updateFilters() {
		const double *c = current;
		const double sr = sampleRate, mix = c[sMix];
		hpf1.set(std::exp2(c[sHpfFreq]), sr);
		hpf2.set(highpass(std::exp2(c[sHpfFreq]), hpfQ, sr));
		lpf.set(lowpass(std::exp2(c[sLpfFreq]), lpfQ, sr));
		// MIX scales every band's gain; the mid bands' Q follows their (scaled) gain
		const double lfGain = c[sLfGain]*mix, lmfGain = c[sLmfGain]*mix, hmfGain = c[sHmfGain]*mix, hfGain = c[sHfGain]*mix;
		lfShelf.set(lowShelf(std::exp2(c[sLfFreq]), lfGain, shelfQ, sr));
		lmf.set(bell(std::exp2(c[sLmfFreq]), lmfGain, std::exp2(c[sLmfQ])*proportionalQFactor(lmfGain), sr));
		hmf.set(bell(std::exp2(c[sHmfFreq]), hmfGain, std::exp2(c[sHmfQ])*proportionalQFactor(hmfGain), sr));
		hfShelf.set(highShelf(std::exp2(c[sHfFreq]), hfGain, shelfQ, sr));
	}
};

} // namespace
