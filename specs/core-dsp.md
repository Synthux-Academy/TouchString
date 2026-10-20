# Core DSP Spec — `string/`

The sound engine: class `synthux::String` ([string/string.h](../string/string.h), [string/string.cpp](../string/string.cpp)) owns 7 voices, the master bus and the arpeggiator. It knows nothing about pads or knobs; it only exposes setters taking normalised values (0..1) and scale-degree note indices (0..6).

## 1. Component map

| File | Class | Role |
|---|---|---|
| `string.h/.cpp` | `String` | Engine façade: note handling, poly/mono, exciter modes, master bus, arp glue |
| `vox.h` | `Vox` | One voice: exciter (pluck burst + bow friction + ext input) → SVF LP → `FastString` → DC block |
| `vox.h` | `FastString` | Extended Karplus-Strong waveguide (optimised port of Mutable Instruments *Rings* `String`) |
| `scale.h/.cpp` | `Scale` | Current scale frequencies + ±12 semitone transpose table |
| `synclock.h/.cpp` | `SynClock` | Internal clock (48 PPQN) with external-clock (24 PPQN) sync |
| `trigger.h/.cpp` | `Trigger` | Divides clock to 16th notes (swing supported, unused) |
| `cpattern.h/.cpp` | `CPattern` | 16-step Euclidean/Christoffel rhythm with rotation |
| `arp.h` | `Arp<8,4>` | Note list (sorted linked list + input order), as-played stepping |
| `latch.h` | `Latch<8>` | Momentary/latched hold of arp input notes |
| `xfade.h` | `XFade` | Square-law crossfade, used as reverb send gain |

Constants: `String::kVoicesCount = 7`, `String::kNotesCount = 8` (arp/latch capacity; note index 7 is never produced), `kPPQN = 48`, `kPPQNExtern = 24`.

## 2. Signal flow (per sample, `String::Process`)

```
in L,R ──(L+R)/2 × _input_volume ──SoftLimit(×1.5)──┐  (only if _input_volume > 0.005)
                                                    │ ext × 0.40 fed into EVERY voice's exciter
Poly:  Σ_v Vox[v].Process(ext) × kPanL/R[v]  × 0.65 ┤
Mono:  Vox[0].Process(ext) × 0.7071 (both channels) ┘
   → body formant: x·0.76 + SVF(310 Hz, res 0.40).Band·0.24   (per channel)
   → DcBlock
   → Overdrive (single shared instance, stateless) × _volume  = bus
   → reverb send = bus × (2m − m²)   (XFade with lhs = 0; m = reverb "mix")
   → ReverbSc (feedback kReverbFeedback=0.8, LP kReverLPFreq=10 kHz)
   → out = SoftLimit(bus + wet)       (dry is always 100 %; "mix" is really a send level)
```

`SynClock::Tick()` is called once at the top of every block (1 ms), so arp events are block-quantised.

Pan law tables (`kPanL/kPanR`) map pads left→right: v0 far L, v1 mid L, v2 centre, v3 mid R, v4 far R, v5 centre-L, v6 centre-R.

## 3. Voice (`Vox`)

### 3.1 State
`_base_freq` (target before bends) → `_target_freq` (with aftertouch bend / bow flatten) → `_current_freq` (slewed). Effective string freq = `clamp(_current_freq × _freq_mult, 20, 8000)`; `_freq_mult` is the global transpose.

### 3.2 Excitation sources (summed into `exc`)
1. **Pluck burst** — on `NoteOn(freq, vel)`: white noise for `_remaining_impulse = min(1/f0, 1024)` samples (≈ one period) at amplitude `strike_gain × 0.70`, where `strike_gain = vel·(0.05 + 0.95·vel)·clamp(√(f/261.63), 0.6, 1.2)`. Also sets `_accent = clamp(0.04 + 0.9·vel, .02, .95)`.
2. **Bow** — `_bow_env` slews toward `_bow_pressure` (rise 0.004/sample, fall 0.008/sample). Stick-slip friction:
   `v_rel = bow_env − 0.6·last_out; friction = v_rel/(1+4.5·v_rel²); bow_force = (friction − static(bow_env))·bow_env·0.28·freq_scale` plus rosin noise `noise·bow_env·0.045·(0.3+|v_rel|)·freq_scale`. `freq_scale = clamp(√(220/f), 0.35, 1.4)`.
3. **External input** — added directly (strings resonate sympathetically with audio in).

### 3.3 Exciter filter
`daisysp::Svf` low-pass, res 0. Cutoff (normalised) = `clamp(3.5·f0·bright_ratio, 1.5·f0, max)`, `max` blends 0.33 (pluck) → 0.09 (bow) by `clamp(bow_env·2.5)`. `bright_ratio = 2^((eff_b(2−eff_b) − 0.5)·72/12)`, `eff_b = brightness + 0.3·accent·(1−brightness)`, where *accent* = `_bow_env` while bowing else `_accent`. Filter freq only re-set if it moves > 5 Hz.

### 3.4 String params pushed into `FastString`
`brightness = clamp(eff_b, 0, .95)`, `damping = clamp(damping + 0.2·accent·(1−damping), 0, .95)`. Recomputed on parameter change and whenever `_bow_env` moves > 0.02.

### 3.5 Structure (timbre) mapping — `Vox::SetStructure`
| structure | non-linearity | character |
|---|---|---|
| < 0.24 | `(s−0.24)·4.166` ∈ [−1, 0) | "curved bridge" buzz (sitar-like) |
| 0.24–0.26 | 0 | pure KS |
| > 0.26 | `(s−0.26)·1.351` ∈ (0, 1] | dispersion (stiff / inharmonic, all-pass stretch + noise above 0.75) |

### 3.6 Pitch modulation
- **Pluck aftertouch** (only when not bowing): 12 000-sample (250 ms) lockout after `NoteOn`; at its end the resting pressure is captured as baseline (`clamp(.1,.85)`). Pressure > baseline + 0.06 bends **up** to +1.5 % (~+25 cents), smoothing 0.003/sample.
- **Bow flattening**: up to −0.3 % when `bow_env > 0.55`.
- **Slew**: `_current_freq` approaches target at 0.02/sample (pluck) or 0.0025/sample (bowing) — the latter is the mono-mode portamento. `SetFreq()` jumps immediately unless the voice is active **and** bowing; `NoteOn()` always jumps.

### 3.7 Activity / CPU gating
A voice goes inactive after 2400 consecutive samples (50 ms) with `|out| < 5e-5` and no impulse/bow/ext input; inactive voices return 0 without computing. Any `|ext_in| > 1e-4`, `NoteOn`, `SetSustain(true)` or bow pressure > 0.001 reactivates. `FastString` resets itself on NaN/Inf.

### 3.8 `FastString`
Delay line 1024 samples (+ 256 all-pass stretch line). Damping cutoff (semitones above f0) = `min(12 + d²·60 + b·24, 84)`; `d ≥ 0.95` ramps toward infinite sustain. Low notes whose period exceeds 1020 samples (< ~47 Hz) run with sample-rate conversion (`src_ratio_ < 1`) and linear crossfade. Coefficients are precomputed in `_update_*` on change — keep `powf/atanf` out of the per-sample path.

## 4. Engine behaviour (`String`)

### 4.1 Exciter modes (`SetExciterMode(int)`, 0/1/2)
Changing mode zeroes bow pressure and sustain on all voices.

| Path | 0 Pluck | 1 Pluck + Bow on hold | 2 Bow |
|---|---|---|---|
| `NoteOn`, arp off, poly | `NoteOn(freq, vel)`, bow off | same as 0 (bow is added later by UI pressure) | `SetFreq` + `SetBowPressure(vel)` (no pluck) |
| `NoteOn`, arp off, mono | pluck on `vox[0]` | `vox[0].NoteOn` (keeps bow) | `vox[0].SetFreq` (glide) + bow |
| `NoteOff`, arp off, poly | sustain/bow off | sustain/bow off | sustain/bow off |
| `NoteOff`, mono, stack not empty | nothing (no re-pluck) | glide to previous note **if** bowing, bow ×0.65 | glide + bow at previous note's pressure (min 0.6 if < 0.2) |
| Arp step (`_on_arp_note_on`) | pluck vel `clamp(√p,.25,1)` (0.85 if p ≤ .05) | pluck + sustain bow `clamp((p−.08)/.92·.5,.2,.7)` (0.35 default) | bow `0.24+0.36p` (0.35 default), sustain on |
| Arp note-off | bow/sustain off | kept while pad pressure > 0.08 | mono: kept; poly: off |

`p` = `_pad_pressure[num]` (live pad pressure written by UI each loop). Arp dynamics never use MIDI-style velocity — `Arp` always stores 127.

### 4.2 Poly vs mono (`SetMono`, toggled from UI)
- Poly: voice index = note index; up to 7 simultaneous notes, each its own pan.
- Mono: only `_vox[0]` is rendered (centre). Last-note priority via `_mono_stack` (size ≤ 7). `ActiveMonoPad()` returns top of stack (UI routes that pad's pressure to voice 0). In arp mode each arp step resets voices 1..6.
- Switching mode clears the stack and `Reset()`s all voices.

### 4.3 Octave & pitch
- `SetCurrentOctaveMult(m)` (UI: 0.25/0.5/1/2/4) affects **new** notes only: on note-on the value is stored in `_voice_oct_mult[note]`. Held notes keep their octave (bass-drone gesture).
- `SetTransp(v)` → `Scale::TransMult` → 25 semitone steps, −12..+12, centre = unison; applied live to all voices via `Vox::SetMult`.
- `SetScaleIndex(i)` retunes all voices immediately (keeps per-voice octave).
- Final freq: `scale[i] × voice_oct_mult[i] (× humanize octave/note) × trans_mult × (1 + bend)`, clamped 20–8000 Hz.

### 4.4 Timbre setters (all 0..1)
| Setter | Effect |
|---|---|
| `SetBrightness(v)` | All voices, live: `v + _bright_offset[i]` |
| `SetStructure(v)` | All voices, live: `v + _struct_offset[i]` |
| `SetDamping(v)` | `_damping = 0.7·v`; effective = `_damping·(0.25 + 0.75·soft_scale)` × dampen choke + `_damp_offset[i]` |
| `SetDampenPad(active, p)` | Choke multiplier `clamp(1 − 1.25p, .001, 1)`; bow pressure scaled by `clamp(1 − 1.4p)` in `SetVoicePressure`; p > 0.4 kills all bows/sustain |
| `SetDrive(v)` | v ≤ 0.3: drive 0.2, `soft_scale = v/0.3`, volume = soft²; v > 0.3: soft = 1, `u=(v−.3)/.7`, drive `0.2+0.3u`, volume `(1−0.55u)²` (loudness compensation) |
| `SetInputVolume(v)` | External audio level into strings |
| `SetReverbMix(v)` | Send = `2v − v²` |
| `SetHumanStringChance(v)` | 0..100 % chance per note-on to randomise brightness/structure (±0.15) and damping (±0.2) offsets for that voice (`_humanize_and_apply`); ≤ 2 % → off |
| `SetHumanNoteChance(v)` | Arp only, see §5.5 |
| `SetVoicePressure/Sustain/Aftertouch(i, …)` | Per-voice bow pressure, sustain flag, aftertouch target (UI drives these every loop) |
| `SetPadPressure(i, p)` | Stores `_pad_pressure[i]` (used by arp dynamics) and sets voice aftertouch |

`soft_scale` also scales note velocity (`vel·(0.15 + 0.85·soft)`) and arp bow pressure (`·(0.25 + 0.75·soft)`).

### 4.5 `Reset()`
Stops clock (if arp off), resets trigger/pattern, clears arp + latch, clears mono stack, sets all `_voice_oct_mult` to current octave, clears humanize offsets, zeroes sustain/bow/aftertouch on all voices. Called on arp on/off transitions, when the arp runs out of notes, from `SetLatch` whenever arp is on with no notes (i.e. every UI loop while idle), and by MIDI CC 123.

## 5. Sequencing (arpeggiator)

Chain (inside audio ISR): `SynClock` 48 PPQN tick → `String::_on_clock_tick` → beat pulse every 48 ticks → `Trigger::Tick()` (true every 12 ticks = 16th) **and** `CPattern::Tick()` (true if step is an onset) → `Arp::Trigger()` → `_on_arp_note_on/off`.

### 5.1 Note input path when arp is on
`String::NoteOn(num)` → stores octave → `Latch::note_on` → `Arp::NoteOn(num, 127)`. Clock runs while the arp has notes. `NoteOff` → `Latch::note_off` → (if not latched) `Arp::NoteOff`; if arp empties → `Reset()`.

### 5.2 `SynClock`
- Driven by `Tick()` each audio block (`update_interval = 1e6·48/48000 = 1000 µs`).
- Normalised tempo `n` → BPM = `190·n + 30`. Default `n = 0.45` → **115.5 BPM**.
- `SpeedUp/SlowDown`: ±0.05 n (~±9.5 BPM), clamped to [0.05, 1].
- `SetBpm(bpm)` (tap tempo): clamps 40–240, recomputes `n`, resets phase if running, raises beat pulse.
- **External-clock mode** is implicit: when BPM < 40 (`n` < ~0.053). In that mode internal ticks only advance between external pulses (`Process(state)` rising edges, 24 PPQN → 2 internal ticks each) with tempo correction. ⚠️ `SlowDown` floor 0.05 ⇒ 39.5 BPM ⇒ external mode: tapping tempo-down to the bottom silently switches to MIDI clock sync and the arp stops until MIDI clock arrives (tap tempo or tempo-up exits it).

### 5.3 `Trigger(48, Every::_16th)`
Fires every 12 ticks. Swing is implemented (`SetSwing`) but never called → straight 16ths.

### 5.4 `CPattern`
16 steps. `SetOnsets(v)`: onsets = `round(v·15) + 1` (1..16), distributed by the Christoffel-word algorithm (Euclidean). `SetShift(v)`: rotation `round(v·16)` steps. Pattern position advances only on Trigger hits.

### 5.5 `Arp<8, 4>`
Configured at init: forward, `as_played = true` (cycles in press order, not pitch order), rand chance 0, non-legato (previous note gets note-off before next note-on). Template `ppqn = 4` makes `Trigger()` step on every call. Holds 8 notes; when full, the oldest is stolen. Internally a doubly linked list sorted by pitch with sentinel slot 0 plus `_input_order[]`.

Note randomisation (`_humanized_note_freq`, S34, arp only), chance c = 0..100:
| c | Behaviour |
|---|---|
| ≤ 2 | off |
| 3–32 | with prob c: octave ±1 (50/50) |
| 33–65 | with prob c: 50 % random scale note, then 25 % −1 oct / 25 % +1 oct |
| ≥ 66 | prob c random scale note; always 20 % +1 oct / 20 % −1 oct |

### 5.6 `Latch<8>`
- Latch off: note_off releases immediately.
- Latch on: released notes stay held. On a new `note_on`, every held note that is not physically down is released first ⇒ "lift all fingers and play a new chord" replaces the old one; adding a finger while still holding others extends it.
- Turning latch off releases held notes that are not physically down.

## 6. Scales

`kStringScales` in [common/config.h](../common/config.h): 7 scales × 7 frequencies (Hz), all rooted at C3 (130.81 Hz). Order = pad order for direct select (P03..P09) and LED blink count (1..7). `Scale::_trans_index` stays at 12 (unison); transposition is applied via `TransMult` → `Vox::SetMult`, not baked into `_scale`.

To add a scale: append a row, increment `kStringScalesCount`. UI direct-select only reaches 7 scales (one per note pad); P00/P02 cycling reaches all. LED blinks `index+1` times.

## 7. Invariants & gotchas

- Voice index == note index in poly; never introduce dynamic allocation without updating UI pressure routing (`ui.md` §4) and pan tables.
- Arp callbacks run in the ISR; UI setters run in main loop. Don't add multi-step state transitions that must be atomic without guarding them.
- `_humanize_and_apply` sets voice damping from raw `_damping` (ignoring soft-scale/choke); `_update_damping` corrects it on the next UI loop (~4 ms).
- `SetBrightness/SetStructure/SetDamping` apply **live** to ringing voices (UI comments saying "sample-and-hold at pluck time" are stale).
- `Overdrive` instance is shared by both channels — fine only because it is stateless.
- Dead code (safe to remove for flash space): `String::SetNoteFreq`, `String::SetBowPressure`, `String::SetSustain`, `String::IsNoteLatched`, `String::_note_hold/_note_on`, `String::InputVolume`, `Vox::BowStroke`, `Vox::IsActive`, `Trigger::SetSwing`, `CPattern::SetMaxOnsets/Length`, `SynClock::Tempo`.
- `Latch::_on` has no initialiser; safe only because `String` is a global (zero-initialised). Initialise it if `String` is ever instantiated elsewhere.
