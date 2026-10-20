# Hardware Spec — `touch/`, platform, build

Target: **Electrosmith Daisy Seed** (STM32H750IB, Cortex-M7 @ 480 MHz, onboard stereo codec) mounted on the **Synthux Simple Touch** board. HAL wrapper: `synthux::Touch` ([touch/touch.h](../touch/touch.h)) aggregating `Pads`, `Knobs`, `Switches`.

## 1. Pin / peripheral map

| Function | Peripheral / pin | Code |
|---|---|---|
| 12 capacitive pads P00–P11 | MPR121 @ I2C addr `0x5A`, **I2C_1**, SCL = PB8, SDA = PB9, 400 kHz | `Pads::Init` |
| S30–S37 (6 knobs + 2 faders) | ADC `seed::A0`–`A7` (single-ended) | `Knobs::Init` |
| Right switch (arp) | `Switch3` on `seed::D7`, `D6` → `Switches::A()` | `Switches::Init` |
| Left switch (exciter) | `Switch3` on `seed::D9`, `D8` → `Switches::B()` | `Switches::Init` |
| LED | Seed onboard user LED (`hw.SetLed`) | `StringUI::Process` |
| Audio in/out | Seed codec, stereo, 48 kHz, block 48 | `TouchString.cpp` |
| USB | Seed micro-USB: USB MIDI device (release) or serial log (`DEBUG=1`) | `StringUI::Init`, `log.h` |

Knob index → name: `_knobs[0..7]` = S30..S37. S36 = left fader, S37 = right fader. Board photo: [touch.jpeg](../touch.jpeg).

## 2. Audio configuration

`hw.SetAudioBlockSize(48)` (1 ms) and `SAI_48KHZ`. Callback: `engine.Process(in, out, size)`; non-interleaved `in[0..1][i]`, `out[0..1][i]`. Input is summed to mono and used only as string excitation (see [core-dsp.md §2](core-dsp.md#2-signal-flow-per-sample-stringprocess)). `SynClock` timing assumes the block period (`1e6·block/sr` µs), so changing block size or SR is handled automatically, but larger blocks coarsen arp timing.

## 3. Touch pads — MPR121 driver ([touch/pads.cpp](../touch/pads.cpp))

The driver talks raw registers via `I2CHandle` (blocking, 10 ms timeout). `Touch` also declares an unused `daisy::Mpr121I2C _mpr` and `_state`.

### 3.1 Register setup (`Pads::Init`)
| Reg | Value | Purpose |
|---|---|---|
| 0x80 | 0x63 | Soft reset (then 5 ms) |
| 0x5E | 0x00 | Stop mode for configuration |
| 0x41+2i / 0x42+2i | 6 / 3 | Touch / release threshold, all 12 electrodes |
| 0x2B–0x2E | 1,1,0x0E,0 | Rising baseline filter (MHDR, NHDR, NCLR, FDLR) |
| 0x2F–0x32 | 1,1,0x10,4 | Falling baseline filter (slow, so presses aren't absorbed) |
| 0x33–0x35 | 0 | Touched baseline filter off |
| 0x5B | 0x11 | Debounce 1 / 1 |
| 0x5C | 0xD0 | CONFIG1: FFI = 34 samples, CDC 16 µA |
| 0x5D | 0x51 | CONFIG2: CDT 1 µs, SFI 10, ESI 2 ms |
| 0x7D/7E/7F | 200/130/180 | Auto-config USL / LSL / TL |
| 0x7B/7C | 0xCB / 0x00 | Auto-config control |
| 0x5E | 0x8C | Run: 12 electrodes, baseline tracking (then 80 ms) |

`main()` then calls `touch.Process()` 40× with 3 ms delays to let baselines settle before the engine starts.

`Pads::Recalibrate()` = ECR stop → 5 ms → ECR 0x8C → 80 ms, and clears all per-pad state (blocks the main loop ~85 ms; audio keeps running).

### 3.2 Per-poll processing (`Pads::Process`, once per UI tick)
Burst-read 42 bytes from reg 0x00: touch status (bytes 0–1), filtered data (10-bit, bytes 4–27), baselines (bytes 0x1E–0x29, `<<2`).

For each pad `i`:
1. `delta = max(0, baseline − filtered)`; `delta < 3` forces "not touched" (anti-stuck guard).
2. **Touch onset** (hardware touched, was not, `release_lockout == 0`): mark touched immediately, open a **strike window** of 4 polls, start peak tracking.
3. **Release**: needs 2 consecutive untouched polls (`_debounce_cnt`), then `pressure = 0`, `release_lockout = 3` polls, fire `on_release`.
4. **Pressure** (while touched): `norm = clamp((delta − 5)/max(maxDelta[i] − 5, 30))`, `target = norm²` (0 if delta < 6). Inside strike window pressure = target; afterwards one-pole smoothing `p += (target − p)·0.40`. Untouched pressure decays with the same factor, snapping to 0 below 0.01.
5. **Velocity & note-on**: the strike window ends after 4 polls, or early once `peak > 30` and delta has fallen 12 below peak. Then `n = clamp((peak − 5)/effmax)`, `velocity = clamp(0.35n + 0.65√n, 0.03, 1)` and `on_touch(i)` fires. ⇒ note-on latency is up to ~4 ticks (~16–20 ms) after contact, by design, to measure the strike.
6. Very short taps (released before the window closes) still fire `on_touch` (if `peak ≥ 6`) followed by `on_release` in the same poll.

Accessors: `IsTouched(pad)`, `HasTouch()`, `Pressure(pad)` (0..1, squared curve), `Velocity(pad)` (latched at strike), `operator[]` = pressure. Callbacks are `std::function<void(uint16_t)>` set by `StringUI::Init` and are invoked synchronously inside `Process()` (main loop).

### 3.3 Calibration
`Pads::kDefaultMaxDeltas` — per-pad delta treated as full pressure: 375 for most pads, **460 for P04**, **425 for P08/P09** (pad geometry differences). Tune here if a pad can't reach full velocity or saturates too early. Global sensitivity: touch/release thresholds (6/3) and the `delta < 3` / `delta >= 6` constants in `Process`.

### 3.4 Failure behaviour
If the MPR121 doesn't ACK, `ReadBurst` returns false and `Process` returns without changes — pads are dead, and each failed read can block the main loop up to the 10 ms I2C timeout.

## 4. Knobs ([touch/knobs.cpp](../touch/knobs.cpp))
`AdcChannelConfig::InitSingle(A0..A7)`, `hw.adc.Init(cfg, 8)`, each wrapped in `daisy::AnalogControl` initialised with `hw.AudioCallbackRate()` (default 2 ms slew, no flip/invert). `hw.adc.Start()` is called in `Touch::Init`. `AnalogControl::Process()` is called from the UI loop (~250 Hz), not the audio callback — so the effective slew is slower than nominal. Values are 0..1, clockwise / up = 1.

## 5. Switches ([touch/switches.cpp](../touch/switches.cpp))
Two `daisy::Switch3` (3-position). `Read()` returns `Switch3::POS_UP`, `POS_CENTER`, `POS_DOWN`. No debouncing beyond libDaisy's; polled every UI tick.

## 6. Timing budget

| Context | Period | Work |
|---|---|---|
| Audio ISR | 1 ms (48 samples) | Clock tick + arp events, 7 × `Vox` (inactive voices skipped), body filter, drive, ReverbSc, per sample |
| Main loop | ~4 ms + I2C burst (~1 ms at 400 kHz) | USB MIDI poll, MPR121 read, UI mapping, LED |

CPU headroom isn't measured in-repo. If you add DSP, measure with `daisy::CpuLoadMeter` (in a `DEBUG` build) — the voice/waveguide code is already hand-optimised (§core-dsp 3.8).

## 7. Build, flash, debug

Prereqs: Daisy toolchain (`arm-none-eabi-gcc`, `dfu-util`, `openocd`), submodules checked out.

```sh
git submodule update --init --recursive
make libs -j8          # once: builds lib/libDaisy and lib/DaisySP
make clean; make -j8   # firmware → build/TouchString.{elf,bin,hex}
make program-dfu       # USB DFU (hold BOOT, tap RESET on the Seed)
make program           # ST-Link via openocd
```

- `Makefile`: `CPP_SOURCES` = `TouchString.cpp`, `touch/*.cpp`, `string/*.cpp`, `ui/*.cpp` (new `.cpp` files in those dirs are picked up automatically; header-only is fine too). Includes `-Ilib/ -Icommon/`, `gnu++17`, `USE_DAISYSP_LGPL = 1` (required for `ReverbSc`). Optimisation `-O2` from libDaisy core Makefile.
- `DEBUG=1 make` (or VS Code task `build_all_debug`): defines `DEBUG`, which **disables USB MIDI** (`common/config.h`) and enables `HW::hw()` logging (`LOGFLOAT/LOGINT/LOGBOOL/LOGMSG` in [common/log.h](../common/log.h), printed over USB serial). Optimisation stays `-O2`.
- VS Code: tasks `build`, `build_and_program`, `build_and_program_dfu`, `build_all`; `launch.json` "Cortex Debug" (openocd + ST-Link, SVD `.vscode/STM32H750x.svd`).
- A prebuilt release binary is committed at repo root (`TouchString.bin`); README links the GitHub release. Users can flash it with the Daisy web programmer.

## Flash budget

Linker script `STM32H750IB_flash.lds` (`APP_TYPE = BOOT_NONE`, default): **FLASH 128 KiB = 131 072 B** at 0x08000000. Current binaries: `build/TouchString.bin` 128 184 B (97.8 %, after the new clock), committed root `TouchString.bin` 130 240 B (~0.8 KiB free). Effectively full.

Before adding features:
- Check the size after `make` (`arm-none-eabi-size build/TouchString.elf`). Link errors like "region FLASH overflowed" mean you're over.
- Cheap savings: remove dead code listed in [core-dsp.md §7](core-dsp.md#7-invariants--gotchas); avoid extra `<random>` engines/distributions, `std::function`, `printf`-style formatting, new `powf`/`expf` call sites pulling in libm variants.
- Bigger room: build with `APP_TYPE = BOOT_SRAM` (512 KB, code copied from QSPI) or `BOOT_QSPI` — requires flashing the Daisy bootloader first (`make program-boot`) and changes the flashing workflow for end users; coordinate with maintainers.
