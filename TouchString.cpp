#include "daisy_seed.h"
#include "touch/touch.h"
#include "string/string.h"
#include "ui/string_ui.h"
#include "log.h"

using namespace daisy;
using namespace synthux;

DaisySeed hw;

Touch touch;
String engine;
StringUI ui(touch, engine);


void AudioCallback(
	AudioHandle::InputBuffer in, 
	AudioHandle::OutputBuffer out, 
	size_t size) {
	engine.Process(in, out, size);
};

int main(void) {
	hw.Init();
	// 48-sample block size (~1.0ms @ 48kHz) optimizes CPU interrupt overhead
	// for 7-voice physical modeling waveguides while preserving sub-millisecond touch latency.
	hw.SetAudioBlockSize(48);
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

	#if DEBUG
	HW::hw().setHW(&hw);
	HW::hw().startLog();
	#endif

	touch.Init(hw);
	// Warm up MPR121 baseline tracking filter so capacitance settles before processing touch events
	for (int i = 0; i < 40; i++) {
		touch.Process();
		System::Delay(3);
	}
	engine.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
	ui.Init(hw);

	hw.StartAudio(AudioCallback);

	while(1) {
		ui.Process(hw);
		System::Delay(4);
	}
};
