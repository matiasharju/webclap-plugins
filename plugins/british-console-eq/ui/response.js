// Frequency response of the British Console EQ, using exactly the same filter designs as british-console-eq.h
// (and shared/webclap/dsp.h).
// Used by the UI (to draw the curve) and by tests/smoke-test.mjs (to check the curve matches the audio).
//
// `params` uses the parameter keys: {hpfOn, hpfFreq, ..., eqIn}

(function (root) {
	'use strict';

	const shelfQ = Math.SQRT1_2, hpfQ = 1, lpfQ = Math.SQRT1_2;

	function prewarp(freq, sampleRate) {
		return Math.tan(Math.PI*Math.min(freq, 0.45*sampleRate)/sampleRate);
	}
	function bell(freq, gainDb, q, sr) {
		const A = Math.pow(10, gainDb/40), k = 1/(q*A);
		return {g: prewarp(freq, sr), k, m0: 1, m1: k*(A*A - 1), m2: 0};
	}
	function lowShelf(freq, gainDb, q, sr) {
		const A = Math.pow(10, gainDb/40), k = 1/q;
		return {g: prewarp(freq, sr)/Math.sqrt(A), k, m0: 1, m1: k*(A - 1), m2: A*A - 1};
	}
	function highShelf(freq, gainDb, q, sr) {
		const A = Math.pow(10, gainDb/40), k = 1/q;
		return {g: prewarp(freq, sr)*Math.sqrt(A), k, m0: A*A, m1: k*(1 - A)*A, m2: 1 - A*A};
	}
	function lowpass(freq, q, sr) {
		return {g: prewarp(freq, sr), k: 1/q, m0: 0, m1: 0, m2: 1};
	}
	function highpass(freq, q, sr) {
		return {g: prewarp(freq, sr), k: 1/q, m0: 1, m1: -1/q, m2: -1};
	}
	// "Proportional Q": gentle boosts/cuts are broad, big ones get narrower
	function proportionalQFactor(gainDb) {
		return 0.5 + Math.abs(gainDb)/15;
	}

	// Complex numbers as [re, im]
	const mul = (...values) => values.reduce((a, b) => [a[0]*b[0] - a[1]*b[1], a[0]*b[1] + a[1]*b[0]], [1, 0]);
	const ONE = [1, 0];
	const toDb = (c) => 10*Math.log10(c[0]*c[0] + c[1]*c[1] + 1e-30);

	// Trapezoidal SVF = bilinear transform: s = j*tan(w/2)/g
	function svf(c, t) {
		const x = t/c.g;
		const dr = 1 - x*x, di = c.k*x, d = dr*dr + di*di;
		const lpRe = dr/d, lpIm = -di/d; // lowpass = 1/(s^2 + k s + 1)
		const bpRe = -x*lpIm, bpIm = x*lpRe; // bandpass = s * lowpass
		return [c.m0 + c.m1*bpRe + c.m2*lpRe, c.m1*bpIm + c.m2*lpIm];
	}
	function onePoleHighpass(freq, sr, t) {
		const x = t/prewarp(freq, sr); // s/(s + 1)
		return [x*x/(1 + x*x), x/(1 + x*x)];
	}

	// Complex response of each section at `freq`
	function sections(p, sr, freq) {
		const t = Math.tan(Math.PI*Math.min(freq, 0.4999*sr)/sr);
		const lfGain = p.lfGain, lmfGain = p.lmfGain, hmfGain = p.hmfGain, hfGain = p.hfGain;

		const hp = mul(onePoleHighpass(p.hpfFreq, sr, t), svf(highpass(p.hpfFreq, hpfQ, sr), t));
		const lp = svf(lowpass(p.lpfFreq, lpfQ, sr), t);
		return {
			hpf: p.hpfOn >= 0.5 ? hp : ONE,
			lpf: p.lpfOn >= 0.5 ? lp : ONE,
			lf: svf(lowShelf(p.lfFreq, lfGain, shelfQ, sr), t),
			lmf: svf(bell(p.lmfFreq, lmfGain, p.lmfQ*proportionalQFactor(lmfGain), sr), t),
			hmf: svf(bell(p.hmfFreq, hmfGain, p.hmfQ*proportionalQFactor(hmfGain), sr), t),
			hf: svf(highShelf(p.hfFreq, hfGain, shelfQ, sr), t),
		};
	}

	// Total response in dB (what the plugin does to a sine wave at `freq`)
	function responseDb(p, sampleRate, freq) {
		const s = sections(p, sampleRate, freq);
		const eq = p.eqIn >= 0.5 ? mul(s.lf, s.lmf, s.hmf, s.hf) : ONE;
		return toDb(mul(s.hpf, s.lpf, eq));
	}

	// Each section on its own, in dB (for drawing the individual bands)
	function sectionsDb(p, sampleRate, freq) {
		const s = sections(p, sampleRate, freq);
		return {
			filters: toDb(mul(s.hpf, s.lpf)),
			lf: toDb(s.lf), lmf: toDb(s.lmf), hmf: toDb(s.hmf), hf: toDb(s.hf)
		};
	}

	const api = {responseDb, sectionsDb};
	if (typeof module === 'object' && module.exports) module.exports = api;
	else root.EqResponse = api;
})(this);
