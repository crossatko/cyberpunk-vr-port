# xr_probe — reading another mod's headset geometry

Two small tools that record the full OpenXR conversation of any VR application: what the headset
reports, what the application asks the runtime to allocate, the per-eye pose and FOV every frame,
and exactly what it submits back.

The immediate purpose is to read Quest 3 numbers out of **R.E.A.L. VR** (`RealVR64.dll`, loaded as
a `dxgi.dll` proxy) while running against the **Meta XR Simulator**, so this port can be developed
against real Quest 3 geometry on a Pico 4.

| | |
|---|---|
| `XR_APILAYER_CPVR_probe.dll` | an OpenXR **API layer**. Registers once, then records every OpenXR application automatically. |
| `xrprobe.exe` | a minimal OpenXR application. Prints what the active runtime reports, without launching a game. Also the layer's smoke test. |

## Why a layer and not a hook into RealVR

What we want is not really RealVR's private state — it is its conversation with the runtime, and
all of that crosses the OpenXR ABI, which is versioned and documented. Reading it there means no
RVAs, no pattern scans and nothing that breaks when RealVR ships a new build. The same probe works
unchanged against our own plugin, which makes A/B comparison trivial: run both, diff the summaries.

RealVR is already on the right path for this — its `PreferredAPI2=2` selects the OpenXR backend,
and `MetaXRSimulator` is one of the runtimes it recognises by name.

## Build

```
cmake --build build --config Release --target xr_probe_layer
cmake --build build --config Release --target xr_probe_cli
```

Both land in `build\bin\xr_probe\Release\`. Neither is part of `cyberpunkvrport_stereo` and neither
is ever deployed into the plugin folder.

## Use

```powershell
# once, from an elevated shell
.\register_probe.ps1

# check what is registered, in both scopes
.\register_probe.ps1 -List

# read the current runtime without launching anything
..\..\build\bin\xr_probe\Release\xrprobe.exe

# remove it again
.\unregister_probe.ps1
```

Registration must go in **HKLM**, not HKCU: the OpenXR loader ignores both `XR_API_LAYER_PATH` and
HKCU-registered layers for elevated processes — measured, the layer simply does not appear in the
loader's list. HKLM works either way, and it is where every other layer on this machine already
lives (ReShade, the OBS mirror, the Virtual Desktop compatibility layer).

**Registration is machine-wide and always on.** Every OpenXR application starts writing to
`%LOCALAPPDATA%\xrprobe` until you unregister. `setx XRPROBE_DISABLE 1` silences it without
unregistering.

### Output

Two files per run in `%LOCALAPPDATA%\xrprobe` (override with `XRPROBE_DIR`):

* `xrprobe-<exe>-<time>-<pid>.jsonl` — one JSON object per event
* `xrprobe-<exe>-<time>-<pid>.txt` — the human summary, rewritten on every static change and at exit

| variable | default | |
|---|---|---|
| `XRPROBE_DIR` | `%LOCALAPPDATA%\xrprobe` | output directory |
| `XRPROBE_FRAMES_FULL` | 120 | log the first N frames in full |
| `XRPROBE_FRAME_STRIDE` | 90 | after that, sample 1 frame in N |
| `XRPROBE_ALL` | 0 | 1 = every frame, no sampling |
| `XRPROBE_DISABLE` | — | set to anything to switch the layer off |

Static facts are always logged in full regardless of sampling, and so is any *change* in a value
that is supposed to be static.

## Capturing RealVR

1. Restore RealVR as the game's `dxgi.dll` (the 11 142 656-byte backup in `bin\x64` is it).
2. Make sure this port's own plugin is not also loaded — two mods both driving OpenXR will fight
   over the session.
3. Launch the game, get into VR, quit.
4. Read `xrprobe-Cyberpunk2077-*.txt`.

RealVR's own `RealVR64.log` is worth reading alongside it: it already prints *"Reported/Adjusted
%s eye FOV"*, *"Pixels per degree"*, *"Optimal total vertical FOV"* and *"Detected display cant"*.
The probe gives the runtime's raw truth; the RealVR log gives what RealVR derived from it. Together
they show the whole transformation.

## Measured: Meta Quest 3 via Meta XR Simulator v201.0

```
system            Meta Quest 3, vendorId 0x00002833
per-eye resolution  1680 x 1760 recommended   (max 8192 x 8192, up to x4 MSAA)
refresh             72 Hz  (13.889 ms)
FOV, left eye       L -54.000  R +40.000  U +50.000  D -49.000   deg   (H 94, V 99)
FOV, right eye      L -40.000  R +54.000  U +50.000  D -49.000   deg   (H 94, V 99)
IPD                 60.0 mm
cant                0.000 deg as eye yaw,  7.000 deg as FOV asymmetry
```

### The cant trap

A canted panel can be described two equivalent ways: rotate the eye pose and keep the frustum
symmetric, or keep the pose straight ahead and skew the frustum. **Runtimes disagree about which
to use, and the Quest 3 profile here uses the second one** — zero eye yaw, 14° of horizontal FOV
asymmetry, i.e. 7° of cant per eye hidden entirely in the projection.

That matters directly: code detecting cant from the eye orientation concludes "symmetric" and
builds a projection 7° off per eye. RealVR's `adjust_reported_eye_fov` measures the FOV asymmetry
and so catches it; its *"Detected display cant"* log line prints the pose euler angles and will
read 0.00 on this runtime. Both numbers are in the summary, separately, for exactly this reason.

For comparison, the Pico 4 is genuinely symmetric — zero on both measures.

## What this does NOT capture

The engine-side camera matrix. RealVR converts the XR pose into a REDengine 4 camera transform and
writes it through its own hooks; that never crosses the OpenXR boundary, so no layer can see it.
Reading it needs either a hook on the game's camera site (this port already knows it — the
`FinalCamera` site at `+0x785578`, camera object at `[rdi+0x18]`, position as int32 fixed-point at
`+0x70/74/78` scaled by 131072, rotation rows at `+0xC0/D0/E0`) or RealVR's cached result globals
from `RealVR_reverse_project`. That is a separate, RealVR-version-dependent tool and is not built
here.
