#pragma once

#include <functional>
#include <bitset>

namespace synthux {

template<uint8_t note_count>
class Latch {
public:
    Latch() {
        _note_on.reset();
        _note_hold.reset();
    }
    ~Latch() = default;

    bool on() const { return _on; }
    bool is_held(const uint8_t num) const { return num < note_count && _note_hold.test(num); }

    void clear() {
        _note_on.reset();
        _note_hold.reset();
    }

    void set_on(const bool on) { 
        auto was_on = _on;
        _on = on;
        if (was_on && !on) {
            for (uint8_t i = 0; i < _note_hold.size(); i++) {
                if (_note_hold.test(i) && !_note_on.test(i)) {
                    if (_on_note_off) _on_note_off(i);
                    _note_hold.reset(i);
                }
            }  
        }
    }

    void note_on(const uint8_t num)
    { 
        _note_on.set(num);

        if (_note_on != _note_hold) {
            for (uint8_t note = 0; note < note_count; note ++) {
                if (_note_hold.test(note) && !_note_on.test(note)) {
                    if (_on_note_off) _on_note_off(note);
                    _note_hold.reset(note);
                }
            }
        }

        if (!_note_hold.test(num)) {
            if (_on_note_on) _on_note_on(num);
            _note_hold.set(num);
        }
    }

    void note_off(const uint8_t num)
    {
        if (!_on) {
            if (_on_note_off) _on_note_off(num);
            _note_hold.reset(num);
        }
        _note_on.reset(num);
    }

    void set_on_note_on(std::function<void(uint8_t)> on_note_on)
    {
        _on_note_on = on_note_on;
    }
    void set_on_note_off(std::function<void(uint8_t)> on_note_off)
    {
        _on_note_off = on_note_off;
    }


private:
    std::function<void(uint8_t)> _on_note_on;
    std::function<void(uint8_t)> _on_note_off;

    std::bitset<note_count> _note_hold;
    std::bitset<note_count> _note_on;
    
    bool _on;
};

}
