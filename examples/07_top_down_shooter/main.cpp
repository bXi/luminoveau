// Example 07 — Twin-Stick Shooter
// ---------------------------------------------------------------------------
// Shows the unified InputDevice interface: the game asks every device for a left-stick and a
// right-stick vector and doesn't care whether it's a keyboard or a gamepad.
//   * gamepad: LEFT stick moves, RIGHT stick aims and fires (true twin-stick)
//   * keyboard: WASD moves (left stick), arrow keys aim + fire (right stick); mouse also aims,
//     left mouse button / L-Shift fires (the SHOOT button)
//   * many bullets/enemies to exercise the sprite batcher, plus circle collisions + culling
//
// InputDevice::getLeftStick()/getRightStick() unify analog sticks and key/D-pad directions, so
// one code path drives both control schemes.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

int   width       = 960;
int   height      = 720;
float moveSpeed   = 260.0f;
float bulletSpeed = 620.0f;
float fireGap     = 0.09f;
float enemySpeed  = 90.0f;
float spawnGap    = 0.6f;

// A moving circle — used for both bullets and enemies.
struct Ent { vf2d pos, vel; float radius; };

vf2d player = {width * 0.5f, height * 0.5f};
std::vector<Ent> bullets;
std::vector<Ent> enemies;
float fireTimer  = 0.0f;
float spawnTimer = 0.0f;
int   score = 0;

float Rand(float lo, float hi) { return lo + (hi - lo) * (std::rand() / (float)RAND_MAX); }
bool  Off(vf2d p) { return p.x < -40 || p.x > width + 40 || p.y < -40 || p.y > height + 40; }

void SpawnEnemy() {
    // Pick a random edge and head roughly toward the player.
    vf2d p = (std::rand() % 2) ? vf2d{Rand(0, width), (std::rand() % 2) ? -30.0f : height + 30.0f}
                               : vf2d{(std::rand() % 2) ? -30.0f : width + 30.0f, Rand(0, height)};
    enemies.push_back({p, (player - p).norm() * enemySpeed, 16.0f});
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Top-Down Shooter", width, height, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({14, 16, 22, 255});
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = (float)Window::GetFrameTime();

    // All input flows through the unified InputDevice interface. Every device (keyboard/mouse and
    // each gamepad) answers getLeftStick() / getRightStick(), so the same code drives a keyboard
    // (WASD move, arrows aim/fire) and a gamepad (twin sticks) without special-casing either.
    vf2d move   = {0, 0};   // summed left sticks -> movement
    vf2d aimDir = {0, 0};   // first non-neutral right stick -> aim
    bool firing = false;

    for (InputDevice* dev : Input::GetAllInputs()) {
        if (!dev) continue;
        move += dev->getLeftStick();

        vf2d aim = dev->getRightStick();
        if (aim.mag2() > 0.25f && !firing) {   // past deadzone -> this device is aiming + firing
            aimDir = aim.norm();
            firing = true;
        }
        // Also let a dedicated SHOOT button fire in the current aim direction.
        if (dev->is(Buttons::SHOOT, Action::HELD)) firing = true;
    }

    // Movement: clamp to unit length so diagonals / combined devices aren't faster.
    if (move.mag2() > 1.0f) move = move.norm();
    player += move * (moveSpeed * dt);
    player.x = std::clamp(player.x, 0.0f, (float)width);
    player.y = std::clamp(player.y, 0.0f, (float)height);

    // Mouse aim: if no stick is aiming, aim toward the cursor.
    if (!firing || (aimDir.x == 0.0f && aimDir.y == 0.0f)) {
        vf2d toMouse = Input::GetMousePosition() - player;
        if (toMouse.mag2() > 1.0f) aimDir = toMouse.norm();
    }

    fireTimer -= dt;
    if (firing && fireTimer <= 0.0f && (aimDir.x != 0.0f || aimDir.y != 0.0f)) {
        bullets.push_back({player + aimDir * 18.0f, aimDir * bulletSpeed, 4.0f});
        fireTimer = fireGap;
    }

    // Spawn enemies over time.
    spawnTimer -= dt;
    if (spawnTimer <= 0.0f) { SpawnEnemy(); spawnTimer = spawnGap; }

    // Move + remove off-screen bullets.
    for (Ent& b : bullets) b.pos += b.vel * dt;
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
                                 [](Ent& b) { return Off(b.pos); }), bullets.end());

    // Enemies steer toward the player.
    for (Ent& e : enemies) { e.vel = (player - e.pos).norm() * enemySpeed; e.pos += e.vel * dt; }

    // Bullet/enemy collisions.
    for (int ei = 0; ei < (int)enemies.size();) {
        bool dead = false;
        for (int bi = 0; bi < (int)bullets.size(); bi++) {
            vf2d d = enemies[ei].pos - bullets[bi].pos;
            float rr = enemies[ei].radius + bullets[bi].radius;
            if (d.x * d.x + d.y * d.y <= rr * rr) {
                bullets.erase(bullets.begin() + bi);
                dead = true; score += 10; break;
            }
        }
        if (dead) enemies.erase(enemies.begin() + ei);
        else ei++;
    }

    // An enemy reaching the player resets the run.
    for (Ent& e : enemies) {
        vf2d d = e.pos - player;
        if (d.x * d.x + d.y * d.y <= (e.radius + 12.0f) * (e.radius + 12.0f)) {
            enemies.clear(); bullets.clear(); score = 0; player = {width * 0.5f, height * 0.5f};
            break;
        }
    }

    // ── Draw ─────────────────────────────────────────────────────────────────────
    Window::StartFrame();

    for (Ent& e : enemies) Draw::CircleFilled(e.pos, e.radius, {220, 90, 90, 255});
    for (Ent& b : bullets) Draw::CircleFilled(b.pos, b.radius, {255, 240, 150, 255});

    Draw::CircleFilled(player, 12.0f, {120, 220, 255, 255});
    Draw::Line(player, player + aimDir * 26.0f, WHITE);  // aim reticle (mouse or right stick)

    FontAsset& font = AssetHandler::GetDefaultFont();
    Text::DrawText(font, {10.0f, 6.0f}, "Score: " + std::to_string(score), WHITE, 26.0f);
    Text::DrawText(font, {10.0f, 34.0f},
                   "Bullets: " + std::to_string(bullets.size()) + "  Enemies: " + std::to_string(enemies.size()),
                   {160, 160, 180, 255}, 22.0f);

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
