# FIADA

**Facio Ludum Autocinetum et Doceo Cerebrum Computatri Currus Agere** — a Latin-inspired name for a racing game built to train a neural network to drive.

FIADA is a top-down 2D racer with a gold-themed 35-sector circuit, deterministic Horn and Diamond-on-a-Rod items, player-chosen off-road cuts, and a recurrent neural driver rendered through Skia.

## Start and choose a mode

FIADA opens on a title screen. Press Enter or controller A, choose Quick Race, Championship, or AI Lab with W/S, arrow keys, triggers/D-pad, then confirm with Enter or A. Press Escape or controller B during play to return to mode selection.

## Play

- `W` / `Up`: accelerate
- `S` / `Down`: brake and reverse
- `A D` / arrow keys: steer
- `Space`: hold to drift; release for a mini-turbo
- `Shift`: use Horn or Diamond on a Rod
- `Tab`: hold for 2× simulation speed while spectating AI mode only
- `P`: toggle the trained driver
- `R`: reset

Off-road terrain applies heavy drag and caps speed near one-third road speed. The camera leads the car to show upcoming corners.

## Recurrent AI

The external policy is a `32 → 768 recurrent → 512 → 5` controller with 1,011,461 float32 parameters (4,045,844 bytes). It sees four track-lookahead horizons, vehicle/slip state, inventory, drift/turbo state, rival proximity, off-road need, and lap phase. It outputs throttle, brake, steering, drift, and item use. Inference runs at 30 Hz while physics remains at 120 Hz. A small deterministic safety driver is used only when the external model is missing or invalid.

The dependency-free native trainer generates expert trajectories online, applies behavioral-cloning gradients, refines against randomized production simulation, and finishes with deterministic item curricula. It persists no dataset or optimizer checkpoints.

```powershell
cmake -S . -B build-native-trainer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-native-trainer --target FIADA_train
./build-native-trainer/FIADA_train.exe ./assets/policy/fiada_policy.bin
./build-native-trainer/FIADA_train.exe --evaluate ./assets/policy/fiada_policy.bin
```

Accepted evaluation after item fine-tuning and contact smoothing: canonical lap in 45.928 seconds; 31/33 randomized races completed; 0/33 escaped; 16 mini-turbos; 46 pickups; 19 item uses; and direct activation tests pass for both new items. See `assets/policy/evaluation.json`.

## Deterministic items

Item boxes make no random-number calls. Item selection deterministically mixes box identity, lap, checkpoint, arrival tick bucket, speed, and lateral line. Each item activates only its matching shortcut, and ordered checkpoint state rejects out-of-sequence shortcut credit.

## Build

FIADA uses GCC C++26 draft mode (`-std=c++2c`) and local MSYS2 UCRT64 Skia 108 dependencies mirrored from Eloi.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap-windows.ps1
powershell -ExecutionPolicy Bypass -File scripts/build-windows-exoskeleton.ps1
```

The one-executable exoskeleton keeps DLLs, PNGs, the model, licenses, hashes, and provenance external. Packaging fails if persistent AI data exceeds 100 MB.

Original SVG art is in `assets/svg`; Skia-ready PNGs are in `assets/png`.
## Grand Prix expansion

FIADA now stages eight-driver races: one player and seven deterministic rivals. The five profile slots emphasize aggression, defense, shortcuts, drift, and recovery. Drafting, contact impulses, live placement, a three-second grid countdown, deterministic rival items, and forward-camera racing run inside the same 120 Hz simulation.

Five championship geometries are available with `[` and `]`: Gold Circuit, Alpine Switchbacks, Volcanic Foundry, Coastal Causeway, and Neon City. AI Lab opens a five-track picker before launching; `L` toggles its telemetry overlay. The Lab exposes policy inputs/outputs, a recurrent hidden-state summary, and the live rival tournament order without changing simulation state.

Xbox-compatible XInput controllers use the left stick to steer, triggers to accelerate/brake, `A` to drift, `X` to use an item, shoulder buttons to change circuit, Back for the Lab. Escape, Backspace, or controller B returns to the menu. Keyboard control remains available. A redistributable CC0 engine loop and its provenance manifest live under `assets/audio`.

Run `FIADA.exe --grand-prix-smoke` to replay five paired seeded simulations and verify deterministic hashes and valid eight-driver placement.
## Effects, contact, and audio

Cars use oriented footprint contacts matching their rendered length and width. Collision and guardrail separation is capped per physics tick to prevent lateral snapping. Horn pressure rings, Diamond-on-a-Rod tether/gem effects, turbo trails, and contact sparks expose forces visually. The engine loop starts only after the window exists; --audio-smoke validates the packaged WinMM playback path.
