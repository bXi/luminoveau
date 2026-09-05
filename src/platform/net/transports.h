#pragma once

// Internal registry of the transports compiled into this build. createTransport
// (itransport.h) picks one; nothing else should construct a transport directly.

#include "platform/net/itransport.h"

/// @cond INTERNAL
ITransport *createSdlNetTransport();
ITransport *createWebTransport();
ITransport *createGnsTransport();
ITransport *createWebRtcTransport();
/// @endcond
