#include "thprac_discord_scenes.h"
#include "thprac_gui_locale.h"
#include <cstring>

// Address/state facts (independently implemented, no third-party RPC code):
// TheBakaRem/TouhouRPC games/Touhou09_5.h, Touhou12_5.h, Touhou14_3.h;
// N0zoM1z0/th095 src/PhotoGameTask.hpp, ZunTimer.hpp, GameplayGlobals.hpp;
// pingval/AutoSplitters th095.asl, th125.asl, th165.asl;
// thpatch/thcrap-tsa base_tsa/th165.js (current scene record);
// exphp-share/th-re-data data/th{095,125,143,165} (object ownership).
// All reads are from the already identified, supported in-process executable.
namespace DiscordPresence {
namespace {
enum SceneAddresses : uintptr_t {
    STB_MODE = 0x4b1fe4, STB_GAME = 0x4c4df4, STB_OWNER = 0x4bdec8,
    DS_MENU = 0x4b68e8, DS_GAME = 0x4b6898, DS_PLAYER = 0x4b68c8,
    DS_CHARACTER = 0x4b308c, DS_SCENE = 0x4b30ac, DS_SCORE = 0x4b305c,
    DS_ACTIVITY = 0x4d3850, DS_TIME = 0x4b30c0,
    ISC_MENU = 0x4e6bb4, ISC_GAME = 0x4e6a5c, ISC_PLAYER = 0x4e6b88,
    ISC_ACTIVITY = 0x53b150, ISC_MODE = 0x4e6bb0, ISC_LIVES = 0x4e475c,
    ISC_SCENE = 0x4e479c, ISC_SCORE = 0x4e46f8, ISC_GRAZE = 0x4e4708,
    ISC_TIME = 0x4e47a8, ISC_MAIN_ITEM = 0x4e4710, ISC_SUB_ITEM = 0x4e4714,
    ISC_ITEMS = 0x4e6b68, ISC_SCOREFILE = 0x4e6b9c,
    VD_MENU = 0x4b5674, VD_GAME = 0x4b552c, VD_PLAYER = 0x4b5654,
    VD_SCENE_RECORD = 0x4b566c, VD_SCENE_MODE = 0x4b5670,
};

uintptr_t Pointer(const EarlyMemory& m, uintptr_t address)
{
    auto pointer = m.Get<uint32_t>(address);
    if (pointer && pointer < 0x10000) throw std::runtime_error("Invalid scene pointer");
    return pointer;
}

std::string Character(int index)
{
    const char* text = S(THPRAC_GAMEROLL_TH125_SHOTTYPES);
    while (index-- > 0) text += strlen(text) + 1;
    return text;
}

Snapshot Menu(th_glossary_t title, Phase phase = Phase::Menu)
{
    Snapshot s;
    s.title = S(title);
    s.phase = phase;
    return s;
}

std::string PhotoScene(int scene, int extraLevel)
{
    int level = scene / 10;
    std::string levelName = level == extraLevel ? "EX" : level == extraLevel + 1 ? "SP" : std::to_string(level + 1);
    return "Level " + levelName + " | Scene " + std::to_string(scene % 10 + 1);
}

void PhotoCount(Snapshot& s, int count, int required)
{
    if (count > required || required < 1) throw std::runtime_error("Invalid photo count");
    s.extra = "撮影 " + std::to_string(count) + "/" + std::to_string(required);
}

void Time(Snapshot& s, int frames)
{
    s.elapsedSeconds = frames / 60.0;
    s.elapsedLabel = "シーン経過";
}

void StillOwned(const EarlyMemory& m, uintptr_t slot, uintptr_t owner)
{
    if (Pointer(m, slot) != owner) throw std::runtime_error("Scene changed during capture");
}
}

Snapshot Capture095(const EarlyMemory& m)
{
    auto s = Menu(TH095_TITLE);
    auto game = Pointer(m, STB_GAME);
    auto mode = m.Flag(STB_MODE, 21);
    if (mode == 5 || mode == 21) return s;
    if (mode != 1 && mode != 2) return Menu(TH095_TITLE, Phase::Unknown);
    if (!game) return Menu(TH095_TITLE, Phase::Title);
    // Supervisor publishes the task separately from its construction owner.
    if (Pointer(m, STB_OWNER) != game) return Menu(TH095_TITLE, Phase::Loading);
    auto flags = m.Get<uint32_t>(game + 0xfc);
    if (flags & 0xc) return Menu(TH095_TITLE, Phase::Loading);
    s.phase = flags & 0x20 ? Phase::GameOver : flags & 0x40 ? Phase::Clear : flags & 0x10 ? Phase::Paused : Phase::Playing;
    s.mode = mode == 2 ? "リプレイ鑑賞" : "通常プレイ";
    s.character = Character(0);
    int scene = m.Int(game + 0x100, 0, 107);
    static const int counts[] = {6, 6, 8, 9, 8, 8, 8, 8, 8, 8, 8};
    if (scene % 10 >= counts[scene / 10]) return Menu(TH095_TITLE, Phase::Unknown);
    s.stage = PhotoScene(scene, 10);
    s.score = m.Int(game + 0x114);
    // zTimer is {previous, subFrame, current}, not three integers.
    Time(s, m.Int(game + 0x30, 0, 3600 * 60 * 24));
    auto player = Pointer(m, game + 0xc);
    if (!player) return Menu(TH095_TITLE, Phase::Loading);
    PhotoCount(s, m.Int(player + 0x29e4, 0, 10), m.Int(player + 0x29ec, 1, 10));
    StillOwned(m, STB_GAME, game);
    return s;
}

Snapshot Capture125(const EarlyMemory& m)
{
    auto s = Menu(TH125_TITLE);
    if (Pointer(m, DS_MENU)) return s;
    auto game = Pointer(m, DS_GAME), player = Pointer(m, DS_PLAYER);
    if (!game || !player) return Menu(TH125_TITLE, Phase::Loading);
    int active = m.Int(DS_ACTIVITY, 0, 2);
    int playerState = m.Int(player + 0x10, 0, 3);
    s.phase = active == 0 ? Phase::Paused : playerState == 2 ? Phase::GameOver : playerState == 3 ? Phase::Clear : Phase::Playing;
    if (playerState == 0) return Menu(TH125_TITLE, Phase::Loading);
    // No menu-history or BGM heuristic: those misclassify mid-replay injection.
    // The photography title has no difficulty setting.
    s.character = Character(m.Int(DS_CHARACTER, 0, 1));
    s.stage = PhotoScene(m.Int(DS_SCENE, 0, 138), 12);
    s.score = m.Int(DS_SCORE);
    Time(s, m.Int(DS_TIME, 0, 3600 * 60 * 24));
    PhotoCount(s, m.Int(player + 0x3834, 0, 10), m.Int(player + 0x383c, 1, 10));
    StillOwned(m, DS_GAME, game);
    StillOwned(m, DS_PLAYER, player);
    return s;
}

Snapshot Capture143(const EarlyMemory& m)
{
    auto s = Menu(TH143_TITLE);
    if (Pointer(m, ISC_MENU)) return s;
    auto game = Pointer(m, ISC_GAME), player = Pointer(m, ISC_PLAYER);
    if (!game || !player) return Menu(TH143_TITLE, Phase::Loading);
    int active = m.Int(ISC_ACTIVITY, 0, 2), mode = m.Int(ISC_MODE, 1, 2);
    int lives = m.Int(ISC_LIVES, -1, 0);
    s.phase = active == 0 ? Phase::Paused : active == 1 ? (lives == -1 ? Phase::GameOver : Phase::Clear) : Phase::Playing;
    s.mode = mode == 2 ? "リプレイ鑑賞" : "通常プレイ";
    s.character = Gui::LocaleGet() == LOCALE_JA_JP ? "正邪" : Gui::LocaleGet() == LOCALE_ZH_CN ? "正邪" : "Seija";
    int scene = m.Int(ISC_SCENE, 1, 75);
    constexpr int ends[] = {6, 12, 19, 26, 34, 42, 50, 57, 65, 75};
    int day = 0;
    while (scene > ends[day]) ++day;
    s.stage = "Day " + std::to_string(day + 1) + " | Scene " + std::to_string(scene - (day ? ends[day - 1] : 0));
    s.score = int64_t(m.Int(ISC_SCORE)) * 10;
    s.graze = m.Int(ISC_GRAZE);
    Time(s, m.Int(ISC_TIME, 0, 3600 * 60 * 24));
    static const char* const items[] = {"ひらり布", "天狗のトイカメラ", "隙間の折りたたみ傘", "亡霊の送り提灯", "血に餓えた陰陽玉", "四尺マジックボム", "身代わり地蔵", "呪いのデコイ人形", "打ち出の小槌（レプリカ）"};
    auto item = Pointer(m, ISC_ITEMS);
    if (item) {
        s.equipment = std::string("メイン ") + items[m.Int(ISC_MAIN_ITEM, 0, 8)];
        s.extra = "メイン残り " + std::to_string(m.Int(item + 0x2bc, 0, 999));
        auto scorefile = Pointer(m, ISC_SCOREFILE);
        if (scene == 35 || (scorefile && m.Int(scorefile + 0x6c28) > 0)) {
            s.equipment += std::string(" | サブ ") + items[m.Int(ISC_SUB_ITEM, 0, 8)];
            s.extra += " | サブ残り " + std::to_string(m.Int(item + 0x2c0, 0, 999));
        }
    }
    StillOwned(m, ISC_GAME, game);
    StillOwned(m, ISC_PLAYER, player);
    return s;
}

Snapshot Capture165(const EarlyMemory& m)
{
    auto s = Menu(TH165_TITLE);
    if (Pointer(m, VD_MENU) || m.Int(VD_SCENE_MODE, 0, 2) == 1) return s;
    auto game = Pointer(m, VD_GAME), player = Pointer(m, VD_PLAYER);
    if (!game || !player) return Menu(TH165_TITLE, Phase::Loading);
    auto record = Pointer(m, VD_SCENE_RECORD);
    if (!record) return Menu(TH165_TITLE, Phase::Loading);
    int scene = m.Int(record, 0, 102);
    auto flags = m.Get<uint32_t>(game + 0x84);
    s.phase = flags & 0x10 ? Phase::Paused : Phase::Playing;
    s.mode = m.Int(game + 0x98, 0, 1) ? "リプレイ鑑賞" : "通常プレイ";
    s.character = Gui::LocaleGet() == LOCALE_JA_JP ? "菫子" : Gui::LocaleGet() == LOCALE_ZH_CN ? "堇子" : "Sumireko";
    // The scene record is the live spell/scene identifier used by thcrap.
    // Show its number; persistent scorefile day/unlock values are not live state.
    s.stage = "Scene " + std::to_string(scene + 1);
    StillOwned(m, VD_GAME, game);
    StillOwned(m, VD_PLAYER, player);
    StillOwned(m, VD_SCENE_RECORD, record);
    return s;
}
}
