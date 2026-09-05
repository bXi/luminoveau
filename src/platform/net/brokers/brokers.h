#pragma once

// Internal registry of the brokers compiled into this build. createBroker (ibroker.h) picks
// one of these; nothing else should construct a broker directly.

#include <string>

#include "platform/net/ibroker.h"
#include "platform/net/playerid.h"

/// @cond INTERNAL

// A brokerage that knows nobody still has to answer LocalPlayer(). This mints an identity
// that lasts as long as the process, which is as much durability as an address broker has.
PlayerId mintProcessIdentity();

IBroker *createLocalBroker();
IBroker *createLanBroker();
// Defined in integrations/steam/steambroker.cpp, which compiles to a null factory when the
// Steamworks SDK is not present.
IBroker *createSteamBroker();
// Defined in brokers/signalbroker.cpp, compiled only with the WebRTC transport.
IBroker *createSignalBroker(const std::string &url);

// Hands control back to the event loop while a broker waits. On the web nothing else runs
// otherwise, so a plain sleep would deadlock the very socket being waited on.
void brokerYield();
/// @endcond
