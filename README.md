# Ringstop – live feedback suppressor (VST3 + standalone for Windows)

Ringstop listens for feedback (a single frequency that rings and grows) and drops a
very narrow notch filter exactly on it, deepening the cut if the ring continues.
It adds **zero latency** — the notches are IIR filters, and detection runs on a
background thread so the audio path is never delayed.

## What gets built

| File | Use it in |
|---|---|
| `Ringstop.vst3` | Any VST3 host: Reaper, Cantabile, Gig Performer, Cubase, Studio One, etc. |
| `Ringstop.exe` | Standalone app — pick your Flow 8 as input/output and run it live |

## Option A: build in the cloud (no software to install)

1. Create a free GitHub account and a new repository.
2. Upload everything in this folder (including the hidden `.github` folder).
3. Open the **Actions** tab. The "Build Windows plugin" job runs automatically (about 10–15 min).
4. When it turns green, open the run and download **Ringstop-Windows** at the bottom.

## Option B: build on your own PC

1. Install **Visual Studio 2022 Community** (free). In the installer tick
   **Desktop development with C++** (this includes CMake).
2. Install **Git for Windows**.
3. Open **Developer PowerShell for VS 2022**, go to this folder and run:

   ```
   cmake -B build -A x64
   cmake --build build --config Release
   ```

   The first run downloads JUCE and takes several minutes.
4. Results:
   - `build\Ringstop_artefacts\Release\VST3\Ringstop.vst3`
   - `build\Ringstop_artefacts\Release\Standalone\Ringstop.exe`

## Installing the VST3

Copy the whole `Ringstop.vst3` folder to `C:\Program Files\Common Files\VST3\`
and rescan plugins in your host.

## Using it live

- Insert it on each vocal/instrument mic channel that can feed back (before reverb).
- **Strength** — higher reacts faster and cuts deeper (up to 24 dB) but may catch
  long held notes. 50–70 % is a good start.
- **Auto-release** — notches fade out after 20 s without feedback. Turn it off to
  keep notches fixed for the whole show (common after "ringing out" during soundcheck).
- **Clear notches** — start fresh.
- Ring out at soundcheck: with Ringstop engaged, slowly raise the monitor level
  until it catches the first few frequencies, then back off a few dB.

For the standalone app, open *Options → Audio/MIDI Settings*, choose
**Windows Audio (Exclusive Mode)** and your Flow 8, and set the buffer as low as
runs without clicks (64–128 samples).

## Testing

`cmake -B build -DRINGSTOP_BUILD_TESTS=ON` then build and run `RingstopTest`.
It feeds a growing 1.85 kHz tone plus noise through the plugin and checks it is
detected and cut by more than 10 dB.

## Before you sell it

- **JUCE licence:** free for personal/small-company use under JUCE's starter terms,
  or AGPLv3. Check https://juce.com/get-juce for the current revenue limit.
- Change `COMPANY_NAME`, `PLUGIN_MANUFACTURER_CODE` and `PLUGIN_CODE` in `CMakeLists.txt`.
- Code-sign the .exe/.vst3 so Windows SmartScreen doesn't warn users, and package
  an installer (e.g. Inno Setup).

## Project layout

- `Source/FeedbackDetector.*` – FFT analysis thread: finds narrow, persistent peaks and assigns notches
- `Source/PluginProcessor.*` – audio path: 16 notch filters per channel, parameters, saved state
- `Source/PluginEditor.*` – GUI: spectrum with notch markers, strength knob, switches
