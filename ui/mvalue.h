#pragma once
#include "nocopy.h"

namespace synthux {

class MValue {
public:
  MValue();
  ~MValue() {}

float Process(const float value, const bool active);

float Value() const { return _value; }

void Set(const float value) {
  _is_tracking = false;
  _is_active = false;
  _value = value;
}

void SetTracking(const float value) {
  _is_tracking = true;
  _is_active = true;
  _value = value;
  _init_value = value;
}

private:
  NOCOPY(MValue)

  bool _set_active(const bool active, const float value);

  static constexpr float kTreshold = 0.02;

  float _init_value;
  float _value;
  bool _is_active;
  bool _is_tracking;
};

};
