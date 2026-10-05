#pragma once

#include "daisy_seed.h"
#include <stdint.h>
#include <array>
#include <functional>
#include "nocopy.h"

#ifdef __cplusplus

namespace synthux {

class Pads {
public:
    static constexpr std::array<float, 12> kDefaultMaxDeltas = {
        375.0f, 375.0f, 375.0f, 375.0f, 460.0f, 375.0f,
        375.0f, 375.0f, 425.0f, 425.0f, 375.0f, 375.0f
    };

    Pads(): _state { 0 } {
        for (size_t i = 0; i < 12; i++) {
            _pressure[i] = 0.0f;
            _velocity[i] = 0.0f;
            _debounce_cnt[i] = 0;
            _strike_window[i] = 0;
            _strike_peak_delta[i] = 0;
            _release_lockout[i] = 0;
            _pad_max_delta[i] = kDefaultMaxDeltas[i];
        }
    }
    ~Pads() {}

    void Init(daisy::DaisySeed& hw);
    void Process();
    void Recalibrate();

    void SetOnTouch(std::function<void(uint16_t)> on_touch) {
        _on_touch = on_touch;
    }
    void SetOnRelease(std::function<void(uint16_t)> on_release) {
        _on_release = on_release;
    }
    bool IsTouched(uint16_t pad) const {
        return _state & (1 << pad);
    }
    bool HasTouch() const {
        return _state > 0;
    }

    float Pressure(uint16_t pad) const {
        return (pad < 12) ? _pressure[pad] : 0.0f;
    }
    float Velocity(uint16_t pad) const {
        return (pad < 12) ? _velocity[pad] : 0.0f;
    }
    float operator[](size_t pad) const {
        return Pressure(pad);
    }

private:
    NOCOPY(Pads)

    void WriteRegister(uint8_t reg, uint8_t val);
    bool ReadBurst(uint8_t start_reg, uint8_t* buffer, uint16_t size);

    static constexpr uint8_t kMpr121Addr = 0x5A;

    daisy::I2CHandle _i2c;
    uint16_t _state;

    std::array<float, 12> _pad_max_delta;
    std::array<float, 12> _pressure;
    std::array<float, 12> _velocity;
    std::array<uint8_t, 12> _debounce_cnt;
    std::array<uint8_t, 12> _strike_window;
    std::array<int32_t, 12> _strike_peak_delta;
    std::array<uint8_t, 12> _release_lockout;

    std::function<void(uint16_t pad)> _on_touch;
    std::function<void(uint16_t pad)> _on_release;
};

};

#endif
