#pragma once

namespace Lumi {
    /// @brief Result returned from app lifecycle callbacks to drive the main loop.
    enum class Result {
        Continue = 0,  ///< Keep running; proceed to the next frame.
        Success  = 1,  ///< Exit the app reporting success.
        Failure  = 2   ///< Exit the app reporting failure.
    };
}
