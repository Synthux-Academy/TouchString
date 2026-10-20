# UI Spec — `ui/`

`synthux::StringUI` ([ui/string_ui.h](../ui/string_ui.h), [ui/string_ui.cpp](../ui/string_ui.cpp)) maps the Simple Touch controls (via `Touch`, see [hardware.md](hardware.md)) onto `String` setters (see [core-dsp.md](core-dsp.md)) and drives the Seed's onboard LED. It runs entirely in the **main loop**; one call to `StringUI::Process` = one **tick** ≈ 4 ms (`System::Delay(4)`) + I2C read time. All timing constants below are in ticks.

User-facing description of controls: [README.md](../README.md#controls). This spec is the code-level truth; where they differ, see §8.

## 1. `Process()` order (one tick)

1. `_process_midi()` (only if `USB_MIDI`) — [midi.md](midi.md)
2. `_touch.Process()` → MPR121 read → may fire `_on_pad_touch` / `_on_pad_release` callbacks synchronously
3. Latch modifier flags: `_is_to_touched = P10`, `_is_ch_touched = P11` (⚠️ updated *after* step 2, so callbacks see the previous tick's modifier state)
4. Recalibration gesture (§3)
5. First tick only: seed `_drive_value`, `_pattern_value`, `_human_string_value` with `SetTracking(knob)` and push to engine
6. Switches → exciter mode and arp mode (§2.3)
7. If arp just switched on: `NoteOn` for every currently held note pad (velocity = `Velocity(pad)`)
8. P01 dampen (§4)
9. Note-pad pressure routing (§4) — skipped entirely while CH (P11) is held
10. Knobs / faders (§2.2)
11. Beat pulse + LED (§6)

## 2. Control map

### 2.1 Pads (MPR121 electrode index = pad number)

| Pad | Plain | + TO (P10) held | + CH (P11) held | Release |
|---|---|---|---|---|
| P00 | Octave −1 (min −2); if P02 held → octave reset to 0 | `SlowDown()` (−0.05 tempo), blink(1,8,0) | Previous scale | — |
| P01 | Dampen/palm-mute (continuous, §4) | — | Toggle poly/mono | `SetDampenPad(false)` |
| P02 | Octave +1 (max +2); if P00 held → reset | `SpeedUp()`, blink(1,8,0) | Next scale | — |
| P03–P09 | `NoteOn(pad−3, vel)` — vel = `Pressure()` in Bow mode, else `Velocity()` | (same as plain) | Select scale `pad−3` directly | `NoteOff(pad−3)` |
| P10 (TO) | Tap tempo: interval 250–1500 ms → `SetBpm(60000/interval)` (40–240 BPM), blink(1,12,0) | modifier | — | — |
| P11 (CH) | Modifier only | — | — | — |
| P10+P11 | Hold > 120 ticks (~0.5 s) → `Pads::Recalibrate()`, blink(3,8,8); latched until one is released | | | |

Octave multipliers: `{-2:0.25, -1:0.5, 0:1, +1:2, +2:4}` → `String::SetCurrentOctaveMult` (applies to subsequently played notes only).

### 2.2 Knobs & faders

| Control | ADC | Plain | + TO held (soft takeover) |
|---|---|---|---|
| S30 knob | A0 | `SetBrightness` | — |
| S31 knob | A1 | `SetTransp` (±12 semitones, 25 steps) | — |
| S32 knob | A2 | `SetStructure` (timbre) | — |
| S33 knob | A3 | `SetPattern` (density 1..16 onsets) via `_pattern_value` | `SetPatternShift` via `_shift_value` |
| S34 knob | A4 | `SetHumanNoteChance` | — |
| S35 knob | A5 | `SetHumanStringChance` via `_human_string_value` | `SetReverbMix` via `_verb_value` |
| S36 fader (left) | A6 | `SetDrive` (volume/soft/drive) via `_drive_value` | `SetInputVolume` via `_in_vol_value` |
| S37 fader (right) | A7 | `SetDamping` | — |

Unlayered controls (S30, S31, S32, S34, S37) are written to the engine **every tick**, so anything else setting those parameters (e.g. MIDI CC) is overwritten within ~4 ms.

### 2.3 Switches (`Switch3`: `POS_UP` / `POS_CENTER` / `POS_DOWN`)

| Physical | Code | UP | CENTER | DOWN |
|---|---|---|---|---|
| Left — exciter | `switches().B()` (D9/D8) | 0 Pluck | 1 Pluck + Bow on hold | 2 Bow |
| Right — arp | `switches().A()` (D7/D6) | Arp on, latched | Arp on, momentary | Arp off |

Every tick: `SetExciterMode(mode)`, `SetLatch(pos == UP)`, `SetArpOn(pos != DOWN)` (engine only reacts to changes, except `SetLatch` which `Reset()`s while the arp is idle).

## 3. Modifier & gesture state

| Field | Meaning |
|---|---|
| `_is_to_touched`, `_is_ch_touched` | Modifier pads held (previous tick when read in callbacks) |
| `_hold_ticks[7]` | Ticks each note pad has been held (Pluck+Bow onset delay) |
| `_octave_shift` | −2..+2 |
| `_scale_index` | 0..`ScalesCount()−1` (mirrors engine) |
| `_exciter_mode` | 0/1/2 |
| `_was_arp_on` | Edge detect for step 7 |
| `_last_to_touch_time` | ms timestamp of last P10 touch (tap tempo) |
| `_recal_counter`, `_recal_latched` | Recalibration gesture |

## 4. Pressure routing (step 8–9)

**P01 dampen**: if P01 touched and CH not held → `SetDampenPad(true, Pressure(1))` each tick, else `SetDampenPad(false, 0)`.

**Note pads** (only when CH not held). For each voice `i` (pad `i+3`), `press = touched ? Pressure(pad) : 0`:

1. `SetPadPressure(i, press)` always (feeds arp dynamics + voice aftertouch).
2. Compute bow amount `bow_p`:
   - Mode 2 (Bow): `press` if arp off, else 0 (arp sets bow itself).
   - Mode 1 (Pluck+Bow): if held > 5 ticks (~20 ms) and `press > 0.08`: `clamp((press−0.08)/0.92 · 0.70, 0, 0.85)`, else 0.
   - Mode 0: 0.
3. **Poly**: if arp off **or** mode 1 → `SetVoicePressure(i, bow_p)`, `SetVoiceSustain(i, bow_p > 0.001)`. In mode 0 also `SetVoiceAftertouch(i, press)`.
   - Consequence: in Pluck+Bow with arp on, voices whose pads are not physically held get bow forced to 0 every tick, so only fingers on pads sustain a bowed chord under the arp.
4. **Mono**: only the pad at `ActiveMonoPad()` (top of mono stack) drives voice 0: pressure/sustain as above; aftertouch only in mode 0.

## 5. Soft takeover — `MValue` ([ui/mvalue.h](../ui/mvalue.h))

Used where one physical control has two layers (plain / +TO). Each layer has its own `MValue`; `Process(knob, active)` is called for both every tick with complementary `active` flags.

- `Process(v, active)`: inactive → returns stored value. On becoming active, remembers `init = v`; it starts following only after `|v − init| ≥ 0.02` (threshold `kTreshold`), then tracks until deactivated.
- `SetTracking(v)`: start already tracking (used at first tick so base layers match knob positions on boot).
- `Set(v)`: store a value without tracking (used by MIDI CC so the knob must move before overriding).

Pairs: S33 `_pattern_value`/`_shift_value`, S35 `_human_string_value`/`_verb_value`, S36 `_drive_value`/`_in_vol_value`. Boot values of the TO layers: shift 0, reverb 0, input volume 0.

## 6. LED

Single LED (`hw.SetLed`). Priority:
1. Active blink pattern (`_trigger_blink_pattern(count, on_ticks, off_ticks)`)
2. Latched arp: ON, dips OFF for 6 ticks on each beat
3. Momentary arp: OFF, pulses ON for 6 ticks on each beat
4. Off

Beat = `String::CheckBeatPulse()` (every quarter note from the clock, and on tap tempo). With `off_ticks = 0` each pulse is a single on-phase.

| Event | count, on, off (ticks) |
|---|---|
| Scale select (index n) | n+1, 7, 7 |
| Octave 0 / +1 / +2 | 1,35,0 / 2,6,6 / 3,6,6 |
| Octave −1 / −2 | 1,16,12 / 2,16,12 |
| Mono / Poly | 2,10,10 / 1,28,0 |
| Tempo up/down | 1,8,0 |
| Valid tap | 1,12,0 |
| Recalibrate | 3,8,8 |

A new pattern replaces the running one.

## 7. Recipes

- **New pad gesture**: add a branch in `_on_pad_touch` (and `_on_pad_release` if needed). Remember callbacks see last tick's modifier flags; query `_touch.pads().IsTouched(n)` for the current state.
- **New shifted knob layer**: add an `MValue` member pair, call both `Process` with complementary `active` each tick, and `SetTracking` the base one on first run.
- **New LED feedback**: call `_trigger_blink_pattern`; keep patterns distinguishable from existing ones (§6).
- **New engine parameter**: add setter in `String` (normalised 0..1), call it in `Process` step 10; optionally add a MIDI CC ([midi.md](midi.md)).

## 8. Known issues / discrepancies

- README says "S37 (Hold TO: Reverb Mix)": not implemented — reverb is only on TO + S35.
- Pressing TO (P10) as a modifier also counts as a tap; two TO presses 250–1500 ms apart change the tempo.
- Modifier flags lag one tick behind pad callbacks; pressing TO/CH and a target pad in the same ~4 ms tick is treated as unmodified.
- `Process` comment "Timbre (sample-and-hold at pluck time)" is stale: brightness/structure/damping are applied live.
- `Knobs` are initialised with `hw.AudioCallbackRate()` (1 kHz) but processed at ~250 Hz, so the `AnalogControl` slew is ~4× slower than its nominal 2 ms (harmless, but wrong if you rely on it).
- `kNotesCount = 8` in `StringUI` is unused.
