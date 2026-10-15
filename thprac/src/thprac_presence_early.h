#pragma once

#include "thprac_discord.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace DiscordPresence {
// Captures run on the game thread. The callback also permits deterministic
// address-map fixtures without attaching to or modifying a game process.
struct EarlyMemory {
    using Read = bool (*)(void*, uintptr_t, void*, size_t);
    Read read;
    void* context;
    EarlyMemory();
    EarlyMemory(Read reader, void* state) : read(reader), context(state) {}

    template <class T> T Get(uintptr_t address) const
    {
        T result {};
        if (!read(context, address, &result, sizeof(result)))
            throw std::runtime_error("Unreadable game state");
        return result;
    }
    int32_t Int(uintptr_t address, int32_t minimum = 0, int32_t maximum = INT32_MAX) const;
    uint8_t Flag(uintptr_t address, uint8_t maximum = 1) const;
    uintptr_t Pointer(uintptr_t address) const;
    double Float(uintptr_t address, double minimum, double maximum, bool whole = false) const;
};

Snapshot Capture06(bool practice = false, const EarlyMemory& memory = {});
Snapshot Capture07(const EarlyMemory& memory = {});
Snapshot Capture08(const EarlyMemory& memory = {});
Snapshot Capture09(const EarlyMemory& memory = {});
Snapshot Capture10(const EarlyMemory& memory = {});
Snapshot Capture11(const EarlyMemory& memory = {});
Snapshot Capture12(const EarlyMemory& memory = {});
Snapshot Capture128(const EarlyMemory& memory = {});
}
