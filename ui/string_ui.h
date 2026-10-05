#pragma once

#include "daisy_seed.h"
#include "../touch/touch.h"
#include "../string/string.h"
#include "config.h"
#include "mvalue.h"
#include <array>
#include <functional>

namespace synthux {

class StringUI {
public:
    StringUI(Touch& touch, String& string):
    _touch { touch },
    _string { string },
    _scale_index { 0 },
    _octave_shift { 0 },
    _exciter_mode { 0 },
    _was_arp_on { false },
    _is_first_run { true },
    _blink_pulses_remaining { 0 },
    _blink_pulse_timer { 0 },
    _blink_on_ticks { 0 },
    _blink_off_ticks { 0 },
    _blink_is_on { false },
    _beat_pulse_timer { 0 },
    _last_to_touch_time { 0 },
    _recal_counter { 0 },
    _recal_latched { false }
     {
         _hold_ticks.fill(0);
     }

    ~StringUI() {}

    void Init(daisy::DaisySeed& hw);
    void Process(daisy::DaisySeed& hw);

private:
    void _trigger_blink_pattern(uint8_t count, uint8_t on_ticks, uint8_t off_ticks) {
        _blink_pulses_remaining = count;
        _blink_on_ticks = on_ticks;
        _blink_off_ticks = off_ticks;
        _blink_is_on = true;
        _blink_pulse_timer = on_ticks;
    }

    void _trigger_scale_blinks(uint8_t scale_idx) {
        // Blink 1 to 7 times for scale 1 to 7 (28ms on, 28ms off per blink)
        _trigger_blink_pattern(scale_idx + 1, 7, 7);
    }

    void _trigger_octave_blinks() {
        if (_octave_shift == 0) {
            // Unison: 1 solid medium confirmation blink (~140ms)
            _trigger_blink_pattern(1, 35, 0);
        } else if (_octave_shift == 1) {
            // +1 Octave: 2 quick crisp blinks
            _trigger_blink_pattern(2, 6, 6);
        } else if (_octave_shift == 2) {
            // +2 Octaves: 3 quick crisp blinks
            _trigger_blink_pattern(3, 6, 6);
        } else if (_octave_shift == -1) {
            // -1 Octave: 1 slow pulse
            _trigger_blink_pattern(1, 16, 12);
        } else if (_octave_shift == -2) {
            // -2 Octaves: 2 slow pulses
            _trigger_blink_pattern(2, 16, 12);
        }
    }

    void _set_scale(uint8_t index) {
        if (index >= _string.ScalesCount()) return;
        _scale_index = index;
        _string.SetScaleIndex(_scale_index);
        _trigger_scale_blinks(_scale_index);
    }

    void _next_scale() {
        _scale_index = (_scale_index + 1) % _string.ScalesCount();
        _string.SetScaleIndex(_scale_index);
        _trigger_scale_blinks(_scale_index);
    }

    void _prev_scale() {
        if (_scale_index == 0) {
            _scale_index = _string.ScalesCount() - 1;
        } else {
            _scale_index--;
        }
        _string.SetScaleIndex(_scale_index);
        _trigger_scale_blinks(_scale_index);
    }

    void _octave_down() {
        if (_octave_shift > -2) {
            _octave_shift--;
            _update_octave();
            _trigger_octave_blinks();
        }
    }

    void _octave_up() {
        if (_octave_shift < 2) {
            _octave_shift++;
            _update_octave();
            _trigger_octave_blinks();
        }
    }

    void _octave_reset() {
        if (_octave_shift != 0) {
            _octave_shift = 0;
            _update_octave();
            _trigger_octave_blinks();
        }
    }

    void _update_octave() {
        static constexpr float kOctaveMults[5] = { 0.25f, 0.50f, 1.00f, 2.00f, 4.00f };
        float mult = kOctaveMults[_octave_shift + 2];
        _string.SetCurrentOctaveMult(mult);
    }

    void _on_pad_touch(uint16_t pad);
    void _on_pad_release(uint16_t pad);

    #ifdef USB_MIDI
    daisy::MidiUsbHandler _midi;
    void _process_midi();
    #endif

    Touch& _touch;
    String& _string;

    MValue _verb_value;
    MValue _human_string_value;
    MValue _pattern_value;
    MValue _shift_value;
    MValue _drive_value;
    MValue _in_vol_value;

    static constexpr uint8_t kNotesCount = 8;
    static constexpr uint16_t kFirstNotePad = 3;

    uint8_t _scale_index;
    int8_t _octave_shift;
    bool _is_to_touched;
    bool _is_ch_touched;
    std::array<uint16_t, 7> _hold_ticks;
    int _exciter_mode;
    bool _was_arp_on;
    bool _is_first_run;
    uint8_t _blink_pulses_remaining;
    uint8_t _blink_pulse_timer;
    uint8_t _blink_on_ticks;
    uint8_t _blink_off_ticks;
    bool _blink_is_on;
    uint8_t _beat_pulse_timer;
    uint32_t _last_to_touch_time;
    uint16_t _recal_counter;
    bool _recal_latched;
};

};
