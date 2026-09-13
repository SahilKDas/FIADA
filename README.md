# FIADA

**Facio Ludum Autocinetum et Doceo Cerebrum Computatri Currus Agere** — a Latin-inspired name for a racing game built to train a neural network to drive.

FIADA is currently a playable top-down 2D racing prototype with a long, flowing, gold-themed 29-sector technical circuit rendered entirely with [Skia](https://skia.org/). Its small deterministic game core is intended to grow into a neural-network training environment.

## Play

- `W` / `Up`: accelerate
- `S` / `Down`: brake and reverse
- `A D` / arrow keys: steer
- `Space`: hold to drift; release a charged drift for a mini-turbo
- `Shift`: use the held Diamond, Feather, or Gold Key
- `P`: toggle the trained reinforcement-learning driver
- `R`: reset the car

Complete a clockwise lap. Leaving the asphalt applies heavy drag and caps the car to roughly one-third of its road speed. The closer camera uses a central dead zone and follows only within the authored circuit bounds; driving entirely off-screen resets the car.

## Checkpoints and AI

Laps use twenty-nine ordered directional key checkpoints. Each gate must be crossed through the road-width span in the forward direction, preventing reverse-and-forward lap exploits. The bundled 14-12-4 MLP was trained locally with procedural warm-starting followed by episodic cross-entropy reinforcement learning. The native trainer is authoritative; `python scripts/train_policy.py` can generate a procedural 232-parameter warm start.



### Native reinforcement training

The production trainer links the same `game.cpp` as FIADA and advances the exact nonlinear physics at 120 Hz. Its curriculum begins at the canonical grid, then randomizes course spawn, heading, initial speed, tire grip, and desired racing-line offset. Off-screen exits terminate the episode with a penalty.

```powershell
cmake -S . -B build-native-trainer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-native-trainer --target FIADA_train
./build-native-trainer/FIADA_train.exe ./assets/policy/fiada_policy.bin
./build-native-trainer/FIADA_train.exe --evaluate ./assets/policy/fiada_policy.bin
```

The current policy observes inventory and matching-shortcut proximity and has a dedicated learned item-use output. On the 35-sector item track, native randomized reward improved from 1981.17 to 3041.18. The frozen holdout completed 26 of 33 laps with zero screen escapes, collected 50 items, used 24, entered 18 item-specific shortcuts, released 67 drift mini-turbos, and completed the canonical grid lap.

## Deterministic items and shortcuts

Item boxes contain no random-number calls. Their Diamond, Feather, or Gold Key result is a deterministic integer mix of box identity, lap, checkpoint, arrival tick bucket, approach speed, and lateral approach band. Repeating a box therefore requires reproducing the entire approach precisely, while identical simulation inputs remain reproducible. Diamond powers the crystal speed cut, Feather opens the narrow white terrain bypass, and Gold Key opens the gold technical lane. Using a mismatched item does not make another lane drivable.
## Physics

FIADA uses a compact nonlinear bicycle model: longitudinal/lateral velocity, yaw rate, control lag, slip-angle tire saturation, weight transfer, and a friction circle. Drifting gently reduces rear grip and adds a mild yaw assist; a controlled slide quickly charges a release-triggered mini-turbo. This stays cheap for batched neural-network episodes while making braking, counter-steer, racing lines, and boost timing meaningful.

## Build on Windows

FIADA compiles in GCC's C++26 draft mode (\`-std=c++2c\`) and uses the same local MSYS2 UCRT64 Skia 108 package as the neighboring Eloi project. Bootstrap the ignored local dependency directory, build, and package with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap-windows.ps1
powershell -ExecutionPolicy Bypass -File scripts/build-windows-exoskeleton.ps1
```

The package is staged at `dist/exoskeleton/FIADA-windows-x64-exoskeleton` and zipped beside it. It deliberately contains one executable; its GCC runtime DLLs, PNG assets, licenses, hashes, and provenance remain external.

## Artwork pipeline

Original vector art lives in `assets/svg`. Runtime-ready transparent PNGs live in `assets/png`. Rebuild them with:

```powershell
powershell -ExecutionPolicy Bypass -File tools/convert_svgs.ps1
```

The converter uses a local Chromium browser as a standards-compliant SVG rasterizer. The game itself decodes and renders the PNGs through Skia.

## Roadmap

1. Separate a fixed-timestep simulation from presentation.
2. Expose ray-cast track sensors and normalized vehicle state.
3. Add headless episodes and reward calculation.
4. Train and replay a neural-network driver.

