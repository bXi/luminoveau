#pragma once

// The messages exchanged with a signalling service, shared by the engine's broker and by
// the service itself (tools/signalserver) so the protocol is written down once.
//
// WebRTC peers cannot find each other unaided: somebody has to carry the offer, the answer
// and the ICE candidates until the direct path is up. That is all this is for. It never
// sees game traffic — once the data channels open, the service is out of the loop.
//
// JSON over a WebSocket, one object per message, `op` selecting the shape. Unknown fields
// and unknown ops are ignored rather than refused, so an old client and a new service stay
// on speaking terms.
//
//   client -> service            service -> client
//   ------------------           ------------------
//   hello   id, build            welcome  ice [ … ]
//   host    name, slots, blob    hosting  room
//   list                         sessions [ … ]
//   join    room                 joined   host
//   signal  to, data             signal   from, data
//   leave                        peer     id, state
//                                error    reason

#include <cstdint>
#include <string>
#include <vector>

/// @cond INTERNAL
namespace SignalProtocol {

// Bumped when a change would confuse an older peer. The service refuses anything else.
constexpr int VERSION = 1;

namespace Op {
    constexpr const char *Hello    = "hello";
    constexpr const char *Welcome  = "welcome";
    constexpr const char *Host     = "host";
    constexpr const char *Hosting  = "hosting";
    constexpr const char *List     = "list";
    constexpr const char *Sessions = "sessions";
    constexpr const char *Join     = "join";
    constexpr const char *Joined   = "joined";
    constexpr const char *Leave    = "leave";
    constexpr const char *Signal   = "signal";
    constexpr const char *Peer     = "peer";
    constexpr const char *Error    = "error";
} // namespace Op

// `welcome` may carry `ice`: the STUN and TURN servers this client should use, as either plain
// URL strings or the browser's own `{ "urls", "username", "credential" }` objects. It is optional
// and a service that omits it changes nothing — the client keeps its built-in default.
//
// **Relay credentials belong here rather than in the client.** A TURN server needs a username and
// password, and anything compiled into a game is public the moment it ships; a web build hands
// them over in the network tab. Issuing short-lived ones per connection (coturn's REST scheme:
// username is `<expiry>:<name>`, password is base64(HMAC-SHA1(secret, username))) keeps the secret
// on the service and lets it rotate without a client release.
//
// Signal payloads are opaque bytes as far as this layer is concerned — the service must not
// need to understand SDP — so they travel base64 encoded rather than as JSON structure.
std::string          base64Encode(const uint8_t *data, size_t size);
std::vector<uint8_t> base64Decode(const std::string &text);

// Room codes are short enough to read down a phone line: no vowels, so no code spells
// anything, and no 0/O or 1/I to mistype.
constexpr const char *ROOM_ALPHABET = "BCDFGHJKLMNPQRSTVWXYZ23456789";
constexpr int         ROOM_LENGTH   = 4;

} // namespace SignalProtocol
/// @endcond
