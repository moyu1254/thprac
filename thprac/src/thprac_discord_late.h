#pragma once
#include <cstddef>
#include "thprac_discord.h"

namespace DiscordPresence {
    // Called only from the game's main-thread GUI hook, after thprac has
    // identified and initialized the supported executable version.
    Snapshot CaptureLate(int game, bool th19v110c = false, int practiceMode = 0);
#ifdef THPRAC_PRESENCE_TESTS
    using LateMemoryReader = bool (*)(uintptr_t address, void* output, size_t size);
    void SetLateMemoryReader(LateMemoryReader reader);
#endif
}
