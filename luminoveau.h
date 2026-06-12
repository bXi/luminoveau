#pragma once

// ── Global config ────────────────────────────────────────────────────────────
#include <config.h>

// ── Math & types ────────────────────────────────────────────────────────────
#include <math/angles.h>
#include <types/color.h>
#include <math/constants.h>
#include <util/helpers.h>
#include <util/lerp.h>
#include <math/rectangles.h>
#include <math/vectors.h>

// ── Asset types ──────────────────────────────────────────────────────────────
#include <assets/texture/texture.h>
#include <assets/shader/shader.h>
#include <assets/font/font.h>
#include <assets/audio/sound.h>
#include <assets/audio/music.h>
#include <assets/audio/pcmsound.h>
#include <assets/model/model.h>
#include <assets/effect/effect.h>
#include <assets/effect/effects.h>
#include <assets/compute/computepipeline.h>

// ── GPU ──────────────────────────────────────────────────────────────────────
#include <gpu/types.h>
#include <gpu/IGpu.h>
#include <gpu/presets.h>
#include <gpu/buffer/buffer.h>
#include <gpu/buffer/buffermanager.h>

// ── Engine systems ───────────────────────────────────────────────────────────
#include <assets/assethandler.h>
#include <platform/audio/audio.h>
#include <core/eventbus/eventbus.h>
#include <platform/input/input.h>
#include <platform/net/net.h>
#include <renderer/renderer.h>
#include <platform/window/window.h>

#include <draw/draw.h>
#include <renderer/shaders.h>
#include <renderer/compute.h>
#include <draw/particles.h>
#include <draw/text.h>
#include <profiler/perf.h>

// ── Scene ────────────────────────────────────────────────────────────────────
#include <scene/camera.h>
#include <scene/camera3d.h>
#include <scene/scene3d.h>

// ── Steam (optional) ─────────────────────────────────────────────────────────
#include <integrations/steam/steam.h>

// ── RmlUI (optional) ─────────────────────────────────────────────────────────
#ifdef LUMINOVEAU_WITH_RMLUI
#include <integrations/rmlui/rmlui.h>
#endif

// ── Callback-based main loop (optional) ──────────────────────────────────────
#ifdef LUMINOVEAU_USE_CALLBACKS
#include <app/app.h>
#endif
