#pragma once

// Filters shared by the EQ plugins.
//
// The 2-pole filters are Andy Simper's linear trapezoidal state-variable filters
// (https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf), which stay stable and click-free
// when their settings move quickly (e.g. automation).  The 1-pole filters use the same
// trapezoidal (TPT) integration.
//
// Each plugin's ui/response.js computes the curve with the same formulas, so keep them in sync.

#include <algorithm>
#include <cmath>

namespace webclap { namespace dsp {

constexpr double pi = 3.14159265358979323846;

// ---- 2-pole designs: output = m0*input + m1*bandpass + m2*lowpass ----

struct Coeffs {
	double g, k, m0, m1, m2;
};

inline double prewarp(double freq, double sampleRate) {
	return std::tan(pi*std::min(freq, 0.45*sampleRate)/sampleRate);
}
inline Coeffs bell(double freq, double gainDb, double q, double sampleRate) {
	double A = std::pow(10.0, gainDb/40), k = 1/(q*A);
	return {prewarp(freq, sampleRate), k, 1, k*(A*A - 1), 0};
}
inline Coeffs lowShelf(double freq, double gainDb, double q, double sampleRate) {
	double A = std::pow(10.0, gainDb/40), k = 1/q;
	return {prewarp(freq, sampleRate)/std::sqrt(A), k, 1, k*(A - 1), A*A - 1};
}
inline Coeffs highShelf(double freq, double gainDb, double q, double sampleRate) {
	double A = std::pow(10.0, gainDb/40), k = 1/q;
	return {prewarp(freq, sampleRate)*std::sqrt(A), k, A*A, k*(1 - A)*A, 1 - A*A};
}
inline Coeffs lowpass(double freq, double q, double sampleRate) {
	return {prewarp(freq, sampleRate), 1/q, 0, 0, 1};
}
inline Coeffs highpass(double freq, double q, double sampleRate) {
	return {prewarp(freq, sampleRate), 1/q, 1, -1/q, -1};
}
// 0 dB at the centre frequency
inline Coeffs bandpass(double freq, double q, double sampleRate) {
	return {prewarp(freq, sampleRate), 1/q, 0, 1/q, 0};
}
// Passes everything unchanged (for unused filter stages)
inline Coeffs passThrough() {
	return {0.1, 1, 1, 0, 0};
}

struct Svf {
	double a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
	double ic1[2] = {0, 0}, ic2[2] = {0, 0};

	void set(const Coeffs &c) {
		a1 = 1/(1 + c.g*(c.g + c.k));
		a2 = c.g*a1;
		a3 = c.g*a2;
		m0 = c.m0;
		m1 = c.m1;
		m2 = c.m2;
	}
	double process(int channel, double v0) {
		double &s1 = ic1[channel], &s2 = ic2[channel];
		double v3 = v0 - s2;
		double v1 = a1*s1 + a2*v3;
		double v2 = s2 + a2*s1 + a3*v3;
		s1 = 2*v1 - s1;
		s2 = 2*v2 - s2;
		return m0*v0 + m1*v1 + m2*v2;
	}
	void reset() {
		ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0;
	}
	void flushDenormals() {
		for (double *s : {&ic1[0], &ic1[1], &ic2[0], &ic2[1]}) {
			if (std::abs(*s) < 1e-20) *s = 0;
		}
	}
};

// ---- 1-pole: output = m0*input + m1*lowpass ----

struct OnePole {
	double G = 0, m0 = 1, m1 = 0;
	double state[2] = {0, 0};

	void setLowpass(double freq, double sampleRate) {
		set(freq, sampleRate, 0, 1);
	}
	void setHighpass(double freq, double sampleRate) {
		set(freq, sampleRate, 1, -1);
	}
	// Gain `lowGain` (linear) at DC, `highGain` at the top, crossing over at `freq`
	void setShelf(double freq, double lowGain, double highGain, double sampleRate) {
		set(freq, sampleRate, highGain, lowGain - highGain);
	}
	void setPassThrough() {
		G = 0;
		m0 = 1;
		m1 = 0;
	}
	void set(double freq, double sampleRate, double newM0, double newM1) {
		double g = prewarp(freq, sampleRate);
		G = g/(1 + g);
		m0 = newM0;
		m1 = newM1;
	}
	double process(int channel, double x) {
		double &s = state[channel];
		double v = (x - s)*G;
		double lp = v + s;
		s = lp + v;
		return m0*x + m1*lp;
	}
	void reset() {
		state[0] = state[1] = 0;
	}
	void flushDenormals() {
		for (double &s : state) {
			if (std::abs(s) < 1e-20) s = 0;
		}
	}
};

// Kept for the Console EQ: a 1-pole highpass with `set(freq, sampleRate)`
struct OnePoleHighpass : OnePole {
	void set(double freq, double sampleRate) {
		setHighpass(freq, sampleRate);
	}
};

}} // namespace
