# FIADA — I Built a Racing Game That Trains Its Own Driver

**Format:** Faceless development video  
**Target length:** 6–8 minutes  
**Assets:** FIADA gameplay, AI spectator mode, code editor, terminal training output, track overview, evaluation report

## 0:00 — Cold open

**Visual:** Rapid montage: human drift, mini-turbo flash, AI taking a shortcut, item box pickup, terminal evaluation. Keep game audio underneath.

**Narration:**

> This racing game has nonlinear tire physics, deterministic items, item-specific shortcuts, and a neural-network driver with more than one million parameters.
>
> I built all of it in C++—and then trained the AI directly against the same simulation that you can play.
>
> This is FIADA.

**On-screen text:** `1,011,461 parameters • C++26 • Skia • No item RNG`

## 0:20 — What FIADA means

**Visual:** Title card, followed by the repository README and the game launching.

**Narration:**

> FIADA stands for “Facio Ludum Autocinetum et Doceo Cerebrum Computatri Currus Agere.”
>
> It is a Latin-inspired way of saying that I made a car game—and taught a computer brain to drive it.
>
> The goal was not just to draw a car following a perfect line. I wanted a compact simulation with enough depth to challenge both a human player and a learning agent.

## 0:52 — The game

**Visual:** Full lap in human mode. Show acceleration, braking, a controlled drift, leaving the road briefly, and resetting.

**Narration:**

> FIADA is a top-down racer running at 120 physics updates per second.
>
> The car uses a compact nonlinear bicycle model. It tracks longitudinal and lateral velocity, yaw rate, steering lag, slip angles, weight transfer, tire-force saturation, and a friction circle.
>
> That sounds complicated, but the result is simple: speed changes how the car turns, careless steering creates instability, and grass is genuinely slower than asphalt.
>
> Going off-road applies heavy drag and limits the car to roughly one-third of its normal road speed. If the car escapes the visible world entirely, the episode ends—or the human player resets.

**On-screen graphic:** `velocity • yaw • slip • weight transfer • grip`

## 1:38 — Drifting and mini-turbos

**Visual:** Hold Space through a curve. Zoom into the drift-charge HUD, then release and show the turbo.

**Narration:**

> Drifting is not a single boost button. Holding Space changes rear grip and yaw behavior, and a controlled slide builds charge.
>
> Release at the right moment and the stored charge becomes a mini-turbo. Release too early and you get almost nothing. Hold the slide carelessly and you lose the racing line.
>
> The handling is assisted enough to be playable, but the tire model is still active underneath it.

## 2:12 — The track and checkpoints

**Visual:** Slowly pan over the complete gold circuit. Highlight several chicanes and hairpins. Overlay checkpoint gates or demonstrate that reversing over the finish line does not create a lap.

**Narration:**

> The gold-themed circuit contains 35 ordered sectors, with chicanes, uneven hairpins, switchbacks, and several possible routes.
>
> A lap is protected by directional checkpoints. The car must cross every required gate in the correct order and direction, so driving backward and forward across the finish line cannot fake progress.
>
> The camera looks ahead of the car instead of trailing behind it, keeping upcoming corners visible. When spectating the AI, holding Tab advances the entire simulation at double speed.

## 2:52 — Deterministic items

**Visual:** Collect several item boxes using visibly different approach lines. Show Diamond, Feather, and Gold Key in the HUD.

**Narration:**

> FIADA has item boxes, but they do not call a random-number generator.
>
> The result is calculated deterministically from the box identity, lap, checkpoint, arrival timing, speed, and lateral approach.
>
> Identical inputs always produce the same result—but consistently farming one item requires reproducing an entire approach precisely.
>
> There are three items: Diamond, Feather, and Gold Key. Each one activates only its matching shortcut. A Diamond cannot open the Feather route, and using an item out of sequence cannot award shortcut progress.

**On-screen text:** `Deterministic ≠ predictable`

## 3:38 — Why the first AI was not enough

**Visual:** Show old metrics or recreated failure clips: repeated line, leaving the road, ignoring items. Then cut to the small policy size in code.

**Narration:**

> The first drivers were tiny, reactive neural networks. They could follow the track, but they had no memory and very little awareness of what was coming next.
>
> They repeated the same line, ignored parts of the control system, and struggled to connect item decisions with shortcuts later in the lap.
>
> Making the track harder exposed the limitation clearly: the AI needed more context, not just more training on the same eight numbers.

## 4:10 — The million-parameter driver

**Visual:** Animate the architecture as four blocks. Then show relevant constants in `neural_policy.hpp` and the binary model size.

**Narration:**

> The current controller has 1,011,461 float parameters and occupies just over four megabytes.
>
> It receives 32 observations, including four look-ahead horizons, vehicle slip, yaw, road state, drift charge, active turbo, held item, shortcut direction, and position around the lap.
>
> Those observations enter a 768-unit recurrent layer, followed by a 512-unit dense layer and five outputs: throttle, brake, steering, drift, and item use.
>
> The recurrent state gives the driver memory. It can connect what happened a moment ago with the decision it needs to make now.

**On-screen diagram:** `32 inputs → 768 recurrent → 512 dense → 5 controls`

## 4:58 — Training without a heavyweight framework

**Visual:** Terminal running `FIADA_train.exe`; alternate with code from the trainer and accelerated AI gameplay.

**Narration:**

> The trainer is also native C++. It links against the production game simulation, so training and gameplay use the same physics, checkpoints, items, and penalties.
>
> First, a deterministic expert generates demonstrations online. The model learns those controls through behavioral-cloning gradients.
>
> Next, it is refined on randomized starting positions, headings, speeds, grip values, and racing-line offsets.
>
> Finally, targeted curricula exercise Diamond, Feather, and Gold Key entries independently.
>
> No training dataset needs to remain on disk. Intermediate state is temporary, and the only large persistent artifact is the final four-megabyte model.

## 5:45 — Results

**Visual:** AI mode completing the canonical lap. Show item uses, mini-turbos, and each shortcut. Overlay evaluation numbers one at a time.

**Narration:**

> In the accepted evaluation, the recurrent driver completed the canonical lap in 45.019 seconds.
>
> It completed 28 of 33 randomized holdout races, with zero off-screen escapes.
>
> Across the suite it collected 57 items, used 30, released 15 mini-turbos, and passed dedicated capability tests for all three item-specific shortcuts.
>
> The model contains more than one million parameters, but the complete persistent AI data is only about 4.05 megabytes. The packaging script enforces a hard 100-megabyte AI budget.

**On-screen metrics:**

- `Canonical lap: 45.019 s`
- `Holdouts: 28 / 33`
- `Off-screen escapes: 0`
- `Model: 4.05 MB`

## 6:28 — Closing

**Visual:** Clean uninterrupted AI lap, optionally at 2× spectator speed. End on the FIADA title and repository tree.

**Narration:**

> FIADA started as a small experiment: make a racing game, then teach a computer to drive it.
>
> It became a test bed for physics, deterministic game design, reinforcement-learning ideas, and the strange ways an AI exploits whatever information you give it.
>
> The driver is much smarter now, but it is not finished. Faster racing lines, stronger recovery, opponents, and richer item strategy all create new problems to solve.
>
> And that is exactly why I built it.

**On-screen text:** `FIADA — built to race, built to learn`

## Recording checklist

- Record gameplay at 1080p60; keep the simulation at its normal 120 Hz physics rate.
- Capture one human lap, one AI lap, one 2× AI segment, all three shortcuts, an off-road penalty, and a mini-turbo.
- Record terminal training and evaluation separately so commands and metrics stay readable.
- Use slow zooms or highlighted crops for code; do not show the entire editor at once.
- Keep music below narration and briefly raise game audio for turbo and item moments.
- Blur usernames or unrelated filesystem paths if they appear in terminal footage.
