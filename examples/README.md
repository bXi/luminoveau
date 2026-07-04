# Luminoveau Examples

A ladder of small, self-contained demos. Each shows one or two engine features so you can
learn how to build a game by reading short, focused programs.

Each demo lives in its own directory with a single `main.cpp` (and an optional `assets/` folder)
that implements the four app callbacks the engine drives: `AppInit`, `AppIterate`, `AppEvent`,
`AppQuit`.

## Demos

**Scaffolds** (`00_*`) are starting points to copy, not single-feature demos:

| Scaffold | Purpose |
|----------|---------|
| `00_1_empty_project` | Blank scaffold — copy this to start a new project |
| `00_2_game_shell` | Game structure: state machine (title / play / pause / game over) |

**Feature demos** — each showcases one or two engine features:

| # | Demo | Teaches |
|---|------|---------|
| 01 | `01_bouncing_ball` | Window + frame loop, `Draw` primitives, delta-time motion |
| 02 | `02_pong` | Mouse input, AABB collision, game state, on-screen text |
| 03 | `03_snake` | Keyboard input, grid logic, fixed timestep |
| 04 | `04_breakout` | Brick grid + collision, audio sound effects |
| 05 | `05_sprite_animator` | Sprite-sheet animation (rolling die), `Draw::TexturePart` |
| 06 | `06_asteroids` | Device-agnostic input (keyboard + gamepad), momentum, screen wrap |
| 07 | `07_top_down_shooter` | Mouse aim, many objects (sprite batching), circle collisions |
| 08 | `08_platformer` | Tilemap + AABB collision, gravity/jump, following 2D camera |
| 09 | `09_3d_walker` | 3D camera + lights, model instances, first-person movement |
| 10 | `10_particles_fountain` | GPU particle system, config, following emitter |
| 11 | `11_shadertoy` | Custom GLSL fragment shader over the whole screen (effect system) |
| 12 | `12_post_fx` | Stackable full-screen post-processing effects, toggled at runtime |

Demos 04 and 05 need assets you supply (04: sound effects; 05: `diceWhite.png`, a 3x2 sheet of
64x64 faces) — see each demo's folder/comments. Demos 12 and 13 ship their GLSL shaders in their
own `assets/shaders/` folders (compiled at runtime). The rest run with no assets.

## Building

From the engine root, configure with examples enabled:

```sh
cmake -B build -DLUMINOVEAU_BUILD_EXAMPLES=ON
cmake --build build
```

Each demo builds to its own folder under `build/examples/<name>/`, with its assets copied
next to the executable.

## Adding a demo

1. Create `examples/NN_name/main.cpp` implementing the four callbacks (copy `01_bouncing_ball`).
2. Add `add_lumi_example(NN_name)` to `examples/CMakeLists.txt`.
3. Put any assets in `examples/NN_name/assets/` — they are copied beside the binary automatically.
