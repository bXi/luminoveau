# Empty Project

A blank Luminoveau project to copy when starting something new. It opens a window, clears it
every frame, and leaves `TODO` markers in `main.cpp` where your init/update/draw code goes.

## Use it as a starting point

1. Copy this whole directory somewhere outside the engine repo and rename it.
2. Get the engine: place a Luminoveau checkout in a `luminoveau/` subfolder next to `main.cpp`
   (clone it, add it as a git submodule, or symlink it), or pass the path at configure time:
   ```sh
   cmake -B build -DLUMINOVEAU_DIR=/path/to/luminoveau
   cmake --build build
   ```
3. Run the executable from `build/`.

## Files

- `main.cpp` — your program (the four app callbacks).
- `CMakeLists.txt` — standalone build file (used only when copied out; the in-repo examples
  build ignores it).

The four callbacks: `AppInit` (startup), `AppIterate` (every frame), `AppEvent` (per OS/input
event), `AppQuit` (shutdown). See `../01_bouncing_ball` and `../02_pong` for worked examples.
