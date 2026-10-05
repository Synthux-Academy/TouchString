#pragma once

#include <array>

// Scale ..................................................................
static constexpr uint8_t kStringScaleSize = 7;
static constexpr uint8_t kStringScalesCount = 7;
static constexpr std::array<std::array<float, kStringScaleSize>, kStringScalesCount> kStringScales = {{
    { 130.81f, 196.00f, 233.08f, 261.63f, 293.66f, 311.13f, 392.00f }, // 1 (P03): Amara (Celtic Minor)
    { 130.81f, 164.81f, 174.61f, 196.00f, 220.00f, 261.63f, 349.23f }, // 2 (P04): Oxalis (Folk Major)
    { 130.81f, 146.83f, 155.56f, 196.00f, 233.08f, 261.63f, 311.13f }, // 3 (P05): Pigmy (Dorian Pygmy)
    { 130.81f, 146.83f, 164.81f, 174.61f, 196.00f, 220.00f, 261.63f }, // 4 (P06): Major (Ionian)
    { 130.81f, 146.83f, 155.56f, 174.61f, 196.00f, 207.65f, 261.63f }, // 5 (P07): Natural Minor (Aeolian)
    { 130.81f, 155.56f, 174.61f, 196.00f, 233.08f, 261.63f, 311.13f }, // 6 (P08): Minor Pentatonic
    { 130.81f, 146.83f, 164.81f, 196.00f, 220.00f, 261.63f, 293.66f }  // 7 (P09): Major Pentatonic
}};

// Reverb .................................................................
static constexpr float kReverbFeedback = .8f;
static constexpr float kReverLPFreq = 10000.f; //Hz

// MIDI ...................................................................
#if !DEBUG
#define USB_MIDI
#endif
