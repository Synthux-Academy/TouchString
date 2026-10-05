# This is TouchString(s)

Polyphonic physical modeling string synthesizer for the Synthux Touch synthesizer and Daisy Seed.

## Quick Install
Flash the precompiled [**TouchString.bin** (v2.1.0)](https://github.com/vincentltm/TouchString/releases/download/v2.1.0/TouchString.bin) directly using the [Daisy Seed web programmer](https://flash.daisy.audio/).

## Controls

<img src="touch.jpeg" width="300"/>

### Pads
- **P00** - Keyboard Octave Down (steps down up to -2 octaves) | Tap both P00 + P02 to reset to unison (0)
- **P01** - String Dampen / Palm-Mute Choke (squeeze to damp ringing strings, tap to mute)
- **P02** - Keyboard Octave Up (steps up up to +2 octaves) | Tap both P00 + P02 to reset to unison (0)
- **P03...P09** - Note pads (strings 1 to 7)
- **P10 (TO)** - Tap tempo & Modifier:
  - **Tap P10** in rhythm to set arpeggiator tempo (40–240 BPM)
  - Hold P10 + P00 - Tempo down
  - Hold P10 + P02 - Tempo up
  - Hold P10 + S33 - Pattern Shift
  - Hold P10 + S35 - Reverb Mix
  - Hold P10 + S36 - External Audio Input Volume
- **P11 (CH)** - Modifier | Hold for scale and voice mode selection:
  - P11 + P00 - Previous scale (cycles through all 7 scales)
  - P11 + P02 - Next scale (cycles through all 7 scales)
  - P11 + P03...P09 - Direct scale select (P03: Amara, P04: Oxalis, P05: Pigmy, P06: Major, P07: Natural Minor, P08: Minor Pentatonic, P09: Major Pentatonic)
  - P11 + P01 - Poly / Mono toggle (LED double-blink = Mono, single blink = Poly)
- **P10 + P11 (TO + CH)** - Hold both together for ~0.5s to recalibrate touch pad baselines (3 rapid blinks)

### Knobs (clockwise)
- S30 **Brightness** | Exciter filter cutoff & string brightness
- S31 **Pitch** | Global tuning across ±1 octave (center = unison)
- S32 **Timbre** | String structure / harmonic content
- S33 **Density** | Arpeggiator pattern density (Hold TO: Pattern Shift)
- S34 **Notes** | Arpeggiator pitch randomization (chance of octave jumps & random scale notes)
- S35 **String** | Per-note acoustic timbre variation (randomizes brightness, structure, damping) | Hold TO: Reverb Mix

### Faders
- S36 (left) **Drive / Volume** | Pull down to soften plucks and fade to quiet mute; push up for warm overdrive (Hold TO: External Input Gain)
- S37 (right) **Damp** | String sustain decay time (Hold TO: Reverb Mix)

### Switches
- **Left Switch — Exciter Mode**
  - **Up**: Pluck (velocity-sensitive, soft tap to snappy strike)
  - **Center**: Pluck + Bow on hold (squeeze pad to swell into singing bowed sustain)
  - **Down**: Bow (continuous violin-like bowing & scratch sensitivity)

- **Right Switch — Arpeggiator**
  - **Up**: Latched (hands-free arpeggio loop)
  - **Center**: Momentary (arpeggiates while holding pads)
  - **Down**: Off

### Polyphony & Expression
- **Poly / Mono Modes**: Toggle with P11 + P01. In Poly mode, all 7 strings run in stereo. In Mono mode, notes have last-note priority with centered stereo panning and smooth portamento.
- **Pluck + Bow Arpeggiator**: In Mode 1 (Left Switch center), holding a chord lets you bow a singing background chord while the arpeggiator plucks through the notes over top.
- **Bowed Arpeggiator**: In Bow mode (Left Switch down), the arpeggiator plays smooth bowed notes in sequence (stereo *bariolage* in Poly mode, singing legato in Mono mode).
- **Keyboard Octaves & Bass Drones**: Tapping P00 or P02 shifts the keyboard for new notes without interrupting currently held notes. You can hold a low bass drone with one hand, shift octaves, and pluck high melodies over it.
- **String Dampen (P01)**: Squeeze P01 for acoustic palm-muting across all strings, or tap it to silence resonance.
- **Velocity Sensitivity**: Responsive touch tracking from whisper-quiet soft plucks to bright, punchy fortissimo.
- **Aftertouch**: In Pluck mode, gently rocking a held pad adds subtle acoustic finger vibrato. In Bow mode, pad pressure modulates bow friction and timbre.

### Scales
Hold **P11 (CH)** and tap a note pad to jump directly to any scale, or use **P00 / P02** to cycle:
- **P03 — Amara**: Celtic minor handpan tuning (`C3, G3, Bb3, C4, D4, Eb4, G4`)
- **P04 — Oxalis**: Folk major handpan tuning (`C3, E3, F3, G3, A3, C4, F4`)
- **P05 — Pigmy**: Dorian Pygmy minor tuning (`C3, D3, Eb3, G3, Bb3, C4, Eb4`)
- **P06 — Major**: Diatonic Major (`C3, D3, E3, F3, G3, A3, C4`)
- **P07 — Natural Minor**: Diatonic Minor (`C3, D3, Eb3, F3, G3, Ab3, C4`)
- **P08 — Minor Pentatonic**: Blues & folk (`C3, Eb3, F3, G3, Bb3, C4, Eb4`)
- **P09 — Major Pentatonic**: Bright & sweet (`C3, D3, E3, G3, A3, C4, D4`)

### LED Indicator
The onboard LED provides rich visual feedback:
- **Visual Tempo Pulse**: When arpeggiator is active:
  - *Momentary Arp*: Pulses ON in rhythm with each quarter-note beat.
  - *Latched Arp*: Solid ON, dipping OFF briefly with each quarter-note beat.
- **Scale Confirmation**: Blinks $N$ times corresponding to scale 1 through 7 (Amara = 1 blink, ..., Major Pentatonic = 7 blinks).
- **Octave Shift**:
  - *Unison (0)*: 1 solid medium confirmation blink
  - *+1 / +2 Octaves*: 2 or 3 quick crisp blinks
  - *-1 / -2 Octaves*: 1 or 2 slow pulses
- **Voice Mode**: Double blink = Mono, single long blink = Poly.
- **Tap Tempo**: Flashes instantly on valid taps to confirm tempo sync.
- **Recalibration**: 3 rapid blinks confirm capacitive sensor baseline reset.

## Project Structure
```
TouchString/
├── TouchString.cpp      # Main application entry point
├── Makefile             # Build configuration
|-- common               # Configuration and utilities
├── string/              # Instrument core
├── touch/               # Simple Touch wrapper (pads, knobs, switches)
├── ui/                  # UI connecting instrument core with touch wrapper
└── lib/                 # Libraries
    ├── libDaisy/        # Daisy hardware abstraction
    └── DaisySP/         # DSP library
```

## Project Setup
```shell
$ git clone --recurse-submodules https://github.com/Synthux-Academy/TouchString.git
$ make libs -j8
$ make clean; make -j8
```

## Configuration
Edit [config.h](https://github.com/Synthux-Academy/TouchString/blob/main/common/config.h) to customize the instrument.
