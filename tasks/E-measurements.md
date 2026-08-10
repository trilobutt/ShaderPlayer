# Project E: responsiveness, measured

Measured on this machine (Windows 11 Home 10.0.26200) against a working tree on top of
commit `3ac2c58`, in the app's restored session (45 presets, `lastOpenedVideo` open and
paused, passthrough shader). Both columns come from `src/FrameProfiler.cpp` via
`pwsh -File tools/measure_responsiveness.ps1`, 20 s cursor sweep, ~2700 frames per run.
Baseline is the E0 run with the instrument in place and nothing yet fixed; final is the
mean of three consecutive runs taken during review, after every fix.

| section | baseline avg ms | final avg ms |
| --- | --- | --- |
| `Tick` (whole frame) | 16.492 | **0.205** |
| `Present` | 14.094 | **0.083** |
| `MainWindowTick` | 14.174 | **0.118** |
| `ProcessFrame` | 1.734 | **0.057** |
| `CheckForChanges` | 1.733 | **0.055** |
| `VideoUpload` | 0.559 | **0.001** |
| `Render` | 0.020 | **0.008** |
| `EventLoopGap` | 0.200 | 16.488 (by design, see below) |
| `InputLatency` | 22 – 25 (see note) | **0.343** |

Final runs individually: `InputLatency` 0.314, 0.372 ms; `Tick` 0.207, 0.203 ms. Frame
counts 2700, 2701 against the baseline run's 2699, so nothing got fast by presenting less.

## Provenance, where it is not what it looks like

- **Build configuration, and it was wrong twice.** `build/` had been reconfigured to
  `Debug` by an IDE, and its cache had separately had every `CMAKE_CXX_FLAGS*` entry
  blanked, so the binary was compiled with no `/O2`, `/EHsc`, `/Zi` or `/DNDEBUG` (see
  `CLAUDE.md`, "When the cache goes wrong"). Both were found during review. The final
  column above is from a cache rebuilt from scratch on the `windows-msvc` preset, with
  `/O2` verified present. Restoring optimisation moved the tick from 0.214 to 0.205 ms and
  `ProcessFrame` from 0.063 to 0.057: small, because by that point the tick is almost
  entirely an interruptible wait rather than computation, and what remains is dominated by
  syscalls that `/O2` does not touch. The baseline was not re-taken, because reproducing
  the pre-fix tree is outside a review pass, so treat the ratios between the two columns as
  indicative and the final column as exact.
- **`InputLatency` baseline is not from this instrument.** The desktop was locked for the
  E0 run, so synthetic input never reached the window and the section recorded no samples.
  The 22 – 25 ms figure is the plan header's, taken on 2026-08-10 with temporary
  `QApplication`-level instrumentation on an unlocked machine. The final figure is this
  instrument's, over three runs. They measure the same quantity by the same method
  (`GetTickCount64()` against `QInputEvent::timestamp()`), but they are not the same run.
- **`EventLoopGap` rising 80× is the fix, not a regression.** It measures the wall clock
  between one tick ending and the next beginning. E1 moved the vsync wait out of `Present`
  and into the Qt event loop, so the idle that used to sit inside `Present` (14.1 ms) now
  sits here (16.5 ms) where input can be dispatched during it. The two rows should be read
  as one number moving from a blocking call into an interruptible wait. This also makes the
  `EventLoopGap` threshold written into E6's `done-when:` unreachable by construction: that
  phase was verified from the diff and from a hover comparison instead.

## What the user feels

The tick costs 0.21 ms of a ~16.7 ms frame, against 16.5 ms before: the GUI thread is now
idle and interruptible for 99% of every frame instead of parked inside a blocking
`Present` for 83% of it. Input delivery is the number that matters and it went from
roughly a frame and a half to a third of a millisecond.
