// Example 06 — Asteroids
// ---------------------------------------------------------------------------
// A vector-style shooter that shows:
//   * device-agnostic input via the Buttons abstraction — the same code reads keyboard
//     AND gamepad (Input::GetAllInputs + InputDevice::is)
//   * rotation, thrust and momentum with simple physics
//   * screen wrapping and circle/point collision
//
// Turn: Left/Right (or D-pad / A & D).  Thrust: Up (W).  Fire: Accept (Space / South button).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

int   width     = 900;
int   height    = 700;
float turnSpeed = 3.2f;     // radians/sec
float thrust    = 320.0f;   // accel px/sec^2
float drag      = 0.6f;     // velocity damping per second
float bulletSpeed = 520.0f;
float fireDelay = 0.18f;
float bulletLife = 1.4f;    // seconds before a bullet expires

// A moving thing with a position, velocity, radius, and (for bullets) remaining life.
struct Body { vf2d pos, vel; float radius; float life = 0.0f; };

Body ship = {{width * 0.5f, height * 0.5f}, {0, 0}, 14.0f};
float shipAngle = -3.14159f * 0.5f;  // pointing up
float fireTimer = 0.0f;
std::vector<Body> bullets;
std::vector<Body> rocks;

float Rand(float lo, float hi) { return lo + (hi - lo) * (std::rand() / (float)RAND_MAX); }

void Wrap(vf2d& p) {
    if (p.x < 0) p.x += width;  else if (p.x >= width)  p.x -= width;
    if (p.y < 0) p.y += height; else if (p.y >= height) p.y -= height;
}

// True if ANY connected device (keyboard or gamepad) reports this button.
bool Btn(Buttons b, Action action) {
    for (InputDevice* d : Input::GetAllInputs())
        if (d && d->is(b, action)) return true;
    return false;
}

void SpawnRocks(int n) {
    rocks.clear();
    for (int i = 0; i < n; i++) {
        vf2d p = {Rand(0, width), Rand(0, height)};
        // Keep new rocks away from the ship's start.
        if (std::abs(p.x - width * 0.5f) < 120 && std::abs(p.y - height * 0.5f) < 120) p.x += 240;
        rocks.push_back({p, {Rand(-60, 60), Rand(-60, 60)}, 46.0f});
    }
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Asteroids", width, height, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({6, 6, 12, 255});
    SpawnRocks(5);
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = (float)Window::GetFrameTime();

    // ── Ship control ───────────────────────────────────────────────────────────
    if (Btn(Buttons::LEFT,  Action::HELD)) shipAngle -= turnSpeed * dt;
    if (Btn(Buttons::RIGHT, Action::HELD)) shipAngle += turnSpeed * dt;

    vf2d heading = {std::cos(shipAngle), std::sin(shipAngle)};
    if (Btn(Buttons::UP, Action::HELD)) ship.vel += heading * (thrust * dt);

    ship.vel = ship.vel * (1.0f - drag * dt);   // momentum with a little drag
    ship.pos += ship.vel * dt;
    Wrap(ship.pos);

    // ── Firing ─────────────────────────────────────────────────────────────────
    fireTimer -= dt;
    if (Btn(Buttons::ACCEPT, Action::HELD) && fireTimer <= 0.0f) {
        bullets.push_back({ship.pos + heading * ship.radius, ship.vel + heading * bulletSpeed,
                           3.0f, bulletLife});
        fireTimer = fireDelay;
    }

    // ── Move bullets, removing them once their life runs out ─────────────────────
    for (Body& b : bullets) { b.pos += b.vel * dt; Wrap(b.pos); b.life -= dt; }
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
                                 [](Body& b) { return b.life <= 0.0f; }),
                  bullets.end());

    // ── Move rocks ───────────────────────────────────────────────────────────────
    for (Body& r : rocks) { r.pos += r.vel * dt; Wrap(r.pos); }

    // ── Bullet / rock collisions ─────────────────────────────────────────────────
    for (int bi = 0; bi < (int)bullets.size();) {
        bool hit = false;
        for (int ri = 0; ri < (int)rocks.size(); ri++) {
            vf2d d = bullets[bi].pos - rocks[ri].pos;
            if (d.x * d.x + d.y * d.y <= rocks[ri].radius * rocks[ri].radius) {
                // Split big rocks into two smaller ones; small rocks just die.
                Body parent = rocks[ri];
                rocks.erase(rocks.begin() + ri);
                if (parent.radius > 20.0f) {
                    for (int k = 0; k < 2; k++)
                        rocks.push_back({parent.pos, {Rand(-90, 90), Rand(-90, 90)}, parent.radius * 0.55f});
                }
                hit = true;
                break;
            }
        }
        if (hit) bullets.erase(bullets.begin() + bi);
        else     bi++;
    }

    if (rocks.empty()) SpawnRocks(6);

    // ── Draw ─────────────────────────────────────────────────────────────────────
    Window::StartFrame();

    for (Body& r : rocks)   Draw::Circle(r.pos, r.radius, {170, 170, 190, 255});
    for (Body& b : bullets) Draw::CircleFilled(b.pos, 3.0f, {255, 240, 160, 255});

    // Ship as a triangle pointing along shipAngle. rot() rotates a local point around the ship.
    auto rot = [&](float ox, float oy) {
        return vf2d{ship.pos.x + ox * std::cos(shipAngle) - oy * std::sin(shipAngle),
                    ship.pos.y + ox * std::sin(shipAngle) + oy * std::cos(shipAngle)};
    };
    Draw::TriangleFilled(rot(18, 0), rot(-12, -11), rot(-12, 11), {120, 220, 255, 255});

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
