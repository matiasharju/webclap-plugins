// Frequency response of the Digital Console EQ, using exactly the same filter designs as
// digital-console-eq.h (and shared/webclap/dsp.h).
// Used by the UI (to draw the curve) and by tests/smoke-test.mjs (to check the curve matches the audio).
//
// `params` uses the parameter keys: {lcOn, lcFreq, ..., lGain, lFreq, lQ, lType, b1Gain, ..., solo}

(function (root) {
	'use strict';

	const shelfQ = Math.SQRT1_2, butterworthQ = Math.SQRT1_2;

	// The six bands: parameter keys, and whether the band can be a shelf
	const BANDS = [
		{id: 'l', gain: 'lGain', freq: 'lFreq', q: 'lQ', type: 'lType', shelf: 'low'},
		{id: 'b1', gain: 'b1Gain', freq: 'b1Freq', q: 'b1Q'},
		{id: 'b2', gain: 'b2Gain', freq: 'b2Freq', q: 'b2Q'},
		{id: 'b3', gain: 'b3Gain', freq: 'b3Freq', q: 'b3Q'},
		{id: 'b4', gain: 'b4Gain', freq: 'b4Freq', q: 'b4Q'},
		{id: 'h', gain: 'hGain', freq: 'hFreq', q: 'hQ', type: 'hType', shelf: 'high'},
	];

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
	function bandpass(freq, q, sr) {
		return {g: prewarp(freq, sr), k: 1/q, m0: 0, m1: 1/q, m2: 0};
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
	// 1-pole: m0 + m1*lowpass, lowpass = 1/(1 + s)
	function onePole(freq, sr, t, m0, m1) {
		const x = t/prewarp(freq, sr);
		const lpRe = 1/(1 + x*x), lpIm = -x/(1 + x*x);
		return [m0 + m1*lpRe, m1*lpIm];
	}

	function bandFilter(band, p, sr) {
		const gain = p[band.gain]*p.mix/100, freq = p[band.freq], q = p[band.q];
		if (band.shelf && p[band.type] < 0.5) {
			return band.shelf === 'low' ? lowShelf(freq, gain, shelfQ, sr) : highShelf(freq, gain, shelfQ, sr);
		}
		return bell(freq, gain, q, sr);
	}

	// Complex response of each part at `freq`
	function sections(p, sr, freq) {
		const t = Math.tan(Math.PI*Math.min(freq, 0.4999*sr)/sr);

		const slope = Math.round(p.lcSlope), lc = p.lcFreq;
		const loCut = [
			() => onePole(lc, sr, t, 1, -1),
			() => svf(highpass(lc, butterworthQ, sr), t),
			() => mul(onePole(lc, sr, t, 1, -1), svf(highpass(lc, 1, sr), t)),
			() => mul(svf(highpass(lc, 0.5411961001461970, sr), t), svf(highpass(lc, 1.3065629648763766, sr), t)),
		][slope]();
		const hiCut = Math.round(p.hcSlope) === 0 ? onePole(p.hcFreq, sr, t, 0, 1) : svf(lowpass(p.hcFreq, butterworthQ, sr), t);
		const half = Math.pow(10, (p.tiltOn >= 0.5 ? p.tiltGain : 0)/40);
		const tilt = onePole(p.tiltFreq, sr, t, half, 1/half - half);

		const result = {
			lc: p.lcOn >= 0.5 ? loCut : ONE,
			hc: p.hcOn >= 0.5 ? hiCut : ONE,
			tilt,
		};
		for (const band of BANDS) result[band.id] = svf(bandFilter(band, p, sr), t);

		const solo = Math.round(p.solo);
		if (solo > 0) {
			const band = BANDS[solo - 1], f = p[band.freq];
			const shelf = band.shelf && p[band.type] < 0.5;
			result.solo = svf(shelf ? (band.shelf === 'low' ? lowpass(f, shelfQ, sr) : highpass(f, shelfQ, sr)) : bandpass(f, p[band.q], sr), t);
		}
		return result;
	}

	// Total response in dB (what the plugin does to a sine wave at `freq`)
	function responseDb(p, sampleRate, freq) {
		const s = sections(p, sampleRate, freq);
		const input = mul(s.lc, s.hc, s.tilt);
		if (s.solo) return toDb(mul(input, s.solo));
		const eq = p.eqOn >= 0.5 ? mul(...BANDS.map(band => s[band.id])) : ONE;
		return toDb(mul(input, eq));
	}

	// Parts on their own, in dB: the input filters together, and each band
	function sectionsDb(p, sampleRate, freq) {
		const s = sections(p, sampleRate, freq);
		const result = {input: toDb(mul(s.lc, s.hc, s.tilt))};
		for (const band of BANDS) result[band.id] = toDb(s[band.id]);
		return result;
	}

	const api = {responseDb, sectionsDb, BANDS};
	if (typeof module === 'object' && module.exports) module.exports = api;
	else root.EqResponse = api;
})(this);
