#include "string.h"
#include <functional>

using namespace synthux;

String::String():
_trigger              { Trigger(kPPQN) },
_dice                 { std::uniform_int_distribution<uint8_t>(0, 100) },
_tempo                { .45f },
_brightness           { 0.f },
_structure            { 0.f },
_damping              { 0.f },
_human_note_chance    { 0 },
_human_string_chance  { 0 },
_volume               { 1.f },
_soft_scale           { 1.f },
_is_arp_on            { false },
_exciter_mode         { 0 },
_input_volume         { 0.f },
_trans_mult           { 1.f },
_is_mono              { false },
_mono_stack_size      { 0 },
_current_oct_mult     { 1.f },
_is_dampen_active     { false },
_dampen_pressure      { 0.f },
_clock_tick_counter   { 0 },
_beat_pulse           { false }
{
  _bright_offset.fill(0.f);
  _struct_offset.fill(0.f);
  _damp_offset.fill(0.f);
  _mono_stack.fill(0);
  _voice_oct_mult.fill(1.f);
  _note_on.fill(false);
  _note_hold.fill(false);
  _pad_pressure.fill(0.f);
  _reverb_in.fill(0.f);
  _reverb_out.fill(0.f);
  _bus.fill(0.f);
};

void String::SetMono(const bool mono) {
  if (_is_mono != mono) {
    _is_mono = mono;
    _mono_stack_size = 0;
    for (auto& v : _vox) {
      v.Reset();
    }
  }
}

void String::Init(const float sample_rate, const float buffer_size) {
  using namespace std::placeholders;

  _clock.Init(1e6 * buffer_size / sample_rate, kPPQNExtern, kPPQN);
  auto on_clock = std::bind(&String::_on_clock_tick, this);
  _clock.SetOnTick(on_clock);

  auto on_latch_note_on = std::bind(&String::_on_latch_note_on, this, _1);
  auto on_latch_note_off = std::bind(&String::_on_latch_note_off, this, _1);
  _latch.set_on_note_on(on_latch_note_on);
  _latch.set_on_note_off(on_latch_note_off);

  auto on_arp_note_on = std::bind(&String::_on_arp_note_on, this, _1, _2);
  auto on_arp_note_off = std::bind(&String::_on_arp_note_off, this, _1);
  _arp.SetOnNoteOn(on_arp_note_on);
  _arp.SetOnNoteOff(on_arp_note_off);
  
  _arp.SetDirection(ArpDirection::fwd);
  _arp.SetRandChance(0);
  _arp.SetAsPlayed(true);
  _arp.SetNonLegato(true);

  for (size_t i = 0; i < kVoicesCount; i++) {
    _vox[i].Init(sample_rate, _scale.FreqAt(i), 123456789u + static_cast<uint32_t>(i) * 987654321u);
  }

  _drive.Init();

  _body_filter_l.Init(sample_rate);
  _body_filter_r.Init(sample_rate);
  _body_filter_l.SetFreq(310.0f);
  _body_filter_l.SetRes(0.40f);
  _body_filter_r.SetFreq(310.0f);
  _body_filter_r.SetRes(0.40f);

  _dc_block_l.Init(sample_rate);
  _dc_block_r.Init(sample_rate);

  _reverb.Init(sample_rate);
  _reverb.SetFeedback(kReverbFeedback);
  _reverb.SetLpFreq(kReverLPFreq);

  SetTempo(_tempo);
};

void String::SetLatch(const bool on) {
    _latch.set_on(on);
    if (_is_arp_on && !_arp.HasNote()) Reset();
};

void String::SetBpm(const float bpm) {
  _clock.SetBpm(bpm);
  const auto clock_off_offset = 10.0f;
  _tempo = (daisysp::fclamp(bpm, 40.0f, 240.0f) - 40.0f + clock_off_offset) / (200.0f - clock_off_offset);
  _tempo = daisysp::fclamp(_tempo, 0.05f, 1.0f);
  _clock_tick_counter = 0;
  _beat_pulse = true;
}

void String::SetScaleIndex(const uint8_t index) {
  _scale.SetScaleIndex(index);
  for (size_t i = 0; i < kVoicesCount; i++) {
    _vox[i].SetFreq(_scale.FreqAt(i) * _voice_oct_mult[i]);
  }
}

void String::SetTransp(const float value) {
  _trans_mult = _scale.TransMult(value);
  for (size_t i = 0; i < kVoicesCount; i++) {
    _vox[i].SetMult(_trans_mult);
  }
}

void String::_update_damping() {
  float base_d = _damping * (0.25f + 0.75f * _soft_scale);
  if (_is_dampen_active) {
    float choke = daisysp::fclamp(1.0f - 1.25f * _dampen_pressure, 0.001f, 1.0f);
    float eff_damping = base_d * choke;
    for (size_t i = 0; i < kVoicesCount; i++) {
      _vox[i].SetDamping(daisysp::fclamp(eff_damping + _damp_offset[i], 0.f, 1.f));
    }
  } else {
    for (size_t i = 0; i < kVoicesCount; i++) {
      _vox[i].SetDamping(daisysp::fclamp(base_d + _damp_offset[i], 0.f, 1.f));
    }
  }
}

void String::SetDampenPad(const bool active, const float pressure) {
  if (active == _is_dampen_active && !active) return;
  _is_dampen_active = active;
  _dampen_pressure = active ? pressure : 0.0f;
  _update_damping();
  if (active && pressure > 0.40f) {
    for (auto& v : _vox) {
      v.SetBowPressure(0.0f);
      v.SetSustain(false);
    }
  }
}

void String::SetBrightness(const float value) {
  _brightness = value;
  for (size_t i = 0; i < kVoicesCount; i++) {
    _vox[i].SetBrightness(daisysp::fclamp(value + _bright_offset[i], 0.f, 1.f));
  }
}

void String::SetStructure(const float value) {
  _structure = value;
  for (size_t i = 0; i < kVoicesCount; i++) {
    _vox[i].SetStructure(daisysp::fclamp(value + _struct_offset[i], 0.f, 1.f));
  }
}

void String::SetDamping(const float value) {
  _damping = value * .7f;
  _update_damping();
}

void String::SetVoicePressure(const uint8_t voice_num, const float pressure) {
  if (voice_num < kVoicesCount) {
    float eff_p = pressure;
    if (_is_dampen_active) {
      eff_p *= daisysp::fclamp(1.0f - 1.4f * _dampen_pressure, 0.0f, 1.0f);
    }
    _vox[voice_num].SetBowPressure(eff_p);
  }
}

void String::SetVoiceSustain(const uint8_t voice_num, const bool sustain) {
  if (voice_num < kVoicesCount) {
    _vox[voice_num].SetSustain(sustain);
  }
}

void String::SetVoiceAftertouch(const uint8_t voice_num, const float pressure) {
  if (voice_num < kVoicesCount) {
    _vox[voice_num].SetAftertouch(pressure);
  }
}

void String::SetBowPressure(const float pressure) {
  for (auto& v : _vox) {
    v.SetBowPressure(pressure);
  }
}

void String::SetSustain(const bool sustain) {
  for (auto& v : _vox) {
    v.SetSustain(sustain);
  }
}

void String::SetNoteFreq(const uint8_t note_num) {
  if (note_num < kVoicesCount) {
    _vox[note_num].SetFreq(_scale.FreqAt(note_num));
  }
}

void String::NoteOn(const uint8_t num, const float velocity) {
  if (num < kVoicesCount) {
    float eff_vel = velocity * (0.15f + 0.85f * _soft_scale);
    _pad_pressure[num] = eff_vel;
    if (!_is_arp_on) {
      if (_is_mono) {
        // Manage mono note stack (last-note priority)
        bool already_in = false;
        for (uint8_t i = 0; i < _mono_stack_size; i++) {
          if (_mono_stack[i] == num) {
            for (uint8_t j = i; j + 1 < _mono_stack_size; j++) {
              _mono_stack[j] = _mono_stack[j + 1];
            }
            _mono_stack[_mono_stack_size - 1] = num;
            already_in = true;
            break;
          }
        }
        if (!already_in && _mono_stack_size < kVoicesCount) {
          _mono_stack[_mono_stack_size++] = num;
        }

        uint8_t target_pad = _mono_stack[_mono_stack_size - 1];
        _voice_oct_mult[target_pad] = _current_oct_mult;
        float freq = _scale.FreqAt(target_pad) * _current_oct_mult;
        _vox[0].SetMult(_trans_mult, false);
        _humanize_and_apply(0);

        if (_exciter_mode == 2) {
          // Bow mode: legato portamento transition on single centered voice
          _vox[0].SetFreq(freq);
          _vox[0].SetBowPressure(eff_vel);
        } else if (_exciter_mode == 1) {
          // Pluck + Bow on hold
          _vox[0].NoteOn(freq, eff_vel);
        } else {
          // Pure Pluck mode: strike new note cleanly on single voice
          _vox[0].SetSustain(false);
          _vox[0].SetBowPressure(0.0f);
          _vox[0].NoteOn(freq, eff_vel);
        }
        return;
      } else {
        // Poly mode
        _voice_oct_mult[num] = _current_oct_mult;
        float freq = _scale.FreqAt(num) * _current_oct_mult;
        _vox[num].SetMult(_trans_mult, false);
        _humanize_and_apply(num);
        if (_exciter_mode == 2) {
          _vox[num].SetFreq(freq);
          _vox[num].SetBowPressure(eff_vel);
        } else {
          _vox[num].SetSustain(false);
          _vox[num].SetBowPressure(0.0f);
          _vox[num].NoteOn(freq, eff_vel);
        }
        return;
      }
    } else {
      _voice_oct_mult[num] = _current_oct_mult;
    }
  }

  _latch.note_on(num);

  if (_arp.HasNote()) {
    if (!_clock.IsRunning()) _clock.Run();
  }
  else {
    Reset();
  }
};

void String::NoteOff(const uint8_t num) {
  if (num < kVoicesCount) {
    _pad_pressure[num] = 0.0f;
    _vox[num].SetAftertouch(0.0f);
    if (!_is_arp_on) {
      if (_is_mono) {
        // Remove num from mono stack
        for (uint8_t i = 0; i < _mono_stack_size; i++) {
          if (_mono_stack[i] == num) {
            for (uint8_t j = i; j + 1 < _mono_stack_size; j++) {
              _mono_stack[j] = _mono_stack[j + 1];
            }
            _mono_stack_size--;
            break;
          }
        }

        if (_mono_stack_size > 0) {
          // Legato return to top of stack
          uint8_t prev_pad = _mono_stack[_mono_stack_size - 1];
          float prev_freq = _scale.FreqAt(prev_pad) * _voice_oct_mult[prev_pad];
          float prev_press = _pad_pressure[prev_pad];
          if (prev_press < 0.2f) prev_press = 0.6f;

          if (_exciter_mode == 2) {
            // Bow mode: legato glide to remaining held note
            _vox[0].SetFreq(prev_freq);
            _vox[0].SetBowPressure(prev_press);
          } else if (_exciter_mode == 1) {
            // Pluck+Bow mode: if already bowing, glide pitch; do NOT re-pluck!
            if (_vox[0].IsBowing()) {
              _vox[0].SetFreq(prev_freq);
              _vox[0].SetBowPressure(prev_press * 0.65f);
            }
          }
          // In pure Pluck mode (mode 0): never re-pluck on release!
        } else {
          // Stack empty - silence bow/sustain
          _vox[0].SetSustain(false);
          _vox[0].SetBowPressure(0.0f);
          _vox[0].SetAftertouch(0.0f);
        }
        return;
      } else {
        // Poly mode
        _vox[num].SetSustain(false);
        _vox[num].SetBowPressure(0.0f);
      }
    }
  }

  if (_is_arp_on) {
    _latch.note_off(num);
    if (!_arp.HasNote()) {
      Reset();
    }
  }
};

void String::Reset() {
  if (!_is_arp_on) {
    _clock.Stop();
  }
  _trigger.Reset();
  _pattern.Reset();
  _arp.Clear();
  _latch.clear();
  _mono_stack_size = 0;
  _voice_oct_mult.fill(_current_oct_mult);
  _bright_offset.fill(0.f);
  _struct_offset.fill(0.f);
  _damp_offset.fill(0.f);
  for (auto& v : _vox) {
    v.SetSustain(false);
    v.SetBowPressure(0.0f);
    v.SetAftertouch(0.0f);
  }
};

void String::Process(const float * const *in, float **out, size_t size) {
  _clock.Tick();
  for (size_t i = 0; i < size; i++) {
    float ext_audio = 0.f;
    if (in != nullptr && _input_volume > 0.005f) {
      ext_audio = (in[0][i] + in[1][i]) * 0.5f * _input_volume;
      ext_audio = daisysp::SoftLimit(ext_audio * 1.5f);
    }

    float sum_l = 0.f;
    float sum_r = 0.f;
    if (_is_mono) {
      float s = _vox[0].Process(ext_audio * 0.40f);
      sum_l = s * 0.7071f;
      sum_r = s * 0.7071f;
    } else {
      for (size_t v = 0; v < kVoicesCount; v++) {
        float s = _vox[v].Process(ext_audio * 0.40f);
        sum_l += s * kPanL[v];
        sum_r += s * kPanR[v];
      }
      sum_l *= 0.65f;
      sum_r *= 0.65f;
    }

    // Acoustic wooden body soundboard formant resonance (~310 Hz)
    _body_filter_l.Process(sum_l);
    _body_filter_r.Process(sum_r);
    sum_l = sum_l * 0.76f + _body_filter_l.Band() * 0.24f;
    sum_r = sum_r * 0.76f + _body_filter_r.Band() * 0.24f;

    sum_l = _dc_block_l.Process(sum_l);
    sum_r = _dc_block_r.Process(sum_r);

    _bus[0] = _drive.Process(sum_l) * _volume;
    _bus[1] = _drive.Process(sum_r) * _volume;
    _xfade.Process(0, 0, _bus[0], _bus[1], _reverb_in[0], _reverb_in[1]);
    _reverb.Process(_reverb_in[0], _reverb_in[1], &(_reverb_out[0]), &(_reverb_out[1]));
    out[0][i] = daisysp::SoftLimit(_bus[0] + _reverb_out[0]);
    out[1][i] = daisysp::SoftLimit(_bus[1] + _reverb_out[1]);
  }
};

void String::_on_clock_tick() {
  _clock_tick_counter = (_clock_tick_counter + 1) % kPPQN;
  if (_clock_tick_counter == 0) {
    _beat_pulse = true;
  }
  if (_trigger.Tick() && _pattern.Tick()) {
    _arp.Trigger();
  }
};

void String::_on_latch_note_on(uint8_t num) 
{ 
  _arp.NoteOn(num, 127); 
}
void String::_on_latch_note_off(uint8_t num) 
{ 
  _arp.NoteOff(num); 
}

void String::_on_arp_note_on(uint8_t num, uint8_t vel) {
  if (num >= kVoicesCount) return;
  uint8_t voice_idx = _is_mono ? 0 : num;
  if (_is_mono) {
    for (size_t i = 1; i < kVoicesCount; i++) {
      _vox[i].Reset();
    }
  }
  _vox[voice_idx].SetMult(_trans_mult, false);
  auto base_f = _is_arp_on ? _humanized_note_freq(num) : _scale.FreqAt(num);
  auto freq = base_f * _voice_oct_mult[num];
  _humanize_and_apply(voice_idx);
  float p = _pad_pressure[num];

  if (_exciter_mode == 2) {
    // Pure Bow mode: musical, singing stick-slip bowing excitation.
    // Moderate, warm bow pressure (0.35 default when latched, scaling gently 0.24-0.60 with touch)
    // without harsh over-pressing or helicopter chopping noise.
    float bow_p = (p > 0.05f) ? (0.24f + 0.36f * daisysp::fclamp(p, 0.0f, 1.0f)) : 0.35f;
    bow_p *= (0.25f + 0.75f * _soft_scale);
    _vox[voice_idx].SetFreq(freq);
    _vox[voice_idx].SetBowPressure(bow_p);
    _vox[voice_idx].SetSustain(true);
  } else if (_exciter_mode == 1) {
    // Pluck + Bow mode: full pluck strike + warm bowed sustain cushion
    float pluck_vel = (p > 0.05f) ? daisysp::fclamp(sqrtf(p), 0.25f, 1.0f) : 0.85f;
    pluck_vel *= (0.15f + 0.85f * _soft_scale);
    _vox[voice_idx].NoteOn(freq, pluck_vel);
    float bow_p = (p > 0.08f) ? daisysp::fclamp(((p - 0.08f) / 0.92f) * 0.50f, 0.20f, 0.70f) : 0.35f;
    bow_p *= (0.25f + 0.75f * _soft_scale);
    _vox[voice_idx].SetBowPressure(bow_p);
    _vox[voice_idx].SetSustain(true);
  } else {
    // Pure Pluck mode: crisp pluck strike, zero bow pressure
    float pluck_vel = (p > 0.05f) ? daisysp::fclamp(sqrtf(p), 0.25f, 1.0f) : 0.85f;
    pluck_vel *= (0.15f + 0.85f * _soft_scale);
    _vox[voice_idx].SetSustain(false);
    _vox[voice_idx].SetBowPressure(0.0f);
    _vox[voice_idx].NoteOn(freq, pluck_vel);
  }
};

void String::_on_arp_note_off(uint8_t num) {
  if (_is_mono) {
    if (_exciter_mode == 2 || (_exciter_mode == 1 && _pad_pressure[num] > 0.08f)) {
      // In Bow mode or sustained Pluck+Bow when physically holding pad: sustain bow across steps!
      return;
    }
    _vox[0].SetBowPressure(0.0f);
    _vox[0].SetSustain(false);
  } else {
    if (num < kVoicesCount) {
      if (_exciter_mode == 1 && _pad_pressure[num] > 0.08f) {
        // Voice is physically held down by finger: do not kill bow sustain on note-off!
        return;
      }
      _vox[num].SetBowPressure(0.0f);
      _vox[num].SetSustain(false);
    }
  }
}

float String::_humanized_note_freq(uint8_t note) {
  auto freq = _scale.FreqAt(note);
  if (_human_note_chance <= 2) return freq;

  auto human_note_chance_dice = _dice(_rand_engine);
  auto note_dice = _dice(_rand_engine);
  auto octave_dice = _dice(_rand_engine);

  if (_human_note_chance < 33) {
    if (human_note_chance_dice < _human_note_chance) {
      return (octave_dice < 50) ? freq * .5f : freq * 2.f;
    }
    return freq;
  }
  else if (_human_note_chance < 66) {
    if (human_note_chance_dice < _human_note_chance) {
      if (note_dice < 50) freq = _scale.Random();
      if (octave_dice < 25) return freq * .5f;
      else if (octave_dice > 75) return freq * 2.f;
      else return freq;
    }
    return freq;
  }
  else {
    if (note_dice < _human_note_chance) freq = _scale.Random();
    if (octave_dice < 20) return freq * 2.f;
    else if (octave_dice > 80) return freq * .5f;
    return freq; // 60% of freq passes through unchanged
  }
};

void String::_humanize_and_apply(uint8_t voice_num) {
  if (voice_num >= kVoicesCount) return;
  if (_human_string_chance > 2) {
    auto chance_dice = _dice(_rand_engine);
    if (chance_dice < _human_string_chance) {
      auto bright_dice = _dice(_rand_engine);
      auto structure_dice = _dice(_rand_engine);
      auto damping_dice = _dice(_rand_engine);

      // Bipolar variations around center (dice 0..100 -> -50..+50)
      _bright_offset[voice_num] = (static_cast<float>(bright_dice) - 50.f) * 0.003f;
      _struct_offset[voice_num] = (static_cast<float>(structure_dice) - 50.f) * 0.003f;
      _damp_offset[voice_num]   = (static_cast<float>(damping_dice) - 50.f) * 0.004f;
    } else {
      _bright_offset[voice_num] = 0.f;
      _struct_offset[voice_num] = 0.f;
      _damp_offset[voice_num]   = 0.f;
    }
  } else {
    _bright_offset[voice_num] = 0.f;
    _struct_offset[voice_num] = 0.f;
    _damp_offset[voice_num]   = 0.f;
  }
  _vox[voice_num].SetBrightness(daisysp::fclamp(_brightness + _bright_offset[voice_num], 0.f, 1.f));
  _vox[voice_num].SetStructure(daisysp::fclamp(_structure + _struct_offset[voice_num], 0.f, 1.f));
  _vox[voice_num].SetDamping(daisysp::fclamp(_damping + _damp_offset[voice_num], 0.f, 1.f));
};
