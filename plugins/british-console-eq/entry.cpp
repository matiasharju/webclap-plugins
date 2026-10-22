// Bundle "british-console-eq": British-console-style channel EQ

#include "webclap/entry.h"
#include "webclap/plugin.h"

#include "./british-console-eq.h"

namespace {

const char *offOn[] = {"Off", "On"};
const char *outIn[] = {"Out", "In"};

struct ConsoleEqPlugin : public webclap::Plugin {
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
			.id="io.github.matiasharju.webclap-plugins.british-console-eq",
			.name="British Console EQ",
			.vendor="WebCLAP Plugins",
			.url="https://github.com/matiasharju/webclap-plugins",
			.manual_url="https://github.com/matiasharju/webclap-plugins",
			.support_url="https://github.com/matiasharju/webclap-plugins/issues",
			.version=WEBCLAP_BUILD,
			.description="British-console-style channel EQ: filters, LF and HF shelves, LMF and HMF with proportional Q",
			.features=features
		};
		return &descriptor;
	}

	// Parameter order (indices for paramValue())
	enum {pHpfOn, pHpfFreq, pLpfOn, pLpfFreq,
		pLfGain, pLfFreq, pLmfGain, pLmfFreq, pLmfQ, pHmfGain, pHmfFreq, pHmfQ, pHfGain, pHfFreq,
		pEqIn};

	ConsoleEqPlugin(const clap_host *host) : Plugin(host, descriptor(), {
		// IDs must never change or be reused: saved projects and automation refer to them.
		// Retired: 12 (HF Shape), 42 (LF Shape), 51 (Q Mode), 52 (Output), 53 (Mix)
		{.id=1, .key="hpfOn", .name="HPF", .unit="", .min=0, .max=1, .defaultValue=0, .labels=offOn, .module="Filters"},
		{.id=2, .key="hpfFreq", .name="HPF Freq", .unit="Hz", .min=16, .max=350, .defaultValue=80, .decimals=0, .module="Filters"},
		{.id=3, .key="lpfOn", .name="LPF", .unit="", .min=0, .max=1, .defaultValue=0, .labels=offOn, .module="Filters"},
		{.id=4, .key="lpfFreq", .name="LPF Freq", .unit="Hz", .min=3000, .max=22000, .defaultValue=12000, .decimals=0, .module="Filters"},
		{.id=40, .key="lfGain", .name="LF Gain", .unit="dB", .min=-20, .max=20, .defaultValue=0, .module="LF"},
		{.id=41, .key="lfFreq", .name="LF Freq", .unit="Hz", .min=30, .max=450, .defaultValue=200, .decimals=0, .module="LF"},
		{.id=30, .key="lmfGain", .name="LMF Gain", .unit="dB", .min=-20, .max=20, .defaultValue=0, .module="LMF"},
		{.id=31, .key="lmfFreq", .name="LMF Freq", .unit="Hz", .min=120, .max=2500, .defaultValue=1000, .decimals=0, .module="LMF"},
		{.id=32, .key="lmfQ", .name="LMF Q", .unit="", .min=0.1, .max=3.5, .defaultValue=0.8, .decimals=2, .module="LMF"},
		{.id=20, .key="hmfGain", .name="HMF Gain", .unit="dB", .min=-20, .max=20, .defaultValue=0, .module="HMF"},
		{.id=21, .key="hmfFreq", .name="HMF Freq", .unit="Hz", .min=600, .max=7000, .defaultValue=3000, .decimals=0, .module="HMF"},
		{.id=22, .key="hmfQ", .name="HMF Q", .unit="", .min=0.1, .max=3.5, .defaultValue=0.8, .decimals=2, .module="HMF"},
		{.id=10, .key="hfGain", .name="HF Gain", .unit="dB", .min=-20, .max=20, .defaultValue=0, .module="HF"},
		{.id=11, .key="hfFreq", .name="HF Freq", .unit="Hz", .min=1500, .max=16000, .defaultValue=8000, .decimals=0, .module="HF"},
		{.id=50, .key="eqIn", .name="EQ In", .unit="", .min=0, .max=1, .defaultValue=1, .labels=outIn, .module="Master"},
	}) {
		uiWidth = 850;
		uiHeight = 374;
	}

	console_eq::ConsoleEq eq;

	console_eq::Settings settings() const {
		console_eq::Settings s;
		s.hpfOn = paramValue(pHpfOn) >= 0.5;
		s.hpfFreq = paramValue(pHpfFreq);
		s.lpfOn = paramValue(pLpfOn) >= 0.5;
		s.lpfFreq = paramValue(pLpfFreq);
		s.lfGain = paramValue(pLfGain);
		s.lfFreq = paramValue(pLfFreq);
		s.lmfGain = paramValue(pLmfGain);
		s.lmfFreq = paramValue(pLmfFreq);
		s.lmfQ = paramValue(pLmfQ);
		s.hmfGain = paramValue(pHmfGain);
		s.hmfFreq = paramValue(pHmfFreq);
		s.hmfQ = paramValue(pHmfQ);
		s.hfGain = paramValue(pHfGain);
		s.hfFreq = paramValue(pHfFreq);
		s.eqIn = paramValue(pEqIn) >= 0.5;
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

WEBCLAP_BUNDLE(ConsoleEqPlugin)
