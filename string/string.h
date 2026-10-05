// SYNTHUX ACADEMY .........................................
// ARPEGGIATED STRING ......................................
#pragma once
#include <array>
#include <random>
#include <algorithm>

#include <daisysp.h>

#include "nocopy.h"
#include "config.h"

#include "synclock.h"
#include "trigger.h"
#include "cpattern.h"
#include "arp.h"
#include "scale.h"
#include "vox.h"
#include "xfade.h"
#include "latch.h"

namespace synthux {

class String {
public:
  static constexpr uint8_t kNotesCount = 8;
  static constexpr uint8_t kVoicesCount = 7;

  String();
  ~String() {}

  void Init(const float sample_rate, const float buffer_size);

  void SetTempo(const float tempo) { _clock.SetTempo(tempo); }
  void SetBpm(const float bpm);
  bool CheckBeatPulse() {
    bool p = _beat_pulse;
    _beat_pulse = false;
    return p;
  }
  void SpeedUp() {
    _tempo = std::min(_tempo + .05f, 1.f);
    SetTempo(_tempo);
  }
  void SlowDown() {
    _tempo = std::max(_tempo - .05f, .05f);
    SetTempo(_tempo);
  }
  void ProcessClockIn(const bool state) { _clock.Process(state); }

  void SetArpOn(const bool value) {
    if (_is_arp_on != value) {
      Reset();
    }
    _is_arp_on = value;
    if (_is_arp_on) {
      if (!_clock.IsRunning()) _clock.Run();
    } else {
      _clock.Stop();
    }
  }

  bool IsLatched() { return _latch.on(); }
  void SetLatch(const bool new_latch);

  bool IsMono() const { return _is_mono; }
  void SetMono(const bool mono);
  void ToggleMono() { SetMono(!_is_mono); }
  int8_t ActiveMonoPad() const { return (_mono_stack_size > 0) ? static_cast<int8_t>(_mono_stack[_mono_stack_size - 1]) : -1; }

  void NoteOn(const uint8_t note_num, const float velocity = 1.f);
  void NoteOff(const uint8_t note_num);

  void SetVoicePressure(const uint8_t voice_num, const float pressure);
  void SetVoiceSustain(const uint8_t voice_num, const bool sustain);
  void SetVoiceAftertouch(const uint8_t voice_num, const float pressure);

  void SetCurrentOctaveMult(const float mult) { _current_oct_mult = mult; }
  void SetDampenPad(const bool active, const float pressure);

  void SetBowPressure(const float pressure);
  void SetSustain(const bool sustain);
  void SetExciterMode(const int mode) {
    if (_exciter_mode != mode) {
      _exciter_mode = mode;
      for (auto& v : _vox) {
        v.SetBowPressure(0.0f);
        v.SetSustain(false);
      }
    }
  }
  void SetNoteFreq(const uint8_t note_num);
  void SetPadPressure(const uint8_t voice_num, const float pressure) {
    if (voice_num < kVoicesCount) {
      _pad_pressure[voice_num] = pressure;
      _vox[voice_num].SetAftertouch(pressure);
    }
  }

  bool IsNoteLatched(const uint8_t num) const {
    return _is_arp_on && _latch.on() && _latch.is_held(num);
  }

  void Reset();

  uint8_t ScalesCount() { return _scale.ScalesCount(); }
  void SetScaleIndex(const uint8_t index);

  void SetTransp(const float value);

  void SetBrightness(const float value);
  void SetStructure(const float value);
  void SetDamping(const float value);

  void SetHumanNoteChance(const float value) {
    _human_note_chance = static_cast<uint8_t>(daisysp::fmap(value, 0.f, 100.f));
  }
  void SetHumanStringChance(const float value) {
    _human_string_chance = static_cast<uint8_t>(daisysp::fmap(value, 0.f, 100.f));
  }

  void SetPattern(const float value) { _pattern.SetOnsets(value); }
  void SetPatternShift(const float value) { _pattern.SetShift(value); }

  void SetReverbMix(const float value) { _xfade.SetStage(value); }

  void SetDrive(const float value) {
    const float kBreak = 0.30f;
    if (value <= kBreak) {
      _drive.SetDrive(0.2f);
      float atten = value / kBreak;
      _volume = atten * atten;
      _soft_scale = atten;
    } else {
      _soft_scale = 1.0f;
      float u = (value - kBreak) / (1.0f - kBreak);
      _drive.SetDrive(0.2f + u * 0.3f);
      float comp = 1.0f - u * 0.55f;
      _volume = comp * comp;
    }
    _update_damping();
  }

  void SetInputVolume(const float value) { _input_volume = daisysp::fclamp(value, 0.f, 1.f); }
  float InputVolume() const { return _input_volume; }

  void Process(const float * const *in, float **out, size_t size);
  void Process(float **out, size_t size) { Process(nullptr, out, size); }

private:
  NOCOPY(String)

  void _on_clock_tick();
  void _on_arp_note_on(uint8_t num, uint8_t vel);
  void _on_arp_note_off(uint8_t num);

  void _on_latch_note_on(uint8_t num);
  void _on_latch_note_off(uint8_t num);

  float _humanized_note_freq(uint8_t note);
  void _humanize_and_apply(uint8_t voice_num);
  void _update_damping();

  static constexpr uint8_t kPPQN = 48;
  static constexpr uint8_t kPPQNExtern = 24;

  Scale _scale;
  std::array<Vox, kVoicesCount> _vox;
  SynClock _clock;
  Trigger  _trigger;
  CPattern _pattern;
  Arp<kNotesCount, 4> _arp;
  daisysp::Overdrive _drive;
  daisysp::ReverbSc  _reverb;
  XFade _xfade;
  Latch<kNotesCount> _latch;

  std::default_random_engine _rand_engine;
  std::uniform_int_distribution<uint8_t> _dice;

  std::array<bool, kNotesCount> _note_hold;
  std::array<bool, kNotesCount> _note_on;
  std::array<float, 2> _reverb_in;
  std::array<float, 2> _reverb_out;
  std::array<float, 2> _bus;

  float _tempo;
  float _brightness;
  float _structure;
  float _damping;
  std::array<float, kVoicesCount> _bright_offset;
  std::array<float, kVoicesCount> _struct_offset;
  std::array<float, kVoicesCount> _damp_offset;
  uint8_t _human_note_chance;
  uint8_t _human_string_chance;
  float _volume;
  float _soft_scale;
  bool _is_arp_on;
  int _exciter_mode;
  float _input_volume;
  float _trans_mult;
  std::array<float, kVoicesCount> _pad_pressure;
  bool _is_mono;
  std::array<uint8_t, kVoicesCount> _mono_stack;
  uint8_t _mono_stack_size;
  float _current_oct_mult;
  std::array<float, kVoicesCount> _voice_oct_mult;
  bool _is_dampen_active;
  float _dampen_pressure;
  daisysp::Svf _body_filter_l;
  daisysp::Svf _body_filter_r;
  daisysp::DcBlock _dc_block_l;
  daisysp::DcBlock _dc_block_r;
  uint8_t _clock_tick_counter;
  bool _beat_pulse;

  // Stereo panning mapped to physical touchpad layout:
  // Voice 0 (P03): Far Left,    Voice 1 (P04): Mid Left,     Voice 2 (P05): Center,
  // Voice 3 (P06): Mid Right,   Voice 4 (P07): Far Right,
  // Voice 5 (P08): Center Left, Voice 6 (P09): Center Right
  static constexpr std::array<float, kVoicesCount> kPanL = { 0.860f, 0.815f, 0.707f, 0.580f, 0.510f, 0.763f, 0.646f };
  static constexpr std::array<float, kVoicesCount> kPanR = { 0.510f, 0.580f, 0.707f, 0.815f, 0.860f, 0.646f, 0.763f };
};

};
