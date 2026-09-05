# PatchDataChannelWasm.cmake
# Run as a script (-P) from CPM's PATCH_COMMAND. Input: SRC — the fetched source directory.
#
# ── Why this patch exists ────────────────────────────────────────────────────────────────────
#
# `RTCDataChannel.send()` refuses a view onto a **resizable** ArrayBuffer, and a browser now hands
# one out for WebAssembly memory. Sending anything therefore throws:
#
#     Failed to execute 'send' on 'RTCDataChannel':
#     The provided ArrayBufferView value must not be resizable.
#
# datachannel-wasm already has the copy this needs — it just does not reach it. Its guard reads:
#
#     if (heapBytes.buffer instanceof ArrayBuffer) { send(heapBytes); } else { copy and send; }
#
# which was written to catch **SharedArrayBuffer**, where `instanceof ArrayBuffer` is false. A
# resizable ArrayBuffer is an ordinary ArrayBuffer with `resizable === true`, so the test passes
# and the forbidden view goes out. Adding the resizable case sends every such buffer down the copy
# path the library already provides.
#
# ── Why patch rather than re-pin ─────────────────────────────────────────────────────────────
#
# datachannel-wasm is **MIT**, so modifying it carries only the notice requirement — unlike
# libdatachannel and libjuice, which are MPL-2.0 and must never be patched (see the licensing note
# in lumistunts' `design_docs/Web Multiplayer.md`). This should still go upstream; it is a bug in
# any Emscripten application that sends from the heap, not something specific to this engine.
#
# Idempotent: applying it twice is a no-op, which matters because CPM re-runs PATCH_COMMAND
# whenever it re-populates.

set(_js "${SRC}/wasm/js/webrtc.js")

if(NOT EXISTS "${_js}")
    message(WARNING "[Lumi] datachannel-wasm: ${_js} not found; resizable-buffer patch skipped")
    return()
endif()

file(READ "${_js}" _contents)

if(_contents MATCHES "!heapBytes\\.buffer\\.resizable")
    return() # already patched
endif()

string(REPLACE
    "if(heapBytes.buffer instanceof ArrayBuffer) {"
    "if(heapBytes.buffer instanceof ArrayBuffer && !heapBytes.buffer.resizable) {"
    _patched "${_contents}")

if(_patched STREQUAL _contents)
    message(WARNING
        "[Lumi] datachannel-wasm: could not apply the resizable-buffer patch — the guard in "
        "wasm/js/webrtc.js has changed. Check whether upstream has fixed it and re-pin.")
    return()
endif()

file(WRITE "${_js}" "${_patched}")
message(STATUS "[Lumi] datachannel-wasm: patched rtcSendMessage for resizable ArrayBuffers")
