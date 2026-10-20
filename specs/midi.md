# MIDI Spec

MIDI is **USB-device, input-only**. Code: `StringUI::_process_midi()` in [ui/string_ui.cpp](../ui/string_ui.cpp); clock handling in [string/synclock.cpp](../string/synclock.cpp).

## 1. Enablement

- [common/config.h](../common/config.h): `#if !DEBUG → #define USB_MIDI`. A `DEBUG=1` build has **no MIDI** (the USB port is used for serial logging instead).
- `StringUI` member `daisy::MidiUsbHandler _midi`, initialised with a default `MidiUsbHandler::Config` in `StringUI::Init` (internal USB FS, Seed micro-USB).
- No UART/TRS MIDI is initialised. No MIDI output.
- Polled once per UI tick (~4 ms) at the start of `StringUI::Process`: `ProcessClockIn(false)`, `_midi.Listen()`, then drain `HasEvents()/PopEvent()`.
- **No channel filtering** — messages on all 16 channels are accepted.

## 2. Implemented messages

### 2.1 Control Change (value `v/127` → 0..1)

| CC | Target | Persists? |
|---|---|---|
| 70 | `SetBrightness` + `_brightness_value.Set` | ✅ until S30 is moved (> 0.02) |
| 71 | `SetTransp` (pitch, ±12 st) + `_transp_value.Set` | ✅ until S31 is moved |
| 72 | `SetStructure` (timbre) + `_structure_value.Set` | ✅ until S32 is moved |
| 73 | `SetPattern` (density) + `_pattern_value.Set` | ✅ until S33 is moved without TO |
| 74 | `SetHumanNoteChance` + `_note_chance_value.Set` | ✅ until S34 is moved |
| 75 | `SetHumanStringChance` + `_human_string_value.Set` | ✅ until S35 is moved without TO |
| 76 | `SetDamping` + `_damping_value.Set` | ✅ until S37 is moved |
| 77 | `SetReverbMix` + `_verb_value.Set` | ✅ until TO + S35 is moved |
| 78 | `SetPatternShift` + `_shift_value.Set` | ✅ until TO + S33 is moved |
| 79 | `SetDrive` + `_drive_value.Set` | ✅ until S36 is moved without TO |
| 123 | `String::Reset()` (All Notes Off) | — |

Every CC with a knob counterpart stores its value via `MValue::Set` (soft takeover, [ui.md §5](ui.md#5-soft-takeover--mvalue-uimvalueh)): the CC value holds until the knob (on the matching layer) moves more than 0.02 from where it was when the CC arrived; from then on the knob controls the parameter again. Knob positions are not changed or reported back, so the first knob movement may jump from the CC value to the knob position.

### 2.2 System Real-Time
- `TimingClock` (0xF8) → `String::ProcessClockIn(true)` → `SynClock::Process` rising edge → `_external_clock_tick()`.
- Start (0xFA) / Continue (0xFB) / Stop (0xFC) and Song Position are ignored.

### 2.3 Not handled
Note On/Off (explicit TODO: engine notes are scale-degree indices 0..6, not MIDI notes), pitch bend, channel/poly aftertouch, program change, SysEx.

## 3. External clock sync

- `SynClock` is constructed with `ppqn_in = 24` (`kPPQNExtern`) and `ppqn_out = 48` (`kPPQN`): each MIDI clock allows 2 internal ticks; between pulses the internal clock free-runs at the estimated tempo and holds if it gets ahead, re-syncing and correcting `_tempo_mks` on every pulse (algorithm after Paul Messick's *Maximum MIDI*).
- **External mode is active only when the internal tempo is < 40 BPM** (`_external_clock()` ⇔ `_manual_tempo < kBPMMin`). The UI has no explicit switch: tempo-down (TO + P00) to its floor (normalised 0.05 → 39.5 BPM) enters it. Tap tempo or tempo-up leaves it. While in external mode with no MIDI clock arriving, the arp is silent.
- When entering external mode while running, the clock waits (`_is_about_to_run`) and starts on the next received pulse.
- Edge detection limitation: `ProcessClockIn(false)` is sent once per tick and every `TimingClock` sends `true`, so **multiple clocks drained in the same tick collapse into one edge**. Fine at normal tempos (24 PPQN at 240 BPM = 10.4 ms/pulse), but loop jitter (I2C timeouts, recalibration's ~85 ms block) drops pulses. A robust fix is to call a dedicated `SynClock::_external_clock_tick()`-equivalent per message instead of edge-detecting a level.
- Clock events are processed in the main loop, while `SynClock::Tick()` runs in the audio ISR — the same unsynchronised shared state as the rest of the engine.

## 4. Adding MIDI note input (recipe)

1. In `_process_midi`, handle `NoteOn` (velocity 0 ⇒ NoteOff) and `NoteOff` via `msg.AsNoteOn()/AsNoteOff()`.
2. Map MIDI note → `(degree 0..6, octave mult)`. Options: fixed key range (e.g. notes 60–66 = degrees 0–6), or nearest degree of the current scale (scale freqs are in `kStringScales`, root C3 = MIDI 48). The engine has no chromatic pitch path; arbitrary pitches would need a new `String` API (e.g. `NoteOnFreq`).
3. Octave is taken from `String::_current_oct_mult` at note-on. Setting it for MIDI notes changes the pad octave too — save/restore it, or add a per-call octave parameter.
4. Call `_string.NoteOn(degree, vel / 127.f)` / `NoteOff(degree)`. Works for poly, mono and arp (latch/arp path) like pad input. In poly mode two MIDI notes mapping to the same degree share one voice.
5. Mind flash space ([hardware.md § Flash budget](hardware.md#flash-budget)).
