# FIADA

**Facio Ludum Autocinetum et Doceo Cerebrum Computatri Currus Agere** — a Latin-inspired name for a racing game built to train a neural network to drive.

FIADA is currently a playable top-down 2D racing prototype with a flowing, gold-themed technical circuit rendered entirely with [Skia](https://skia.org/). Its small deterministic game core is intended to grow into a neural-network training environment.

## Play

- `W` / `Up`: accelerate
- `S` / `Down`: brake and reverse
- `A D` / arrow keys: steer
- `Space`: hold to drift; release a charged drift for a mini-turbo
- `R`: reset the car

Complete a clockwise lap. Leaving the asphalt dramatically changes grip and rolling resistance. The camera uses a central dead zone and follows only within the authored circuit bounds; driving entirely off-screen resets the car.

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

