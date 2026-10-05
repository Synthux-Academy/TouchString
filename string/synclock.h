#pragma once

#include <cmath>
#include <array>
#include <functional>

#include "nocopy.h"

static constexpr float kBPMMin = 40;
static constexpr float kBPMRange = 200;

inline static bool fcomp(const float lhs, const float rhs, const int precision = 2) {
    auto digits = precision * 10;
    auto lhs_int = static_cast<int32_t>(roundf(lhs * digits));
    auto rhs_int = static_cast<int32_t>(roundf(rhs * digits));
    return lhs_int == rhs_int;
}

namespace synthux {

class SynClock {
public:
    SynClock();
    ~SynClock() {}

    void Init(const float update_interval_mks, const uint32_t ppqn_in, const uint32_t ppqn_out);

    /*
    Called by internal interrupt timer (audio callback in this implementation).
    */
    void Tick() { if (_is_running) _emit_ticks(); }

    void SetOnTick(std::function<void()> on_tick) { _on_tick = on_tick; }

    /*
    Read external clock pin
    */
    void Process(const bool state);
    
    float Tempo() { return 60000000.f / _tempo_mks; }
    /*
    Setting tempo from internal control. 
    Has no effect in case of syncing to extrnal clock.
    */
    void SetTempo(const float norm_value);
    void SetBpm(const float bpm) {
        _manual_tempo = (bpm < kBPMMin) ? kBPMMin : ((bpm > kBPMMin + kBPMRange) ? (kBPMMin + kBPMRange) : bpm);
        _tempo_mks = _get_tempo_mks(_manual_tempo);
        const auto clock_off_offset = 10.0f;
        _raw_manual_tempo = (_manual_tempo - kBPMMin + clock_off_offset) / (kBPMRange - clock_off_offset);
        if (_raw_manual_tempo < 0.0f) _raw_manual_tempo = 0.0f;
        if (_raw_manual_tempo > 1.0f) _raw_manual_tempo = 1.0f;
        if (_is_running) {
            _reset();
        }
    }

    /*
    In case of external clock sync this
    method only schedules playback. Actual playback 
    starts on the first tick of the external clock.
    see clock_in_tick() below.
    */
    void Run() {
      if (_external_clock()) _is_about_to_run = true; else _is_running = true;
    }

    void Stop() {
      _is_running = false;
      _reset();
    }

    bool IsRunning() { return _is_running; };

private:
    NOCOPY(SynClock)

    bool _external_clock() { return _manual_tempo < kBPMMin; }
    /*
    External clock received
    This method starts playback on first received clock
    after playback was scheduled. After that, calls sync 
    for every tick received.
    */
    void _external_clock_tick();

    /*
    See cpp file for details
    */
    void _emit_ticks();
    
    void _reset();
    
    uint32_t _get_tempo_mks(const float tempo) { return static_cast<uint32_t>(60.f * 1e6 / tempo); }

    std::function<void()> _on_tick;

    float _manual_tempo;
    float _raw_manual_tempo;

    uint32_t _ppqn_out;
    uint32_t _tr_time;
    uint32_t _ticks_per_clock;
    uint32_t _ticks;
    uint32_t _fticks;
    uint32_t _ticks_at_last_clock;
    uint32_t _tempo_ticks;
    uint32_t _tempo_mks;
    bool _hold;
    bool _resync;
    bool _is_running;
    bool _is_about_to_run;
    bool _last_state;
};

};
