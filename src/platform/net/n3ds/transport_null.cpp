// 3DS-backend null transport. Networking is out of scope for the 3DS port (M1);
// net.cpp treats a null transport as "no backend available" and every Net:: call
// no-ops gracefully.

#include "platform/net/itransport.h"

ITransport *createTransport() { return nullptr; }
