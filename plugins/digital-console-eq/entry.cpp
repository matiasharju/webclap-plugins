// Bundle "digital-console-eq": six-band digital console channel EQ

#include "webclap/entry.h"
#include "webclap/plugin.h"

#include "./digital-console-eq.h"

namespace {

const char *offOn[] = {"Off", "On"};
const char *lcSlopes[] = {"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct"};
const char *hcSlopes[] = {"6 dB/oct", "12 dB/oct"};
const char *bandTypes[] = {"Shelf", "PEQ"};
const char *soloBands[] = {"Off", "L", "1", "2", "3", "4", "H"};

struct DigitalConsoleEqPlugin : public webclap::Plugin {
	static const clap_plugin_descriptor * descriptor() {
		static const char *features[] = {
			CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
			CLAP_PLUGIN_FEATURE_EQUALIZER,
			CLAP_PLUGIN_FEATURE_FILTER,
			CLAP_PLUGIN_FEATURE_STEREO,
			nullptr
		};
		static const clap_plugin_descriptor descriptor{
			.clap_version=CLAP_VERSION_INIT,
			.id="io.github.matiasharju.webclap-plugins.digital-console-eq",
			.name="Digital Console EQ",
			.vendor="WebCLAP Plugins",
			.url="https://github.com/matiasharju/webclap-plugins",
			.manual_url="https://github.com/matiasharju/webclap-plugins",
			.support_url="https://github.com/matiasharju/webclap-plugins/issues",
			.version=WEBCLAP_BUILD,
			.description="Six-band digital console channel EQ, laid out like the Behringer WING channel EQ",
			.features=features
		};
		return &descriptor;
	}

	// Parameter order (indices for paramValue())
	enum {pLcOn, pLcFreq, pLcSlope, pHcOn, pHcFreq, pHcSlope, pTiltOn, pTiltGain, pTiltFreq,
		pEqOn, pMix,
		pLGain, pLFreq, pLQ, pLType,
		p1Gain, p1Freq, p1Q, p2Gain, p2Freq, p2Q, p3Gain, p3Freq, p3Q, p4Gain, p4Freq, p4Q,
		pHGain, pHFreq, pHQ, pHType,
		pSolo};
	// First parameter (gain) of each band; freq and Q follow it
	static constexpr int bandParams[digital_console_eq::bandCount] = {pLGain, p1Gain, p2Gain, p3Gain, p4Gain, pHGain};

	DigitalConsoleEqPlugin(const clap_host *host) : Plugin(host, descriptor(), {
		// IDs must never change: saved projects and automation refer to them
		{.id=1, .key="lcOn", .name="Lo Cut", .unit="", .min=0, .max=1, .defaultValue=0, .labels=offOn, .module="Input filter"},
		{.id=2, .key="lcFreq", .name="Lo Cut Freq", .unit="Hz", .min=20, .max=2000, .defaultValue=100, .decimals=0, .module="Input filter"},
		{.id=3, .key="lcSlope", .name="Lo Cut Slope", .unit="", .min=0, .max=3, .defaultValue=1, .labels=lcSlopes, .module="Input filter"},
		{.id=4, .key="hcOn", .name="Hi Cut", .unit="", .min=0, .max=1, .defaultValue=0, .labels=offOn, .module="Input filter"},
		{.id=5, .key="hcFreq", .name="Hi Cut Freq", .unit="Hz", .min=50, .max=20000, .defaultValue=10000, .decimals=0, .module="Input filter"},
		{.id=6, .key="hcSlope", .name="Hi Cut Slope", .unit="", .min=0, .max=1, .defaultValue=1, .labels=hcSlopes, .module="Input filter"},
		{.id=7, .key="tiltOn", .name="Tilt EQ", .unit="", .min=0, .max=1, .defaultValue=0, .labels=offOn, .module="Input filter"},
		{.id=8, .key="tiltGain", .name="Tilt Gain", .unit="dB", .min=-6, .max=6, .defaultValue=0, .module="Input filter"},
		{.id=9, .key="tiltFreq", .name="Tilt Freq", .unit="Hz", .min=100, .max=10000, .defaultValue=1000, .decimals=0, .module="Input filter"},
		{.id=10, .key="eqOn", .name="EQ", .unit="", .min=0, .max=1, .defaultValue=1, .labels=offOn, .module="EQ"},
		{.id=11, .key="mix", .name="Mix", .unit="%", .min=0, .max=125, .defaultValue=100, .decimals=0, .module="EQ"},

		{.id=20, .key="lGain", .name="L Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="Band L"},
		{.id=21, .key="lFreq", .name="L Freq", .unit="Hz", .min=20, .max=20000, .defaultValue=80, .decimals=0, .module="Band L"},
		{.id=22, .key="lQ", .name="L Q", .unit="", .min=0.44, .max=10, .defaultValue=1, .decimals=2, .module="Band L"},
		{.id=23, .key="lType", .name="L Type", .unit="", .min=0, .max=1, .defaultValue=0, .labels=bandTypes, .module="Band L"},

		{.id=30, .key="b1Gain", .name="1 Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="Band 1"},
		{.id=31, .key="b1Freq", .name="1 Freq", .unit="Hz", .min=20, .max=20000, .defaultValue=200, .decimals=0, .module="Band 1"},
		{.id=32, .key="b1Q", .name="1 Q", .unit="", .min=0.44, .max=10, .defaultValue=1, .decimals=2, .module="Band 1"},

		{.id=40, .key="b2Gain", .name="2 Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="Band 2"},
		{.id=41, .key="b2Freq", .name="2 Freq", .unit="Hz", .min=20, .max=20000, .defaultValue=600, .decimals=0, .module="Band 2"},
		{.id=42, .key="b2Q", .name="2 Q", .unit="", .min=0.44, .max=10, .defaultValue=1, .decimals=2, .module="Band 2"},

		{.id=50, .key="b3Gain", .name="3 Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="Band 3"},
		{.id=51, .key="b3Freq", .name="3 Freq", .unit="Hz", .min=20, .max=20000, .defaultValue=1500, .decimals=0, .module="Band 3"},
		{.id=52, .key="b3Q", .name="3 Q", .unit="", .min=0.44, .max=10, .defaultValue=1, .decimals=2, .module="Band 3"},

		{.id=60, .key="b4Gain", .name="4 Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="Band 4"},
		{.id=61, .key="b4Freq", .name="4 Freq", .unit="Hz", .min=20, .max=20000, .defaultValue=4000, .decimals=0, .module="Band 4"},
		{.id=62, .key="b4Q", .name="4 Q", .unit="", .min=0.44, .max=10, .defaultValue=1, .decimals=2, .module="Band 4"},

		{.id=70, .key="hGain", .name="H Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="Band H"},
		{.id=71, .key="hFreq", .name="H Freq", .unit="Hz", .min=20, .max=20000, .defaultValue=12000, .decimals=0, .module="Band H"},
		{.id=72, .key="hQ", .name="H Q", .unit="", .min=0.44, .max=10, .defaultValue=1, .decimals=2, .module="Band H"},
		{.id=73, .key="hType", .name="H Type", .unit="", .min=0, .max=1, .defaultValue=0, .labels=bandTypes, .module="Band H"},

		{.id=100, .key="solo", .name="Band Solo", .unit="", .min=0, .max=6, .defaultValue=0, .labels=soloBands, .module="EQ"},
	}) {
		uiWidth = 760;
		uiHeight = 520;
	}

	digital_console_eq::DigitalConsoleEq eq;

	digital_console_eq::Settings settings() const {
		digital_console_eq::Settings s;
		s.lcOn = paramValue(pLcOn) >= 0.5;
		s.lcFreq = paramValue(pLcFreq);
		s.lcSlope = int(paramValue(pLcSlope));
		s.hcOn = paramValue(pHcOn) >= 0.5;
		s.hcFreq = paramValue(pHcFreq);
		s.hcSlope = int(paramValue(pHcSlope));
		s.tiltOn = paramValue(pTiltOn) >= 0.5;
		s.tiltGain = paramValue(pTiltGain);
		s.tiltFreq = paramValue(pTiltFreq);
		s.eqOn = paramValue(pEqOn) >= 0.5;
		s.mix = paramValue(pMix);
		for (int b = 0; b < digital_console_eq::bandCount; ++b) {
			auto &band = s.bands[b];
			band.gain = paramValue(bandParams[b]);
			band.freq = paramValue(bandParams[b] + 1);
			band.q = paramValue(bandParams[b] + 2);
		}
		s.bands[0].parametric = paramValue(pLType) >= 0.5;
		s.bands[digital_console_eq::bandCount - 1].parametric = paramValue(pHType) >= 0.5;
		s.solo = int(paramValue(pSolo));
		return s;
	}

	void prepare(double sampleRate, uint32_t maxFrames) override {
		eq.prepare(sampleRate);
	}
	void reset() override {
		eq.reset(settings());
	}
	void processAudio(const float *inL, const float *inR, float *outL, float *outR, uint32_t frames) override {
		eq.process(settings(), inL, inR, outL, outR, frames);
	}
};

} // namespace

WEBCLAP_BUNDLE(DigitalConsoleEqPlugin)
