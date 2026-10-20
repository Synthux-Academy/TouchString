# TouchString — Specs Index

Agent-oriented specs for the TouchString firmware: a 7-voice physical-modelling string synth for the Synthux **Simple Touch** board on a **Daisy Seed** (STM32H750).

Read this file first, then open only the spec you need.

| Spec | Covers | Source |
|---|---|---|
| [core-dsp.md](core-dsp.md) | `String` engine, `Vox` voice, `FastString` waveguide, exciter modes, poly/mono, master bus, arpeggiator / clock / pattern / latch, scales | `string/` |
| [ui.md](ui.md) | `StringUI`: control mapping, modifier layers, pressure routing, soft takeover (`MValue`), LED feedback | `ui/` |
| [hardware.md](hardware.md) | Pins, MPR121 touch controller, pad signal processing, ADC knobs, switches, audio config, build/flash/debug, flash budget | `touch/`, `TouchString.cpp`, `Makefile` |
| [midi.md](midi.md) | USB MIDI input: CC map, clock sync, limitations | `ui/string_ui.cpp` (`_process_midi`), `string/synclock.*` |

## Architecture in one picture

```
 main loop (~4–5 ms/iter)                         audio ISR (48 kHz, 48-sample blocks = 1 ms)
 ─────────────────────────                         ───────────────────────────────────────────
 StringUI::Process(hw)                             AudioCallback → String::Process(in, out, n)
   ├─ _process_midi()     (USB MIDI, if !DEBUG)       ├─ SynClock::Tick()  → _on_clock_tick()
   ├─ Touch::Process()    (MPR121 I2C burst read)     │      → Trigger → CPattern → Arp::Trigger()
   │    └─ Pads callbacks → _on_pad_touch/_release    │            → _on_arp_note_on/off → Vox
   ├─ read switches/knobs → String setters            └─ per sample: 7× Vox::Process → pan → body
   └─ LED state machine → hw.SetLed()                        filter → DC → drive → reverb → out
```

Global objects in [TouchString.cpp](../TouchString.cpp): `DaisySeed hw`, `Touch touch`, `String engine`, `StringUI ui(touch, engine)`.

Init order: `hw.Init()` → block size 48 / SR 48 kHz → `touch.Init(hw)` → 40× `touch.Process()` warm-up (3 ms apart) → `engine.Init(sr, block)` → `ui.Init(hw)` → `hw.StartAudio()` → loop `ui.Process(hw); System::Delay(4)`.

### Threading model (important)

- **Two contexts, no locks.** The UI/MIDI code calls `String` setters from the main loop, while the audio ISR runs `String::Process`. The arpeggiator's note events fire **inside the audio ISR** (from `SynClock::Tick`). All shared state is plain fields; correctness relies on single-word writes being atomic on Cortex-M7 and on the code tolerating torn multi-field updates.
- Do not allocate, block, or log from anything reachable from `String::Process` / `_on_clock_tick`.
- `std::function` callbacks are bound once at init; never rebind at runtime.

## Directory map

```
TouchString.cpp     entry point, audio callback, main loop
common/config.h     scales table, reverb constants, USB_MIDI define
common/log.h        DEBUG-only logging singleton (HW::hw().print, LOG* macros)
common/nocopy.h     NOCOPY(T) macro (deletes copy/move)
string/             engine: string.*, vox.h, arp.h, latch.h, cpattern.*, trigger.*, synclock.*, scale.*, xfade.h
touch/              Simple Touch HAL: pads.* (MPR121), knobs.*, switches.*, touch.h
ui/                 string_ui.* (control mapping), mvalue.* (soft takeover)
lib/libDaisy        submodule (Synthux-Academy fork)
lib/DaisySP         submodule (needs USE_DAISYSP_LGPL=1 for ReverbSc)
```

## "Where do I change X?" quick table

| Task | Go to |
|---|---|
| Add / edit a scale | `kStringScales` in [common/config.h](../common/config.h) (7 notes each, Hz; bump `kStringScalesCount`) — see [core-dsp.md § Scales](core-dsp.md#6-scales) |
| Change a knob's function | `StringUI::Process` in [ui/string_ui.cpp](../ui/string_ui.cpp) — see [ui.md](ui.md) |
| Add a pad gesture | `StringUI::_on_pad_touch` / `_on_pad_release` |
| Change pluck/bow sound | `Vox` in [string/vox.h](../string/vox.h) |
| Change string physics | `FastString` in [string/vox.h](../string/vox.h) |
| Change master FX chain | `String::Process` in [string/string.cpp](../string/string.cpp) |
| Arp timing / rhythm | `String::_on_clock_tick`, `Trigger`, `CPattern`, `Arp` |
| Add MIDI messages | `StringUI::_process_midi` — see [midi.md](midi.md) |
| Touch sensitivity | `Pads::Init` registers & `kDefaultMaxDeltas` in [touch/pads.*](../touch/pads.cpp) |
| Reverb tone | `kReverbFeedback`, `kReverLPFreq` in [common/config.h](../common/config.h) |

## Cross-cutting constraints

1. **Flash is nearly full.** `TouchString.bin` is 126–130 KB (130 240 B for the committed release) of the Seed's 131 072 B internal flash (`APP_TYPE=BOOT_NONE`). Any feature adding code size may fail to link. Options: trim code, avoid new `std::` heavy templates/`<random>` engines, or switch to `APP_TYPE=BOOT_SRAM`/`BOOT_QSPI` (requires Daisy bootloader). See [hardware.md § Flash budget](hardware.md#flash-budget).
2. **CPU budget:** 7 waveguide voices + ReverbSc per sample. `Vox`/`FastString` recompute expensive coefficients (`powf`, `atanf`) only on parameter change past thresholds and mark them `noinline`. Keep that pattern.
3. **Voice ↔ pad identity.** Poly mode uses fixed voice = scale degree = pad (`voice i` ↔ `P(03+i)`); there is no voice allocator. Mono mode uses only `_vox[0]`.
4. **Note numbers are scale-degree indices 0..6**, not MIDI note numbers.
5. **No persistence.** Nothing is saved to flash/QSPI; all state resets on power-up (scale 0 Amara, octave 0, poly, tempo ≈115.5 BPM, reverb 0).
6. No tests, no host build. Verification = build with `make`, flash, play.

## Glossary

| Term | Meaning |
|---|---|
| TO / P10 | Tempo/shift modifier pad |
| CH / P11 | "Change" modifier pad (scale, mono/poly) |
| Exciter mode | 0 Pluck, 1 Pluck + Bow-on-hold, 2 Bow (left switch) |
| Soft scale | 0..1 factor from the Drive fader's lower 30 % that scales velocity, bow and damping |
| Human note / string chance | Arp note randomisation (S34) / per-note timbre randomisation (S35) |
| PPQN | Pulses per quarter note: internal 48, external MIDI clock 24 |
| Soft takeover | `MValue`: a shared knob only takes effect after moving > 0.02 from where it was when its layer became active |
