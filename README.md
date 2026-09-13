# FIADA

**Facio Ludum Autocinetum et Doceo Cerebrum Computatri Currus Agere** — a Latin-inspired name for a racing game built to train a neural network to drive.

FIADA is currently a playable top-down 2D racing prototype with a long, flowing, gold-themed 18-sector technical circuit rendered entirely with [Skia](https://skia.org/). Its small deterministic game core is intended to grow into a neural-network training environment.

## Play

- `W` / `Up`: accelerate
- `S` / `Down`: brake and reverse
- `A D` / arrow keys: steer
- `Space`: hold to drift; release a charged drift for a mini-turbo
- `P`: toggle the trained reinforcement-learning driver
- `R`: reset the car

Complete a clockwise lap. Leaving the asphalt dramatically changes grip and rolling resistance. The camera uses a central dead zone and follows only within the authored circuit bounds; driving entirely off-screen resets the car.

## Checkpoints and AI

Laps use eighteen ordered directional key checkpoints. Each gate must be crossed through the road-width span in the forward direction, preventing reverse-and-forward lap exploits. The bundled 8-12-3 MLP was trained locally with procedural warm-starting followed by episodic cross-entropy reinforcement learning. Run `python scripts/train_policy.py` to retrain and export the 147 parameters.



### Native reinforcement training

The production trainer links the same `game.cpp` as FIADA and advances the exact nonlinear physics at 120 Hz. Its curriculum begins at the canonical grid, then randomizes course spawn, heading, initial speed, tire grip, and desired racing-line offset. Off-screen exits terminate the episode with a penalty.

```powershell
cmake -S . -B build-native-trainer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-native-trainer --target FIADA_train
./build-native-trainer/FIADA_train.exe ./assets/policy/fiada_policy.bin
./build-native-trainer/FIADA_train.exe --evaluate ./assets/policy/fiada_policy.bin
```

The current compact policy is an early checkpoint, not a solved agent: on the expanded 18-sector circuit its frozen native baseline completed 1 of 33 holdout laps, reached checkpoint 4 canonically, and escaped the screen in 1 of 33 episodes. The external binary is loaded at runtime so training can continue without recompiling the game.

## Physics

FIADA uses a compact nonlinear bicycle model: longitudinal/lateral velocity, yaw rate, control lag, slip-angle tire saturation, weight transfer, and a friction circle. Drifting reduces rear grip; only a sustained controlled slide charges a release-triggered mini-turbo. This stays cheap for batched neural-network episodes while making braking, counter-steer, racing lines, and boost timing meaningful.

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

