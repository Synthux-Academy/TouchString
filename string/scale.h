#pragma once

#include <array>
#include <random>
#include "nocopy.h"
#include "config.h"

namespace synthux {

class Scale {
public:
  Scale();
  ~Scale() {}

  uint8_t ScalesCount() {
    return kStringScalesCount;
  }

  void SetScaleIndex(uint8_t index) {
    if (index < kStringScalesCount && index != _scale_index) {
      _scale_index = index;
      _PrepareScale();
    }
  }

  float TransMult(const float value) {
    auto new_trans_index = static_cast<uint8_t>(value * (_trans.size() - 1) + 0.5f);
    if (new_trans_index >= _trans.size()) new_trans_index = _trans.size() - 1;
    return _trans[new_trans_index];
  }

  float FreqAt(uint8_t idx) {
    if (idx >= kStringScaleSize) idx = kStringScaleSize - 1;
    return _scale[idx];
  }

  float Random() {
    std::uniform_int_distribution<uint8_t> note_distribution(0, kStringScaleSize - 1);
    return FreqAt(note_distribution(_rand_engine));
  }

private:
  NOCOPY(Scale)

  void _PrepareScale() {
    auto transposition = _trans[_trans_index];
    auto& scale = kStringScales[_scale_index];
    for (uint8_t i = 0; i < kStringScaleSize; i++) {
      _scale[i] = scale[i] * transposition;
    }
  }

  uint8_t _scale_index;
  uint8_t _trans_index;
  std::default_random_engine _rand_engine;

  std::array<float, kStringScaleSize> _scale;

  static constexpr std::array<float, 25> _trans = {
    0.50000000f, 0.52973155f, 0.56123102f, 0.59460356f, 0.62996052f, 0.66741993f,
    0.70710678f, 0.74915354f, 0.79370053f, 0.84089642f, 0.89089872f, 0.94387431f,
    1.00000000f, // Center (0 semitones / unison root note)
    1.05946309f, 1.12246205f, 1.18920712f, 1.25992105f, 1.33483985f, 1.41421356f,
    1.49830708f, 1.58740105f, 1.68179283f, 1.78179744f, 1.88774863f, 2.00000000f
  };
};

};
