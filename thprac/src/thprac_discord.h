#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Presence formatting and IPC behavior adapted from TouhouStatus (MIT).
// Copyright (c) 2026 moyu1254. See docs/touhoustatus-license.txt.
namespace DiscordPresence {

inline constexpr const char* DefaultApplicationId = "1558405174777876580";

enum class Phase { Unknown, Loading, Title, Menu, Playing, Paused, GameOver, Clear, Ending, Results };

// Captures own their strings. Only the game/frame thread may read game memory or
// thprac's locale tables; the Discord worker receives this value after formatting.
struct Snapshot {
    Phase phase = Phase::Unknown;
    std::string title, mode, difficulty, stage, segment, character, equipment;
    std::string spell, extra, opponent, lifePieces, bombPieces, elapsedLabel;
    std::optional<int64_t> score, graze, stageGraze, pointItems, stagePointItems, opponentScore;
    std::optional<double> lives, bombs, power, health, pointItemValue;
    std::optional<double> opponentLives, opponentBombs, opponentHealth, elapsedSeconds;
};

struct Activity {
    std::string name, details, state, significantKey;
    std::optional<int64_t> startUnixSeconds;
    bool operator==(const Activity&) const = default;
};

using Capture = Snapshot (*)();
// Register during game initialization, before the first frame.
void Initialize(const char* gameId, Capture capture) noexcept;
// Call from the frame thread. Sampling is capped at 10 Hz and pipe IO is off-thread.
void Tick(bool enabled, const char* applicationId = DefaultApplicationId) noexcept;
// RequestStop is safe during process detach: it never waits or runs pipe IO.
void RequestStop() noexcept;
// Call only outside DllMain/loader lock. The DLL is pinned while its worker exists.
void Shutdown() noexcept;

bool IsValidApplicationId(std::string_view applicationId) noexcept;
std::optional<Activity> Format(std::string_view gameId, const Snapshot& snapshot, int64_t nowUnixSeconds = 0);

#ifdef THPRAC_DISCORD_TESTS
namespace Testing {
// Must be set before the first Tick. Tests never connect to a Discord pipe.
void SetPipeName(const wchar_t* name);
}
#endif
}
