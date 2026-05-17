#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// IBackendAccess — internal interface for accessing raw backend handles.
//
// Only systems that must talk directly to the backend (render pass
// implementations, the asset loader, etc.) should use this.  Do NOT expose
// it in any public-facing API.
//
// Each backend provides a concrete implementation alongside its IGpu impl.
// ─────────────────────────────────────────────────────────────────────────────

class IBackendAccess {
public:
    virtual ~IBackendAccess() = default;

    // Returns the raw native device handle.
    // Cast to the appropriate type for the active backend:
    //   SDL backend  →  reinterpret_cast<SDL_GPUDevice*>(getRawDevice())
    //   GL  backend  →  not meaningful; returns nullptr
    //   SW  backend  →  not meaningful; returns nullptr
    virtual void* getRawDevice() const = 0;

    // Returns the native sampler for the given scale mode.
    // SDL  →  reinterpret_cast<SDL_GPUSampler*>(getRawSampler(mode))
    virtual void* getRawSampler(int scaleModeInt) const = 0;
};
