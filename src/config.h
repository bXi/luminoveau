#pragma once

#include "types/color.h"

// ─────────────────────────────────────────────────────────────────────────────
// Global engine configuration.
// Tune these values to match your project's requirements.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef MAX_SPRITES
#  ifdef LUMINOVEAU_WEBGPU_BACKEND
#    define MAX_SPRITES 100'000   // Browser VRAM/heap budget: ~3 MB GPU + ~12 MB CPU per pass
#  else
#    define MAX_SPRITES 4'000'000
#  endif
#endif

// Particle slot cap. Browser WebGPU implementations vary widely in storage-buffer caps;
// Firefox's heap is much tighter than Chrome's. 1.5M (≈96 MB) fits both browsers.
#ifndef LUMINOVEAU_MAX_PARTICLES
#  ifdef LUMINOVEAU_WEBGPU_BACKEND
#    define LUMINOVEAU_MAX_PARTICLES 1'500'000u
#  else
#    define LUMINOVEAU_MAX_PARTICLES 50'000'000u
#  endif
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Named colors
// ─────────────────────────────────────────────────────────────────────────────

static inline Color RED       = {255,   0,   0, 255};
static inline Color GREEN     = {  0, 255,   0, 255};
static inline Color BLUE      = {  0,   0, 255, 255};
static inline Color BLACK     = {  0,   0,   0, 255};
static inline Color WHITE     = {255, 255, 255, 255};

static inline Color YELLOW    = {255, 255,   0, 255};
static inline Color CYAN      = {  0, 255, 255, 255};
static inline Color MAGENTA   = {255,   0, 255, 255};
static inline Color PURPLE    = {255,   0, 255, 255};
static inline Color ORANGE    = {255, 165,   0, 255};
static inline Color PINK      = {255, 192, 203, 255};
static inline Color LIME      = {  0, 255,   0, 255};

static inline Color LIGHTGRAY = {200, 200, 200, 255};
static inline Color GRAY      = {128, 128, 128, 255};
static inline Color DARKGRAY  = { 80,  80,  80, 255};

static inline Color DARKRED   = {139,   0,   0, 255};
static inline Color DARKGREEN = {  0, 100,   0, 255};
static inline Color DARKBLUE  = {  0,   0, 139, 255};
static inline Color DARKYELLOW= {204, 204,   0, 255};

static inline Color SKYBLUE   = {135, 206, 235, 255};
static inline Color NAVY      = {  0,   0, 128, 255};
static inline Color VIOLET    = {148,   0, 211, 255};
static inline Color BROWN     = {139,  69,  19, 255};
static inline Color MAROON    = {128,   0,   0, 255};
static inline Color BEIGE     = {245, 245, 220, 255};
static inline Color GOLD      = {255, 215,   0, 255};
static inline Color SILVER    = {192, 192, 192, 255};
