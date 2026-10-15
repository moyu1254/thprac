#include "thprac_presence_early.h"
// EarlyGames.cs port: Copyright (c) 2026 moyu1254, MIT.
// See docs/touhoustatus-license.txt for the complete notice.
#include "thprac_gui_locale.h"
#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace DiscordPresence {
namespace {
bool ReadCurrentProcess(void*, uintptr_t address, void* destination, size_t size)
{
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination, size, &copied) && copied == size;
}

template <class T> T Range(T value, T minimum, T maximum)
{
    if (value < minimum || value > maximum) throw std::runtime_error("Invalid game state");
    return value;
}

Snapshot Empty(th_glossary_t title, Phase phase)
{
    Snapshot result;
    result.title = S(title);
    result.phase = phase;
    return result;
}

const char* Difficulty(int value, bool phantasm = false)
{
    static const char* names[] = { "Easy", "Normal", "Hard", "Lunatic", "Extra", "Phantasm" };
    return names[Range(value, 0, phantasm ? 5 : 4)];
}

std::string Stage(int value, bool phantasm = false)
{
    Range(value, 1, phantasm ? 8 : 7);
    return value == 7 ? "Extra" : value == 8 ? "Phantasm" : "Stage " + std::to_string(value);
}

template <size_t N> const char* Label(const th_glossary_t (&labels)[N], int index)
{
    return S(labels[Range(index, 0, static_cast<int>(N) - 1)]);
}

template <size_t N> const char* Choice(const char* const (&labels)[N], int index)
{
    return labels[Range(index, 0, static_cast<int>(N) - 1)];
}

constexpr th_glossary_t Characters[] = { TH_TRACKER_REIMU, TH_TRACKER_MARISA, TH_TRACKER_SAKUYA };
constexpr const char* Shots[] = { "A", "B", "C" };

const char* Character09(int character)
{
    // The game-roll list groups the Prismriver sisters together; live player
    // IDs put Merlin and Lunasa after the other fourteen characters.
    constexpr int listIndices[] = {0, 1, 2, 3, 4, 5, 6, 9, 10, 13, 11, 12, 14, 15, 7, 8};
    const char* name = S(THPRAC_GAMEROLL_TH09_SHOTTYPES);
    for (int i = listIndices[Range(character, 0, 15)]; i > 0; --i) {
        if (!*name) throw std::runtime_error("Missing TH09 character label");
        name += std::strlen(name) + 1;
    }
    if (!*name) throw std::runtime_error("Missing TH09 character label");
    return name;
}

std::string Number(double value)
{
    char text[48];
    std::snprintf(text, sizeof(text), "%.2f", value);
    std::string result(text);
    while (!result.empty() && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result;
}

bool HasBoss(const EarlyMemory& m, uintptr_t manager, size_t offset)
{
    const auto boss = m.Get<uint32_t>(m.Pointer(manager) + offset);
    if (boss != 0) Range<uint32_t>(boss, 0x10000, 0x7fffffff);
    return boss != 0;
}
}

EarlyMemory::EarlyMemory() : read(ReadCurrentProcess), context(nullptr) {}
int32_t EarlyMemory::Int(uintptr_t address, int32_t minimum, int32_t maximum) const
{
    return Range(Get<int32_t>(address), minimum, maximum);
}
uint8_t EarlyMemory::Flag(uintptr_t address, uint8_t maximum) const
{
    return Range<uint8_t>(Get<uint8_t>(address), 0, maximum);
}
uintptr_t EarlyMemory::Pointer(uintptr_t address) const
{
    return Range<uint32_t>(Get<uint32_t>(address), 0x10000, 0x7fffffff);
}
double EarlyMemory::Float(uintptr_t address, double minimum, double maximum, bool whole) const
{
    const double value = Get<float>(address);
    if (!std::isfinite(value) || (whole && std::floor(value) != value)) throw std::runtime_error("Invalid game resource");
    return Range(value, minimum, maximum);
}

// Lifecycle and resource layouts are ported from TouhouStatus EarlyGames.cs,
// corroborated by the existing thprac game structures and resource setters.
// Each invocation creates a fresh snapshot: menus/results never retain a run.
Snapshot Capture06(bool practice, const EarlyMemory& m)
{
    enum addrs : uintptr_t {
        GAME_MANAGER = 0x69bca0,
        CURRENT_STATE = 0x6c6ea4,
        WANTED_STATE = 0x6c6ea0,
        SPELL_ACTIVE = 0x5a5f90,
        BOSS_ACTIVE = 0x69bc50,
        SPELL_ID = 0x5a5f98,
    };
    constexpr uintptr_t g = GAME_MANAGER;
    auto s = Empty(TH06_TITLE, Phase::Unknown);
    const int screen = m.Int(CURRENT_STATE);
    if (screen != m.Int(WANTED_STATE)) { s.phase = Phase::Loading; return s; }
    if (screen != 2) {
        switch (screen) {
        case 0: case 3: s.phase = Phase::Loading; break;
        case 1: case 8: s.phase = Phase::Title; break;
        case 6: s.phase = Phase::Results; break;
        case 7: s.phase = m.Flag(g + 0x1822) ? Phase::Clear : Phase::Results; break;
        case 9: s.phase = Phase::Menu; break;
        case 10: s.phase = Phase::Ending; break;
        }
        return s;
    }
    if (m.Flag(g + 0x1824)) { s.phase = Phase::Title; s.mode = "デモ"; return s; }
    if (m.Flag(g + 0x1820)) { s.phase = Phase::GameOver; return s; }
    if (m.Flag(g + 0x1822)) { s.phase = Phase::Clear; return s; }
    s.phase = m.Flag(g + 0x181f, 2) ? Phase::Paused : Phase::Playing;
    s.mode = m.Int(g + 0x1c, 0, 1) ? "リプレイ鑑賞" : practice || m.Flag(g + 0x1823) ? "プラクティス" : "通常プレイ";
    s.difficulty = Difficulty(m.Int(g + 0x10));
    s.character = Label(Characters, m.Flag(g + 0x181d));
    s.equipment = Choice(Shots, m.Flag(g + 0x181e));
    s.stage = Stage(m.Int(g + 0x1a34));
    const bool spell = m.Int(SPELL_ACTIVE, 0, 1) != 0;
    s.segment = spell || m.Flag(BOSS_ACTIVE) ? "ボス戦" : "道中";
    if (spell) s.spell = "スペル #" + std::to_string(m.Int(SPELL_ID, 0, 63) + 1);
    s.lives = Range<int>(m.Get<int8_t>(g + 0x181a), 0, 8);
    s.bombs = Range<int>(m.Get<int8_t>(g + 0x181b), 0, 8);
    s.power = Range<int>(m.Get<uint16_t>(g + 0x1810), 0, 128);
    s.score = Range<uint32_t>(m.Get<uint32_t>(g), 0, 999999999);
    s.stageGraze = m.Int(g + 0x14); s.graze = m.Int(g + 0x18);
    s.stagePointItems = m.Get<uint16_t>(g + 0x1814); s.pointItems = m.Get<uint16_t>(g + 0x1816);
    s.elapsedSeconds = m.Get<uint32_t>(g + 0x1a30) / 60.0; s.elapsedLabel = "面経過";
    return s;
}

Snapshot Capture07(const EarlyMemory& m)
{
    enum addrs : uintptr_t {
        CURRENT_STATE = 0x575aa4,
        WANTED_STATE = 0x575aa8,
        MODE_FLAGS = 0x62f648,
        GAME_ACTIVITY = 0x134d9cc,
        PAUSE_STATE = 0x62f64c,
        DIFFICULTY = 0x626280,
        CHARACTER = 0x62f645,
        SHOT_TYPE = 0x62f646,
        STAGE = 0x62f85c,
        GLOBALS_PTR = 0x626278,
        SPELL_ACTIVE = 0x12fe0c8,
        BOSS_ACTIVE = 0x49fc14,
        SPELL_ID = 0x12fe0d8,
        CHERRY = 0x62f88c,
        CHERRY_MAX = 0x62f888,
        CHERRY_PLUS = 0x62f890,
        BORDER_STATE = 0x4bfee5,
    };
    auto s = Empty(TH07_TITLE, Phase::Unknown);
    const int screen = m.Int(CURRENT_STATE);
    if (screen != m.Int(WANTED_STATE)) { s.phase = Phase::Loading; return s; }
    if (screen != 2) {
        switch (screen) {
        case 1: s.phase = Phase::Title; break;
        case 6: case 7: s.phase = Phase::Results; break;
        case 8: case 9: s.phase = Phase::Menu; break;
        case 0: case 3: case 10: case 11: case 12: s.phase = Phase::Loading; break;
        }
        return s;
    }
    const auto mode = m.Get<uint8_t>(MODE_FLAGS);
    if (mode & 2) { s.phase = Phase::Title; s.mode = "デモ"; return s; }
    const int active = m.Int(GAME_ACTIVITY);
    if (active != 30 && active != 60) return s;
    const int pause = m.Flag(PAUSE_STATE, 2);
    if (pause != 2 && !(mode & 4)) return s;
    s.phase = pause == 2 ? Phase::Paused : Phase::Playing;
    s.mode = mode & 8 ? "リプレイ鑑賞" : mode & 1 ? "プラクティス" : "通常プレイ";
    s.difficulty = Difficulty(m.Int(DIFFICULTY), true);
    s.character = Label(Characters, m.Flag(CHARACTER, 2)); s.equipment = Choice(Shots, m.Flag(SHOT_TYPE));
    s.stage = Stage(m.Int(STAGE), true);
    const uintptr_t g = m.Pointer(GLOBALS_PTR);
    s.lives = m.Float(g + 0x5c, 0, 8, true); s.bombs = m.Float(g + 0x68, 0, 8, true); s.power = m.Float(g + 0x7c, 0, 128, true);
    s.score = int64_t(m.Int(g, 0, 999999999)) * 10 + m.Flag(g + 0x20, 9);
    s.graze = m.Int(g + 0x18); s.stagePointItems = m.Int(g + 0x24); s.pointItems = m.Int(g + 0x28);
    const bool spell = m.Int(SPELL_ACTIVE) != 0;
    s.segment = spell || m.Int(BOSS_ACTIVE, 0, 1) ? "ボス戦" : "道中";
    if (spell) s.spell = "スペル #" + std::to_string(m.Int(SPELL_ID, 0, 140) + 1);
    const auto cherryBase = m.Get<int32_t>(g + 0x88);
    const auto cherry = Range<int64_t>(int64_t(m.Get<int32_t>(CHERRY)) - cherryBase, 0, 9999990);
    const auto cherryMax = Range<int64_t>(int64_t(m.Get<int32_t>(CHERRY_MAX)) - cherryBase, 0, 9999990);
    const auto cherryPlus = Range<int64_t>(int64_t(m.Get<int32_t>(CHERRY_PLUS)) - cherryBase, 0, 50000);
    const char* borders[] = { "結界なし", "森羅結界", "結界発動待ち" };
    s.extra = "桜点 " + std::to_string(cherry) + "/" + std::to_string(cherryMax) + " | 桜+ " + std::to_string(cherryPlus) + " | " + Choice(borders, m.Flag(BORDER_STATE, 2));
    return s;
}

Snapshot Capture08(const EarlyMemory& m)
{
    enum addrs : uintptr_t {
        CURRENT_STATE = 0x17ce8b4,
        MODE_FLAGS = 0x164d0b4,
        WANTED_STATE = 0x17ce8b0,
        GAME_ACTIVITY = 0x160f540,
        RETRY_STATE = 0x164d0bb,
        PAUSE_STATE = 0x164d0ba,
        DIFFICULTY = 0x160f538,
        CHARACTER = 0x164d0b1,
        STAGE = 0x4e4850,
        GLOBALS_PTR = 0x160f510,
        SPELL_FLAGS = 0x4ea670,
        BOSS_ACTIVE = 0x18b89b8,
        SPELL_ID = 0x4ea678,
    };
    auto s = Empty(TH08_TITLE, Phase::Unknown);
    const int screen = m.Int(CURRENT_STATE);
    const auto flags = m.Get<uint32_t>(MODE_FLAGS);
    if (screen != m.Int(WANTED_STATE)) { s.phase = Phase::Loading; return s; }
    if (flags & 2) { s.phase = Phase::Title; s.mode = "デモ"; return s; }
    if (screen != 2) {
        switch (screen) {
        case 1: s.phase = Phase::Title; break;
        case 5: case 6: s.phase = Phase::Results; break;
        case 8: s.phase = Phase::Menu; break;
        case 9: s.phase = Phase::Ending; break;
        case 0: case 3: case 7: case 10: case 11: case 12: s.phase = Phase::Loading; break;
        }
        return s;
    }
    const int progress = (flags >> 5) & 3;
    if (progress) { s.phase = progress == 1 ? Phase::Loading : Phase::Results; return s; }
    if (m.Int(GAME_ACTIVITY, 0, 2)) { s.phase = Phase::Loading; return s; }
    if (m.Flag(RETRY_STATE)) { s.phase = flags & (0x4000 | 1 | 8) ? Phase::Results : Phase::GameOver; return s; }
    s.phase = m.Flag(PAUSE_STATE, 2) ? Phase::Paused : Phase::Playing;
    s.mode = flags & 8 ? "リプレイ鑑賞" : flags & 0x4000 ? "スペルプラクティス" : flags & 1 ? "プラクティス" : "通常プレイ";
    s.difficulty = Difficulty(m.Int(DIFFICULTY));
    constexpr th_glossary_t names[] = { TH_TRACKER_BORDER_TEAM, TH_TRACKER_MAGIC_TEAM, TH_TRACKER_SCARLET_TEAM, TH_TRACKER_NETHERWORLD_TEAM,
        TH_TRACKER_REIMU, TH_TRACKER_YUKARI, TH_TRACKER_MARISA, TH_TRACKER_ALICE, TH_TRACKER_SAKUYA, TH_TRACKER_REMILIA, TH_TRACKER_YOUMU, TH_TRACKER_YUYUKO };
    s.character = Label(names, m.Flag(CHARACTER, 11));
    const char* stages[] = { "Stage 1", "Stage 2", "Stage 3", "Stage 4A", "Stage 4B", "Stage 5", "Stage 6A", "Stage 6B", "Extra" };
    s.stage = Choice(stages, m.Flag(STAGE, 8));
    const uintptr_t g = m.Pointer(GLOBALS_PTR);
    s.lives = m.Float(g + 0x74, 0, 8, true); s.bombs = m.Float(g + 0x80, 0, 8, true); s.power = m.Float(g + 0x98, 0, 128, true);
    s.score = int64_t(m.Int(g, 0, 999999999)) * 10 + m.Flag(g + 0x29, 9);
    s.graze = m.Int(g + 0xc); s.stagePointItems = m.Int(g + 0x2c); s.pointItems = m.Int(g + 0x30);
    const bool spell = (m.Get<uint32_t>(SPELL_FLAGS) & 1) != 0;
    s.segment = spell || m.Flag(BOSS_ACTIVE) ? "ボス戦" : "道中";
    if (spell) s.spell = "スペル #" + std::to_string(m.Int(SPELL_ID, 0, 221) + 1);
    const char* clocks[] = { "23:00", "23:30", "0:00", "0:30", "1:00", "1:30", "2:00", "2:30", "3:00", "3:30", "4:00", "4:30", "5:00" };
    s.extra = std::string("時刻 ") + Choice(clocks, m.Flag(g + 0x28, 12)) + " | 刻符 " + std::to_string(m.Int(g + 0x3c, 0, 9999999)) + " / " + std::to_string(m.Int(g + 0x40, 0, 9999999)) + " | 人妖 " + Number(Range<int>(m.Get<int16_t>(g + 0x22), -10000, 10000) / 100.0) + "%";
    return s;
}

Snapshot Capture09(const EarlyMemory& m)
{
    enum addrs : uintptr_t {
        CURRENT_STATE = 0x4b3690,
        WANTED_STATE = 0x4b368c,
        GAME_ACTIVITY = 0x4a7eb4,
        MODE_FLAGS = 0x4a7ec4,
        GAME_MODE = 0x4a7ea8,
        PAUSE_STATE = 0x4a7ecc,
        RETRY_STATE = 0x4a7ecd,
        RESULT_STATE = 0x4a7ece,
        DIFFICULTY = 0x4a7eac,
        P1_CPU = 0x4a7db8,
        P2_CPU = 0x4a7df0,
        P1_CHARACTER = 0x4a7db0,
        P2_CHARACTER = 0x4a7de8,
        ROUND = 0x4a7e90,
        STAGE = 0x4a7e8c,
        P1_PLAYER_PTR = 0x4a7d94,
        P2_PLAYER_PTR = 0x4a7dcc,
        P1_RESOURCES_PTR = 0x4a7dac,
        P2_RESOURCES_PTR = 0x4a7de4,
        P1_ROUNDS_WON = 0x4a7e98,
        P2_ROUNDS_WON = 0x4a7e9c,
        ROUND_TIME = 0x4a7e60,
    };
    auto s = Empty(TH09_TITLE, Phase::Unknown);
    const int screen = m.Int(CURRENT_STATE);
    if (screen != m.Int(WANTED_STATE)) { s.phase = Phase::Loading; return s; }
    if (screen != 2) {
        s.phase = screen == 1 ? Phase::Menu : screen == 9 ? Phase::Ending : screen == 0 || screen == 3 ? Phase::Loading : Phase::Unknown;
        return s;
    }
    if (m.Int(GAME_ACTIVITY)) { s.phase = Phase::Loading; return s; }
    const auto flags = m.Get<uint32_t>(MODE_FLAGS);
    if (!(flags & 4)) return s;
    if (flags & 2) { s.phase = Phase::Title; s.mode = "デモ"; return s; }
    const int mode = m.Int(GAME_MODE, 0, 2);
    const bool match = mode == 2;
    const char* modes[] = { "ストーリー", "Extra", "対戦" };
    s.mode = Choice(modes, mode); if (flags & 8) s.mode += "・リプレイ鑑賞";
    const int pause = m.Flag(PAUSE_STATE, 2), retry = m.Flag(RETRY_STATE), result = m.Flag(RESULT_STATE);
    if ((retry && result) || (retry && match) || (result && !match)) return Empty(TH09_TITLE, Phase::Unknown);
    if (retry || result) { s.phase = retry ? Phase::GameOver : Phase::Results; return s; }
    s.phase = pause ? Phase::Paused : Phase::Playing;
    s.difficulty = Difficulty(m.Int(DIFFICULTY));
    const bool cpu1 = m.Int(P1_CPU, 0, 1) != 0, cpu2 = m.Int(P2_CPU, 0, 1) != 0;
    const bool use2 = match && cpu1 && !cpu2;
    std::string c1 = Character09(m.Int(P1_CHARACTER, 0, 15)), c2 = Character09(m.Int(P2_CHARACTER, 0, 15));
    if (match) { c1 += cpu1 ? "（P1・CPU）" : "（P1・人間）"; c2 += cpu2 ? "（P2・CPU）" : "（P2・人間）"; }
    s.character = use2 ? c2 : c1; s.opponent = use2 ? c1 : c2;
    s.stage = match ? "Round " + std::to_string(m.Int(ROUND, 0, 999) + 1) : "Stage " + std::to_string(m.Int(STAGE, 0, 8) + 1);
    const auto p1 = m.Pointer(P1_PLAYER_PTR), p2 = m.Pointer(P2_PLAYER_PTR), r1 = m.Pointer(P1_RESOURCES_PTR), r2 = m.Pointer(P2_RESOURCES_PTR);
    const double health1 = m.Int(p1 + 0xa8, 0, 10) / 2.0, health2 = m.Int(p2 + 0xa8, 0, 10) / 2.0;
    const auto r = use2 ? r2 : r1;
    s.score = int64_t(m.Int(r + 8, 0, 999999999)) * 10 + m.Int(r + 0x14, 0, 9);
    s.extra = "1P ";
    if (!match && !cpu1) s.extra += "残機 " + Number(m.Float(r1, 0, 7, true)) + " | ";
    s.extra += "体力 " + Number(health1) + " | 取得ラウンド " + std::to_string(m.Int(P1_ROUNDS_WON, 0, 999)) + "\n2P 体力 " + Number(health2) + " | 取得ラウンド " + std::to_string(m.Int(P2_ROUNDS_WON, 0, 999));
    s.elapsedSeconds = m.Int(ROUND_TIME, 0, 60 * 60 * 24 * 7 * 60) / 60.0; s.elapsedLabel = "ラウンド経過";
    return s;
}

namespace {
// These titles share the shape, but not the addresses or pause-state ranges.
struct ModernEarlyLayout {
    th_glossary_t title;
    uintptr_t supervisor, mode, menu, ending, game, pause, replay, globals, enemy;
    int endingState, pauseMaximum, resultFirst, gameOverFirst, flagsOffset, replayOffset, bossOffset;
};

Snapshot CaptureModernEarly(const ModernEarlyLayout& l, const EarlyMemory& m)
{
    auto s = Empty(l.title, Phase::Unknown);
    const int state = m.Int(l.supervisor);
    if (state != m.Int(l.supervisor + 4)) { s.phase = Phase::Loading; return s; }
    if (state == 4) {
        if (!m.Get<uint32_t>(l.menu)) { s.phase = Phase::Loading; return s; }
        const int menu = m.Int(m.Pointer(l.menu) + 0x1c, 0, l.title == TH10_TITLE ? 16 : 15);
        s.phase = menu == 0 || (l.title != TH10_TITLE && menu == 2) ? Phase::Loading : menu == 1 || (l.title == TH10_TITLE && menu == 2) ? Phase::Title : Phase::Menu;
        return s;
    }
    if (state == l.endingState) { s.phase = m.Get<uint32_t>(l.ending) ? Phase::Ending : Phase::Loading; return s; }
    if (state != 7) { if (state == 0 || state == 1 || (state >= 10 && state <= 16)) s.phase = Phase::Loading; return s; }
    const auto mode = m.Get<uint32_t>(l.mode);
    if (mode & 0x20) { s.phase = Phase::Title; s.mode = "デモ"; return s; }
    if (!m.Get<uint32_t>(l.game) || !m.Get<uint32_t>(l.pause) || !m.Get<uint32_t>(l.replay)) { s.phase = Phase::Loading; return s; }
    const auto game = m.Pointer(l.game), pause = m.Pointer(l.pause), replayManager = m.Pointer(l.replay);
    const auto flags = m.Get<uint32_t>(game + l.flagsOffset);
    const int pauseState = m.Int(pause + 4, 0, l.pauseMaximum);
    if (flags & 0x80c) { s.phase = Phase::Loading; return s; }
    const bool replay = m.Int(game + l.replayOffset, 0, 1) != 0;
    if (l.title != TH12_TITLE && m.Int(replayManager + 0x10, 0, 1) != int(replay)) { s.phase = Phase::Loading; return s; }
    if (pauseState >= l.gameOverFirst) {
        s.phase = pauseState >= l.resultFirst || (l.title == TH10_TITLE && ((mode & 0x10) || replay)) ? Phase::Results : Phase::GameOver;
        return s;
    }
    if (pauseState && !(flags & 0x10)) { s.phase = Phase::Loading; return s; }
    if (!pauseState && (flags & 0x70)) return s;
    s.phase = pauseState ? Phase::Paused : Phase::Playing;
    s.mode = replay ? "リプレイ鑑賞" : mode & 0x10 ? "プラクティス" : "通常プレイ";
    s.segment = HasBoss(m, l.enemy, l.bossOffset) ? "ボス戦" : "道中";
    return s;
}
bool InRun(const Snapshot& s) { return s.phase == Phase::Playing || s.phase == Phase::Paused; }
}

Snapshot Capture10(const EarlyMemory& m)
{
    enum addrs : uintptr_t {
        SUPERVISOR = 0x491fb4,
        MODE_FLAGS = 0x474ca0,
        MAIN_MENU_PTR = 0x47784c,
        ENDING_PTR = 0x477700,
        GAME_PTR = 0x477810,
        PAUSE_PTR = 0x477830,
        REPLAY_PTR = 0x477838,
        GLOBALS = 0x474c40,
        ENEMY_PTR = 0x477704,
    };
    const ModernEarlyLayout l { TH10_TITLE, SUPERVISOR, MODE_FLAGS, MAIN_MENU_PTR, ENDING_PTR, GAME_PTR, PAUSE_PTR, REPLAY_PTR, GLOBALS, ENEMY_PTR, 14, 13, 14, 6, 0x58, 0x5c, 0x10 };
    auto s = CaptureModernEarly(l, m); if (!InRun(s)) return s;
    const auto g = l.globals;
    s.difficulty = Difficulty(m.Int(g + 0x34)); s.character = Label(Characters, m.Int(g + 0x28, 0, 1)); s.equipment = Choice(Shots, m.Int(g + 0x2c, 0, 2));
    s.stage = Stage(m.Int(g + 0x3c)); s.lives = m.Int(g + 0x30, 0, 9);
    s.power = Range<int>(m.Get<uint16_t>(g + 8), 0, 100) / 20.0;
    s.score = int64_t(m.Int(g + 4, 0, 999999999)) * 10 + m.Int(g + 0x50, 0, 9);
    s.elapsedSeconds = m.Int(g + 0x48) / 60.0; s.elapsedLabel = "面経過";
    s.extra = "信仰 " + std::to_string(m.Int(g + 0xc, 0, 99999) * 10) + " | 信仰ゲージ " + std::to_string(m.Int(g + 0x18, 0, 130));
    return s;
}

Snapshot Capture11(const EarlyMemory& m)
{
    enum addrs : uintptr_t {
        SUPERVISOR = 0x4c37d4,
        MODE_FLAGS = 0x4a5758,
        MAIN_MENU_PTR = 0x4a8ecc,
        ENDING_PTR = 0x4a8d78,
        GAME_PTR = 0x4a8e88,
        PAUSE_PTR = 0x4a8eb0,
        REPLAY_PTR = 0x4a8eb8,
        GLOBALS = 0x4a56e0,
        ENEMY_PTR = 0x4a8d7c,
    };
    const ModernEarlyLayout l { TH11_TITLE, SUPERVISOR, MODE_FLAGS, MAIN_MENU_PTR, ENDING_PTR, GAME_PTR, PAUSE_PTR, REPLAY_PTR, GLOBALS, ENEMY_PTR, 15, 26, 20, 12, 0x60, 0x74, 0x1c };
    auto s = CaptureModernEarly(l, m); if (!InRun(s)) return s;
    const auto g = l.globals;
    const int character = m.Int(g + 0x30, 0, 1), shot = m.Int(g + 0x34, 0, 2);
    constexpr th_glossary_t teams[] = { TH_TRACKER_REIMU_YUKARI, TH_TRACKER_REIMU_SUIKA, TH_TRACKER_REIMU_AYA, TH_TRACKER_MARISA_ALICE, TH_TRACKER_MARISA_PATCHOULI, TH_TRACKER_MARISA_NITORI };
    s.character = Label(teams, character * 3 + shot);
    s.difficulty = Difficulty(m.Int(g + 0x40)); s.stage = Stage(m.Int(g + 0x48)); s.lives = m.Int(g + 0x38, 0, 9);
    const int maximum = m.Int(g + 0x68, 1, 10000), denominator = m.Int(g + 0x6c, 1, 10000);
    s.power = Range(m.Int(g + 8, 0, maximum) / double(denominator), 0.0, 8.0);
    s.score = int64_t(m.Int(g + 4, 0, 999999999)) * 10 + m.Int(g + 0x5c, 0, 9);
    s.elapsedSeconds = m.Int(g + 0x54) / 60.0; s.elapsedLabel = "面経過";
    s.lifePieces = std::to_string(m.Int(g + 0x3c, 0, 4)) + "/5";
    s.pointItemValue = m.Int(g + 0x10) / 100.0; s.graze = m.Int(g + 0x74, 0, 99999999);
    return s;
}

Snapshot Capture12(const EarlyMemory& m)
{
    enum addrs : uintptr_t {
        SUPERVISOR = 0x4cee3c,
        MODE_FLAGS = 0x4b0ce0,
        MAIN_MENU_PTR = 0x4b4530,
        ENDING_PTR = 0x4b43d8,
        GAME_PTR = 0x4b44e8,
        PAUSE_PTR = 0x4b4510,
        REPLAY_PTR = 0x4b4518,
        GLOBALS = 0x4b0c40,
        ENEMY_PTR = 0x4b43dc,
        UFO_PTR = 0x4b4534,
        SPELL_PTR = 0x4b43cc,
    };
    const ModernEarlyLayout l { TH12_TITLE, SUPERVISOR, MODE_FLAGS, MAIN_MENU_PTR, ENDING_PTR, GAME_PTR, PAUSE_PTR, REPLAY_PTR, GLOBALS, ENEMY_PTR, 15, 26, 20, 12, 0x60, 0x74, 0x1c };
    auto s = CaptureModernEarly(l, m); if (!InRun(s)) return s;
    const auto g = l.globals;
    constexpr th_glossary_t names[] = { TH_TRACKER_REIMU_A, TH_TRACKER_REIMU_B, TH_TRACKER_MARISA_A, TH_TRACKER_MARISA_B, TH_TRACKER_SANAE_A, TH_TRACKER_SANAE_B };
    s.character = Label(names, m.Int(g + 0x50, 0, 2) * 2 + m.Int(g + 0x54, 0, 1));
    s.difficulty = Difficulty(m.Int(g + 0x68)); s.stage = Stage(m.Int(g + 0x70));
    s.lives = m.Int(g + 0x58, 0, 9); s.bombs = m.Int(g + 0x60, 0, 9);
    const int denominator = m.Int(g + 0x94, 1, 10000);
    s.power = Range(m.Int(g + 8, 0, 10000) / double(denominator), 0.0, 8.0);
    s.score = int64_t(m.Int(g + 4, 0, 999999999)) * 10 + m.Int(g + 0x84, 0, 9);
    s.elapsedSeconds = m.Int(g + 0x7c) / 60.0; s.elapsedLabel = "面経過";
    s.graze = m.Int(g + 0x9c, 0, 9999999); s.pointItemValue = m.Int(g + 0x38) / 100 / 10 * 10;
    const int life = m.Int(g + 0x5c, 0, 4), bomb = m.Int(g + 0x64, 0, 4);
    s.lifePieces = life == 0 ? "0/4" : life == 1 ? "未確定" : std::to_string(life - 1) + "/4";
    s.bombPieces = bomb % 2 ? "未確定" : std::to_string(bomb / 2) + "/3";
    const char* colors[] = { "", "赤", "青", "緑" };
    const int count = m.Int(g + 0x18, 0, 3);
    s.extra = "ベントラー ";
    if (!count) s.extra += "なし";
    for (int i = 0; i < count; ++i) { if (i) s.extra += "→"; s.extra += Choice(colors, m.Int(g + 0xc + i * 4, 1, 3)); }
    const auto ufo = m.Pointer(UFO_PTR);
    const bool activeUfo = m.Int(ufo + 0x2c, 0, 1) && m.Get<uint32_t>(ufo + 0x38);
    s.extra += " | UFO ";
    if (activeUfo) {
        const int color = m.Int(ufo + 0x30, -1, 3);
        if (!color) throw std::runtime_error("Invalid UFO color");
        s.extra += color == -1 ? "虹" : Choice(colors, color);
    } else s.extra += "なし";
    const bool spell = (m.Get<uint32_t>(m.Pointer(SPELL_PTR) + 0x7c) & 1) != 0;
    if (spell) { s.spell = "スペル発動中"; s.segment = "ボス戦"; }
    return s;
}

Snapshot Capture128(const EarlyMemory& m)
{
    enum addrs : uintptr_t {
        MAIN_MENU_PTR = 0x4b8a9c,
        ENDING_PTR = 0x4b8944,
        GAME_PTR = 0x4b8954,
        PLAYER_PTR = 0x4b8a80,
        PAUSE_PTR = 0x4b8a7c,
        REPLAY_MODE = 0x4d2e30,
        STAGE = 0x4b4d14,
        MODE_FLAGS = 0x4b4d50,
        DIFFICULTY = 0x4b4d0c,
        SCORE = 0x4b4cc4,
        CONTINUES = 0x4b4d28,
        STAGE_TIME = 0x4b4d20,
        MOTIVATION = 0x4b4d64,
        PERFECT_FREEZE = 0x4b4d70,
        ICE_POWER = 0x4b4d6c,
        ICE_AREA = 0x4b4d60,
    };
    // TH128's globals and stage numbering are documented by TouhouRPC's
    // Touhou12_8.h; object ownership and menu offsets by th-re-data. Resource
    // units also match this repository's th128_patch_main setters. We do not
    // infer bosses or end states from music or elapsed-frame thresholds.
    auto s = Empty(TH128_TITLE, Phase::Unknown);
    if (m.Get<uint32_t>(MAIN_MENU_PTR)) {
        const int menu = m.Int(m.Pointer(MAIN_MENU_PTR) + 0x1c, 0, 16);
        s.phase = menu == 0 || menu == 2 ? Phase::Loading : menu == 1 ? Phase::Title : Phase::Menu;
        return s;
    }
    if (m.Get<uint32_t>(ENDING_PTR)) { s.phase = Phase::Ending; return s; }
    if (!m.Get<uint32_t>(GAME_PTR) || !m.Get<uint32_t>(PLAYER_PTR) || !m.Get<uint32_t>(PAUSE_PTR)) {
        s.phase = Phase::Loading; return s;
    }
    const int pause = m.Int(m.Pointer(PAUSE_PTR) + 4);
    // The source describes this as a submenu discriminator, without proving
    // which nonzero values mean pause, retry, or results. Clear run resources
    // while any such menu is active rather than exposing a previous run.
    if (pause != 0) { s.phase = Phase::Menu; return s; }
    const int replay = m.Int(REPLAY_MODE, 1, 2);
    const int stage = m.Int(STAGE, 1, 16);
    s.phase = Phase::Playing;
    s.mode = replay == 2 ? "リプレイ鑑賞" : m.Get<uint32_t>(MODE_FLAGS) & 1 ? "プラクティス" : "通常プレイ";
    s.difficulty = Difficulty(m.Int(DIFFICULTY)); s.character = S(TH_TRACKER_CIRNO);
    const char* stages[] = { "A1-1", "A1-2", "A1-3", "A2-2", "A2-3", "B1-1", "B1-2", "B1-3", "B2-2", "B2-3", "C1-1", "C1-2", "C1-3", "C2-2", "C2-3", "Extra" };
    s.stage = stage == 16 ? "Extra" : std::string("Stage ") + Choice(stages, stage - 1);
    s.score = int64_t(m.Int(SCORE, 0, 999999999)) * 10 + m.Int(CONTINUES, 0, 9);
    s.elapsedSeconds = m.Int(STAGE_TIME) / 60.0; s.elapsedLabel = "面経過";
    s.extra = std::string(S(TH128_MOTIVATION)) + " " + Number(m.Int(MOTIVATION, 0, 100000) / 100.0) + "% | "
        + S(TH128_PERFECT_FREEZE) + " " + Number(m.Int(PERFECT_FREEZE, 0, 30000) / 100.0) + "% | "
        + S(TH128_ICE_POWER) + " " + Number(m.Int(ICE_POWER, 0, 10000) / 100.0) + "% | "
        + S(TH128_ICE_AREA) + " " + Number(m.Float(ICE_AREA, 0, 3000)) + "%";
    return s;
}
}
