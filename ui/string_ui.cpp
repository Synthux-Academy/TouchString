#include "string_ui.h"
#include "daisysp.h"
#include "config.h"

using namespace synthux;
using namespace daisy;
using namespace daisysp;

float norm(const uint8_t value) {
    return static_cast<float>(value) / 127.f;
};

void StringUI::Init(daisy::DaisySeed& hw) {
    // Callbacks ................................................
    using namespace std::placeholders;
    auto on_touch = std::bind(&StringUI::_on_pad_touch, this, _1);
    auto on_release = std::bind(&StringUI::_on_pad_release, this, _1);
    _touch.pads().SetOnTouch(on_touch);
    _touch.pads().SetOnRelease(on_release);

    _pattern_value.Set(1.f);
    _shift_value.Set(0.f);
    _drive_value.Set(0.f);
    _in_vol_value.Set(0.f);

    _octave_shift = 0;
    _update_octave();

    // Initialize MIDI //////////////////////////////////////////
    /////////////////////////////////////////////////////////////
    #ifdef USB_MIDI
    daisy::MidiUsbHandler::Config midi_cfg;
    _midi.Init(midi_cfg);
    #endif
};

void StringUI::Process(DaisySeed& hw) {
    #ifdef USB_MIDI
    _process_midi();
    #endif

    // Process touch ////////////////////////////////////////////
    /////////////////////////////////////////////////////////////
    _touch.Process();
    _is_to_touched = _touch.pads().IsTouched(10);
    _is_ch_touched = _touch.pads().IsTouched(11);

    // Baseline Recalibration: Touch both modifier pads (P10 & P11) simultaneously for ~0.5s
    if (_is_to_touched && _is_ch_touched) {
        _recal_counter++;
        if (_recal_counter > 120 && !_recal_latched) {
            _recal_latched = true;
            _touch.pads().Recalibrate();
            _trigger_blink_pattern(3, 8, 8); // 3 rapid blinks confirm recalibration
        }
    } else {
        _recal_counter = 0;
        _recal_latched = false;
    }

    if (_is_first_run) {
        _is_first_run = false;
        auto initial_s36 = _touch.knobs().s36().Process();
        _drive_value.SetTracking(initial_s36);
        _string.SetDrive(initial_s36);

        auto initial_s33 = _touch.knobs().s33().Process();
        _pattern_value.SetTracking(initial_s33);
        _string.SetPattern(initial_s33);

        auto initial_s35 = _touch.knobs().s35().Process();
        _human_string_value.SetTracking(initial_s35);
        _string.SetHumanStringChance(initial_s35);
    }

    // Arpeggiator (Switch 2 / Right switch) .....................
    auto switch_value = _touch.switches().A();

    // Exciter mode (Switch 1 / Left switch) .....................
    auto exciter_switch = _touch.switches().B();
    if (exciter_switch == daisy::Switch3::POS_UP) {
        _exciter_mode = 0; // Pluck
    } else if (exciter_switch == daisy::Switch3::POS_DOWN) {
        _exciter_mode = 2; // Bow
    } else {
        _exciter_mode = 1; // Pluck + Bow on hold
    }
    _string.SetExciterMode(_exciter_mode);

    // Arpeggiator (Switch 2 / Right switch) .....................
    bool is_arp_on = switch_value != daisy::Switch3::POS_DOWN;
    bool arp_just_enabled = is_arp_on && !_was_arp_on;
    _was_arp_on = is_arp_on;

    _string.SetLatch(switch_value == daisy::Switch3::POS_UP);
    _string.SetArpOn(is_arp_on);

    if (arp_just_enabled) {
        for (uint8_t i = 0; i < 7; i++) {
            uint16_t p_idx = i + kFirstNotePad;
            if (_touch.pads().IsTouched(p_idx)) {
                _string.NoteOn(i, _touch.pads().Velocity(p_idx));
            }
        }
    }

    // String Dampen Pad (P01)
    bool p01_touched = _touch.pads().IsTouched(1);
    bool ch_touched = _is_ch_touched || _touch.pads().IsTouched(11);
    if (p01_touched && !ch_touched) {
        float p01_press = _touch.pads().Pressure(1);
        _string.SetDampenPad(true, p01_press);
    } else {
        _string.SetDampenPad(false, 0.0f);
    }

    // Continuous pressure handling for bowing / hold ...........
    if (!ch_touched) {
        int8_t active_mono = _string.IsMono() ? _string.ActiveMonoPad() : -1;
        float mono_bow_p = 0.0f;
        float active_mono_press = 0.0f;

        for (uint8_t i = 0; i < 7; i++) {
            uint16_t p_idx = i + kFirstNotePad;
            bool touched = _touch.pads().IsTouched(p_idx);
            float press = touched ? _touch.pads().Pressure(p_idx) : 0.0f;
            _string.SetPadPressure(i, press);

            float voice_bow_p = 0.0f;
            if (_exciter_mode == 2) {
                if (!is_arp_on) {
                    voice_bow_p = press;
                }
            } else if (_exciter_mode == 1) {
                if (touched) {
                    _hold_ticks[i]++;
                    if (_hold_ticks[i] > 5 && press > 0.08f) {
                        float bow_amt = (press - 0.08f) / 0.92f;
                        voice_bow_p = daisysp::fclamp(bow_amt * 0.70f, 0.0f, 0.85f);
                    }
                } else {
                    _hold_ticks[i] = 0;
                }
            }

            if (!_string.IsMono()) {
                if (!is_arp_on || _exciter_mode == 1) {
                    _string.SetVoicePressure(i, voice_bow_p);
                    _string.SetVoiceSustain(i, voice_bow_p > 0.001f);
                }
                if (_exciter_mode == 0) {
                    _string.SetVoiceAftertouch(i, press);
                }
            } else {
                if (active_mono == i) {
                    active_mono_press = press;
                    mono_bow_p = voice_bow_p;
                }
            }
        }

        if (_string.IsMono()) {
            if (!is_arp_on || _exciter_mode == 1) {
                _string.SetVoicePressure(0, mono_bow_p);
                _string.SetVoiceSustain(0, mono_bow_p > 0.001f);
            }
            _string.SetVoiceAftertouch(0, (_exciter_mode == 0 && active_mono >= 0) ? active_mono_press : 0.0f);
        }
    }

    // Pitch (real-time) ..........................................
    _string.SetTransp(_touch.knobs().s31().Process());

    // Timbre (sample-and-hold at pluck time) .....................
    _string.SetDamping(_touch.knobs().s37().Process());
    _string.SetStructure(_touch.knobs().s32().Process());
    _string.SetBrightness(_touch.knobs().s30().Process());

    // Note randomization (arp only) ..............................
    _string.SetHumanNoteChance(_touch.knobs().s34().Process());

    // Human string chance <-> reverb mix .........................
    auto human_verb_knob_value = _touch.knobs().s35().Process();
    auto verb_stage = _verb_value.Process(human_verb_knob_value, _is_to_touched);
    auto human_string_raw = _human_string_value.Process(human_verb_knob_value, !_is_to_touched);
    _string.SetHumanStringChance(human_string_raw);
    _string.SetReverbMix(verb_stage);

    // Pattern density <-> pattern shift ..........................
    auto pattern_shift_value = _touch.knobs().s33().Process();
    auto pattern_amt = _pattern_value.Process(pattern_shift_value, !_is_to_touched);
    auto shift_amt = _shift_value.Process(pattern_shift_value, _is_to_touched);
    _string.SetPattern(pattern_amt);
    _string.SetPatternShift(shift_amt);

    // Drive / volume compensation <-> External Input Volume (shifted with TO) ....
    auto drive_fader_value = _touch.knobs().s36().Process();
    auto drive_amt = _drive_value.Process(drive_fader_value, !_is_to_touched);
    auto in_vol_amt = _in_vol_value.Process(drive_fader_value, _is_to_touched);
    _string.SetDrive(drive_amt);
    _string.SetInputVolume(in_vol_amt);

    if (_string.CheckBeatPulse()) {
        _beat_pulse_timer = 6; // ~24ms beat pulse
    } else if (_beat_pulse_timer > 0) {
        _beat_pulse_timer--;
    }

    bool led_state = false;
    if (_blink_pulses_remaining > 0) {
        if (_blink_is_on) {
            led_state = true;
            if (--_blink_pulse_timer == 0) {
                _blink_is_on = false;
                _blink_pulse_timer = _blink_off_ticks;
                if (_blink_off_ticks == 0) {
                    _blink_pulses_remaining--;
                }
            }
        } else {
            led_state = false;
            if (--_blink_pulse_timer == 0) {
                _blink_pulses_remaining--;
                if (_blink_pulses_remaining > 0) {
                    _blink_is_on = true;
                    _blink_pulse_timer = _blink_on_ticks;
                }
            }
        }
    } else {
        if (_string.IsLatched()) {
            // Latched: Solid ON, dips OFF on each beat pulse
            led_state = (_beat_pulse_timer == 0);
        } else if (is_arp_on) {
            // Momentary Arp: Pulses ON on each beat pulse
            led_state = (_beat_pulse_timer > 0);
        } else {
            led_state = false;
        }
    }
    hw.SetLed(led_state);
};

void StringUI::_on_pad_touch(uint16_t pad) {
    // Scale, Tempo & Octave
    if (pad == 0) {
        if (_is_to_touched) {
            _string.SlowDown();
            _trigger_blink_pattern(1, 8, 0);
        } else if (_is_ch_touched) {
            _prev_scale();
        } else {
            if (_touch.pads().IsTouched(2)) {
                _octave_reset();
            } else {
                _octave_down();
            }
        }
        return;
    }
    if (pad == 2) {
        if (_is_to_touched) {
            _string.SpeedUp();
            _trigger_blink_pattern(1, 8, 0);
        } else if (_is_ch_touched) {
            _next_scale();
        } else {
            if (_touch.pads().IsTouched(0)) {
                _octave_reset();
            } else {
                _octave_up();
            }
        }
        return;
    }
    if (pad == 1) {
        if (_touch.pads().IsTouched(11) || _is_ch_touched) {
            _string.ToggleMono();
            if (_string.IsMono()) {
                _trigger_blink_pattern(2, 10, 10); // Double blink = Mono
            } else {
                _trigger_blink_pattern(1, 28, 0);  // Single long blink = Poly
            }
        }
        return;
    }
    if (pad == 10) {
        uint32_t now = daisy::System::GetNow();
        uint32_t interval = now - _last_to_touch_time;
        _last_to_touch_time = now;
        if (interval >= 250 && interval <= 1500) {
            float bpm = 60000.0f / static_cast<float>(interval);
            _string.SetBpm(bpm);
            _trigger_blink_pattern(1, 12, 0); // instant flash acknowledging tap
        }
        return;
    }
    if (pad == 11) {
        return;
    }

    if (pad < kFirstNotePad || pad >= kFirstNotePad + String::kVoicesCount) return;
    if (_touch.pads().IsTouched(11) || _is_ch_touched) {
        _set_scale(pad - kFirstNotePad);
        return;
    }
    auto note_num = pad - kFirstNotePad;
    // In Bow mode, continuous pressure directly seeds bowing so feather touches swell smoothly from zero
    float vel = (_exciter_mode == 2) ? _touch.pads().Pressure(pad) : _touch.pads().Velocity(pad);
    _hold_ticks[note_num] = 0;
    _string.NoteOn(note_num, vel);
};

void StringUI::_on_pad_release(uint16_t pad) {
    if (pad == 1) {
        _string.SetDampenPad(false, 0.0f);
        return;
    }
    if (pad < kFirstNotePad || pad >= kFirstNotePad + String::kVoicesCount) return;
    auto note_num = pad - kFirstNotePad;
    _hold_ticks[note_num] = 0;
    _string.NoteOff(note_num);
};

#ifdef USB_MIDI
void StringUI::_process_midi() {
    _string.ProcessClockIn(false);
    _midi.Listen();
    while(_midi.HasEvents()) {
        auto msg = _midi.PopEvent();
        switch(msg.type) {
            case SystemRealTime: {
                switch (msg.srt_type) {
                    case TimingClock: {
                        _string.ProcessClockIn(true);
                    }
                    break;
                    default: break;
                }
            }
            break;
            // TODO: unlike Bass, the string engine's NoteOn/NoteOff take a
            // scale-degree index (0..7), not a chromatic MIDI note number,
            // so incoming NoteOn/NoteOff messages aren't forwarded here.
            case ControlChange: {
                auto ctrl_msg = msg.AsControlChange();
                auto num = ctrl_msg.control_number;
                auto norm_value = norm(ctrl_msg.value);
                if (num == 70) { _string.SetBrightness(norm_value); }         // brightness
                else if (num == 71) { _string.SetTransp(norm_value); }       // transpose
                else if (num == 72) { _string.SetStructure(norm_value); }    // structure
                else if (num == 73) { _pattern_value.Set(norm_value); _string.SetPattern(norm_value); }        // pattern
                else if (num == 74) { _string.SetHumanNoteChance(norm_value); } // note randomization
                else if (num == 75) { _human_string_value.Set(norm_value); _string.SetHumanStringChance(norm_value); } // string randomization
                else if (num == 76) { _string.SetDamping(norm_value); }      // damping
                else if (num == 77) { _verb_value.Set(norm_value); _string.SetReverbMix(norm_value); }          // reverb
                else if (num == 78) { _shift_value.Set(norm_value); _string.SetPatternShift(norm_value); }      // pattern shift
                else if (num == 79) { _string.SetDrive(norm_value); }        // drive
                else if (num == 123) { _string.Reset(); }
            }
            break;

            default: break;
        }
    }
};
#endif
