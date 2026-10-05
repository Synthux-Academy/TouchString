#pragma once

#include "daisysp.h"
#include "nocopy.h"
#include <cmath>

namespace synthux {

// Optimized Extended Karplus-Strong string module.
// Precomputes damping, dispersion, and delay parameters on change
// instead of re-evaluating powf/atanf on every audio sample.
class FastString {
public:
  FastString() {}
  ~FastString() {}

  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    frequency_ = 440.f / sample_rate_;
    non_linearity_amount_ = 0.5f;
    brightness_ = 0.5f;
    damping_ = 0.5f;
    damping_ratio_ = 1.0f;
    damping_compensation_ = 1.0f;
    rand_seed_ = 123456789u;

    string_.Init();
    stretch_.Init();
    Reset();

    SetDamping(0.8f);
    SetNonLinearity(0.1f);
    SetBrightness(0.5f);
    SetFreq(440.f);

    crossfade_.Init();
  }

  void Reset() {
    string_.Reset();
    stretch_.Reset();
    iir_damping_filter_.Init();
    dc_blocker_.Init(sample_rate_);
    dispersion_noise_ = 0.0f;
    curved_bridge_ = 0.0f;
    out_sample_[0] = out_sample_[1] = 0.0f;
    src_phase_ = 0.0f;
  }

  void SetFreq(float freq) {
    freq /= sample_rate_;
    frequency_ = daisysp::fclamp(freq, 0.f, 0.25f);
    _update_pitch();
  }

  void SetNonLinearity(float non_linearity_amount) {
    if (non_linearity_amount <= 0.0f) {
      is_dispersion_ = false;
      non_linearity_amount_ = daisysp::fclamp(-non_linearity_amount, 0.f, 1.f);
    } else {
      is_dispersion_ = true;
      non_linearity_amount_ = daisysp::fclamp(non_linearity_amount, 0.f, 1.f);
    }
    _update_non_linearity();
  }

  void SetBrightness(float brightness) {
    brightness_ = daisysp::fclamp(brightness, 0.f, 1.f);
    _update_damping_and_brightness();
  }

  void SetDamping(float damping) {
    damping_ = daisysp::fclamp(damping, 0.f, 1.f);
    _update_damping_and_brightness();
  }

  float Process(const float in) {
    src_phase_ += src_ratio_;
    if (src_phase_ > 1.0f) {
      src_phase_ -= 1.0f;

      float delay = delay_ * damping_compensation_;
      float s = 0.0f;

      if (is_dispersion_) {
        rand_seed_ = rand_seed_ * 1664525u + 1013904223u;
        float noise = static_cast<float>(static_cast<int32_t>(rand_seed_)) * (1.0f / 2147483648.0f) * 0.5f;
        daisysp::fonepole(dispersion_noise_, noise, noise_filter_);
        float mod = daisysp::fclamp(1.0f + dispersion_noise_ * noise_amount_, 0.90f, 1.10f);
        delay *= mod;
        delay = daisysp::fclamp(delay, 4.0f, static_cast<float>(kDelayLineSize - 4));

        float ap_delay = delay * stretch_point_;
        float main_delay = delay - ap_delay * (0.408f - stretch_point_ * 0.308f) * stretch_correction_;
        if (ap_delay >= 4.0f && main_delay >= 4.0f) {
          s = string_.Read(main_delay);
          s = stretch_.Allpass(s, ap_delay, ap_gain_);
        } else {
          s = string_.ReadHermite(delay);
        }
      } else {
        float mod = daisysp::fclamp(1.0f - curved_bridge_ * bridge_curving_, 0.90f, 1.10f);
        delay *= mod;
        delay = daisysp::fclamp(delay, 4.0f, static_cast<float>(kDelayLineSize - 4));
        s = string_.ReadHermite(delay);
        float value = daisysp::fclamp(fabsf(s) - 0.025f, 0.0f, 1.0f);
        float sign = s > 0.0f ? 1.0f : -1.5f;
        curved_bridge_ = (value + value) * sign;
      }

      s += in;
      s = daisysp::fclamp(s, -2.5f, +2.5f);

      s = dc_blocker_.Process(s);
      s = iir_damping_filter_.Process(s);

      if (std::isnan(s) || std::isinf(s)) {
        s = 0.0f;
        Reset();
      }

      string_.Write(s);

      out_sample_[1] = out_sample_[0];
      out_sample_[0] = s;
    }

    crossfade_.SetPos(src_phase_);
    return crossfade_.Process(out_sample_[1], out_sample_[0]);
  }

private:
  static constexpr size_t kDelayLineSize = 1024;

  void _update_non_linearity() {
    stretch_point_ = non_linearity_amount_ * (2.0f - non_linearity_amount_) * 0.225f;
    float noise_amount_sqrt = non_linearity_amount_ > 0.75f
                                  ? 4.0f * (non_linearity_amount_ - 0.75f)
                                  : 0.0f;
    noise_amount_ = noise_amount_sqrt * noise_amount_sqrt * 0.1f;
    float bridge_curving_sqrt = non_linearity_amount_;
    bridge_curving_ = bridge_curving_sqrt * bridge_curving_sqrt * 0.01f;
    ap_gain_ = -0.618f * non_linearity_amount_
                    / (0.15f + fabsf(non_linearity_amount_));
  }

  void _update_damping_and_brightness() {
    noise_filter_ = 0.06f + 0.94f * brightness_ * brightness_;

    float brightness = brightness_;
    float damping_cutoff = daisysp::fmin(12.0f + damping_ * damping_ * 60.0f + brightness * 24.0f, 84.0f);

    if (damping_ >= 0.95f) {
      float to_infinite = 20.0f * (damping_ - 0.95f);
      brightness += to_infinite * (1.0f - brightness);
      damping_cutoff += to_infinite * (110.0f - damping_cutoff);
    }

    damping_ratio_ = powf(2.f, damping_cutoff * daisysp::kOneTwelfth);
    damping_compensation_ = 1.f - 2.f * atanf(1.f / damping_ratio_) / (TWOPI_F);

    _update_pitch();
  }

  void _update_pitch() {
    float delay = 1.0f / (frequency_ > 0.0001f ? frequency_ : 0.0001f);
    delay_ = daisysp::fclamp(delay, 4.f, static_cast<float>(kDelayLineSize - 4));

    src_ratio_ = delay_ * frequency_;
    if (src_ratio_ >= 0.9999f) {
      src_phase_ = 1.0f;
      src_ratio_ = 1.0f;
    }

    float damping_f = daisysp::fmin(frequency_ * damping_ratio_, 0.44f);
    if (damping_ >= 0.95f) {
      float to_infinite = 20.0f * (damping_ - 0.95f);
      damping_f += to_infinite * (0.45f - damping_f);
    }
    iir_damping_filter_.SetFrequency(damping_f);

    stretch_correction_ = (160.0f / sample_rate_) * delay_;
    stretch_correction_ = daisysp::fclamp(stretch_correction_, 1.f, 2.1f);
  }

  daisysp::DelayLine<float, kDelayLineSize>     string_;
  daisysp::DelayLine<float, kDelayLineSize / 4> stretch_;

  float frequency_;
  float non_linearity_amount_;
  bool  is_dispersion_;
  float brightness_;
  float damping_;
  float sample_rate_;

  daisysp::OnePole   iir_damping_filter_;
  daisysp::DcBlock   dc_blocker_;
  daisysp::CrossFade crossfade_;

  float dispersion_noise_;
  float curved_bridge_;
  float src_phase_;
  float out_sample_[2];
  uint32_t rand_seed_;

  float delay_;
  float src_ratio_;
  float damping_ratio_;
  float damping_compensation_;
  float stretch_point_;
  float stretch_correction_;
  float noise_amount_;
  float noise_filter_;
  float bridge_curving_;
  float ap_gain_;
};

class Vox {
public:
  Vox(): 
    _sample_rate { 48000.f },
    _freq_mult { 1.f },
    _current_freq { 220.f },
    _target_freq { 220.f },
    _base_freq { 220.f },
    _aftertouch { 0.f },
    _target_aftertouch { 0.f },
    _aftertouch_baseline { 0.5f },
    _aftertouch_lockout { 0 },
    _f0 { 0.005f },
    _brightness { 0.5f },
    _structure { 0.5f },
    _damping { 0.5f },
    _accent { 0.8f },
    _bright_ratio { 1.f },
    _current_filter_cutoff { 0.f },
    _strike_gain { 1.f },
    _freq_scale { 1.f },
    _remaining_impulse { 0 },
    _is_bowing { false },
    _bow_pressure { 0.f },
    _bow_env { 0.f },
    _last_bow_update { 0.f },
    _rand_seed { 123456789 },
    _is_active { false },
    _silent_samples { 0 },
    _last_string_out { 0.f }
  {}
  ~Vox() {}

  void Init(float sample_rate, float initial_freq = 220.f, uint32_t seed = 123456789u) {
    _sample_rate = sample_rate;
    _rand_seed = seed;
    _string.Init(sample_rate);
    _filter.Init(sample_rate);
    _filter.SetRes(0.0f);
    _dc_block.Init(sample_rate);
    _target_freq = initial_freq;
    _current_freq = initial_freq;
    _base_freq = initial_freq;
    _aftertouch = 0.f;
    _target_aftertouch = 0.f;
    _aftertouch_baseline = 0.5f;
    _aftertouch_lockout = 0;
    _freq_mult = 1.f;
    _freq_scale = 1.f;
    _is_active = false;
    _silent_samples = 0;
    _last_bow_update = 0.f;
    _last_string_out = 0.f;
    SetStructure(0.5f);
    SetBrightness(0.5f);
    SetDamping(0.5f);
    _update_freq_internal(initial_freq);
  }

  void SetAftertouch(float pressure) {
    _target_aftertouch = daisysp::fclamp(pressure, 0.f, 1.f);
  }

  void SetBrightness(const float value) {
    _brightness = daisysp::fclamp(value, 0.f, 1.f);
    _update_bright_ratio();
    _update_filter();
    _update_string_params();
  }

  void SetStructure(const float value) {
    _structure = daisysp::fclamp(value, 0.f, 1.f);
    const float non_linearity = _structure < 0.24f
      ? (_structure - 0.24f) * 4.166f
      : (_structure > 0.26f ? (_structure - 0.26f) * 1.35135f : 0.0f);
    _string.SetNonLinearity(non_linearity);
  }

  void SetDamping(const float value) {
    _damping = daisysp::fclamp(value, 0.f, 1.f);
    _update_string_params();
  }

  void NoteOn(float freq, float velocity = 1.f) {
    _base_freq = freq;
    _target_freq = freq;
    _current_freq = freq;
    _aftertouch = 0.f;
    _target_aftertouch = 0.f;
    _aftertouch_baseline = 1.0f;
    _aftertouch_lockout = 12000; // 250ms at 48kHz: rock-solid stable strike attack
    _accent = daisysp::fclamp(0.04f + 0.90f * velocity, 0.02f, 0.95f);
    float eff_freq = daisysp::fclamp(freq * _freq_mult, 20.f, 8000.f);
    float freq_comp = daisysp::fclamp(sqrtf(eff_freq / 261.63f), 0.60f, 1.20f);
    _strike_gain = velocity * (0.05f + 0.95f * velocity) * freq_comp;
    _update_bright_ratio();
    _update_filter();
    _update_string_params();
    _update_freq_internal(freq);

    _remaining_impulse = static_cast<size_t>(1.0f / _f0);
    if (_remaining_impulse > 1024) _remaining_impulse = 1024;
    _is_active = true;
    _silent_samples = 0;
    _last_string_out = 0.f;
  }

  void SetFreq(float freq) {
    _base_freq = freq;
    _target_freq = freq;
    if (!_is_active || !_is_bowing) {
      _current_freq = freq;
      _update_freq_internal(freq);
    }
  }

  void SetMult(const float value, bool update_now = true) {
    _freq_mult = value;
    if (update_now) {
      _update_freq_internal(_current_freq);
    }
  }

  void SetSustain(bool sustain) {
    _is_bowing = sustain;
    if (!sustain) {
      _bow_pressure = 0.f;
    } else {
      _is_active = true;
      _silent_samples = 0;
    }
  }

  void SetBowPressure(float pressure) {
    _bow_pressure = daisysp::fclamp(pressure, 0.f, 1.f);
    _is_bowing = (_bow_pressure > 0.001f);
    if (_is_bowing) {
      _is_active = true;
      _silent_samples = 0;
    }
  }

  void BowStroke(float pressure) {
    SetBowPressure(pressure);
  }

  bool IsActive() const { return _is_active; }
  bool IsBowing() const { return _is_bowing; }

  void Reset() {
    _string.Reset();
    _filter.Init(_sample_rate);
    _filter.SetRes(0.0f);
    _dc_block.Init(_sample_rate);
    _is_active = false;
    _is_bowing = false;
    _bow_pressure = 0.f;
    _bow_env = 0.f;
    _remaining_impulse = 0;
    _silent_samples = 0;
    _aftertouch = 0.f;
    _target_aftertouch = 0.f;
    _last_string_out = 0.f;
  }

  float Process(float ext_in = 0.f) {
    if (fabsf(ext_in) > 0.0001f) {
      _is_active = true;
      _silent_samples = 0;
    }

    if (!_is_active) {
      return 0.f;
    }

    // Subtle guitar-like aftertouch ONLY in pluck mode (finger rocking vibrato)
    if (!_is_bowing) {
      if (_aftertouch_lockout > 0) {
        _aftertouch_lockout--;
        if (_aftertouch_lockout == 0) {
          // Attack phase ended: capture resting touch baseline
          _aftertouch_baseline = daisysp::fclamp(_target_aftertouch, 0.10f, 0.85f);
          _aftertouch = 0.0f;
        }
        _target_freq = _base_freq;
      } else {
        // Only respond to intentional extra pressure beyond the landing touch
        float extra = _target_aftertouch - _aftertouch_baseline;
        float target_press = (extra > 0.06f)
          ? daisysp::fclamp((extra - 0.06f) / (1.0f - _aftertouch_baseline), 0.0f, 1.0f)
          : 0.0f;
        _aftertouch += (target_press - _aftertouch) * 0.003f;

        // Subtle acoustic bend (max +25 cents at full squeeze):
        // Hand micro-rocking directly generates real acoustic finger vibrato
        float bend = (_aftertouch * _aftertouch) * 0.015f;
        _target_freq = _base_freq * (1.0f + bend);
      }
    } else {
      // Bow mode: subtle physical pitch deflection under heavy bow force (~2-4 cents flattening)
      float pitch_flatten = daisysp::fclamp((_bow_env - 0.55f) * 0.006f, 0.0f, 0.003f);
      _target_freq = _base_freq * (1.0f - pitch_flatten);
      _aftertouch = 0.0f;
    }

    if (fabsf(_target_freq - _current_freq) > 0.01f) {
      float slew = _is_bowing ? 0.0025f : 0.02f;
      _current_freq += (_target_freq - _current_freq) * slew;
      _update_freq_internal(_current_freq);
    }

    float target_bow = _is_bowing ? _bow_pressure : 0.f;
    float bow_slew = (target_bow < _bow_env) ? 0.008f : 0.004f;
    _bow_env += (target_bow - _bow_env) * bow_slew;
    if (_bow_env < 0.0002f && target_bow == 0.f) {
      _bow_env = 0.f;
    }

    if (fabsf(_bow_env - _last_bow_update) > 0.02f) {
      _last_bow_update = _bow_env;
      _update_bright_ratio();
      _update_filter();
      _update_string_params();
    }

    _rand_seed = _rand_seed * 1664525u + 1013904223u;
    float noise = static_cast<float>(static_cast<int32_t>(_rand_seed)) * (1.0f / 2147483648.0f);

    float exc = 0.f;

    if (_remaining_impulse > 0) {
      exc += noise * (_strike_gain * 0.70f);
      _remaining_impulse--;
    }

    if (_bow_env > 0.0001f) {
      // Closed-loop non-linear stick-slip Helmholtz friction with zero-DC equilibrium
      float v_rel = _bow_env - 0.60f * _last_string_out;
      float friction = v_rel / (1.0f + 4.5f * (v_rel * v_rel));
      float static_f = _bow_env / (1.0f + 4.5f * (_bow_env * _bow_env));
      float bow_force = (friction - static_f) * (_bow_env * 0.28f * _freq_scale);

      // Acoustic rosin friction noise is purely continuous, musical, and proportional to bow force
      float rosin = noise * (_bow_env * 0.045f * (0.30f + fabsf(v_rel)) * _freq_scale);
      exc += bow_force + rosin;
    }

    if (fabsf(ext_in) > 0.00005f) {
      exc += ext_in;
    }

    _filter.Process(exc);
    float in = _filter.Low();

    float out = _string.Process(in);
    out = _dc_block.Process(out);
    _last_string_out = daisysp::fclamp(out, -1.2f, 1.2f);

    // Voice activity detection
    if (_remaining_impulse == 0 && _bow_env < 0.0001f && !_is_bowing && fabsf(ext_in) < 0.0001f) {
      if (fabsf(out) < 0.00005f) {
        _silent_samples++;
        if (_silent_samples > 2400) { // ~50ms of silence
          _is_active = false;
          return 0.f;
        }
      } else {
        _silent_samples = 0;
      }
    } else {
      _silent_samples = 0;
    }

    return out;
  }

private:
  NOCOPY(Vox)

  __attribute__((noinline)) void _update_bright_ratio() {
    float eff_accent = (_is_bowing || _bow_env > 0.001f) ? _bow_env : _accent;
    float b = _brightness;
    float eff_b = b + 0.30f * eff_accent * (1.f - b);
    float range = 72.0f;
    _bright_ratio = powf(2.f, daisysp::kOneTwelfth * (eff_b * (2.0f - eff_b) - 0.5f) * range);
  }

  __attribute__((noinline)) void _update_string_params() {
    float eff_accent = (_is_bowing || _bow_env > 0.001f) ? _bow_env : _accent;
    float b = _brightness;
    float eff_b = b + 0.30f * eff_accent * (1.f - b);
    float eff_d = _damping + 0.20f * eff_accent * (1.f - _damping);
    _string.SetBrightness(daisysp::fclamp(eff_b, 0.f, 0.95f));
    _string.SetDamping(daisysp::fclamp(eff_d, 0.f, 0.95f));
  }

  __attribute__((noinline)) void _update_freq_internal(float freq) {
    float eff_freq = daisysp::fclamp(freq * _freq_mult, 20.f, 8000.f);
    _string.SetFreq(eff_freq);
    _f0 = daisysp::fclamp(eff_freq / _sample_rate, 0.0005f, 0.25f);
    _freq_scale = daisysp::fclamp(sqrtf(220.0f / eff_freq), 0.35f, 1.40f);
    _update_filter();
  }

  __attribute__((noinline)) void _update_filter() {
    float f = 3.5f * _f0;
    float bow_blend = daisysp::fclamp(_bow_env * 2.5f, 0.0f, 1.0f);
    float max_cutoff = 0.33f * (1.0f - bow_blend) + 0.09f * bow_blend;
    float cutoff = daisysp::fclamp(f * _bright_ratio, _f0 * 1.5f, max_cutoff);
    float cutoff_hz = cutoff * _sample_rate;
    if (fabsf(cutoff_hz - _current_filter_cutoff) > 5.0f) {
      _current_filter_cutoff = cutoff_hz;
      _filter.SetFreq(cutoff_hz);
    }
  }

  float _sample_rate;
  float _freq_mult;
  float _current_freq;
  float _target_freq;
  float _base_freq;
  float _aftertouch;
  float _target_aftertouch;
  float _aftertouch_baseline;
  uint32_t _aftertouch_lockout;
  float _f0;
  float _brightness;
  float _structure;
  float _damping;
  float _accent;
  float _bright_ratio;
  float _current_filter_cutoff;
  float _strike_gain;
  float _freq_scale;
  size_t _remaining_impulse;

  bool  _is_bowing;
  float _bow_pressure;
  float _bow_env;
  float _last_bow_update;
  uint32_t _rand_seed;

  bool     _is_active;
  uint32_t _silent_samples;

  float    _last_string_out;

  daisysp::Svf     _filter;
  FastString       _string;
  daisysp::DcBlock _dc_block;
};

};

