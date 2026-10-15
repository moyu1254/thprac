#define NOMINMAX
#include "../thprac/src/thprac_presence_early.cpp"
#include "../thprac/src/thprac_discord_scenes.cpp"
#include "../thprac/src/thprac_locale_def.cpp"
#include <iostream>
#include <unordered_map>

namespace Gui { Locale __glocale_current = LOCALE_JA_JP; }
namespace {
using namespace DiscordPresence;
int checks = 0;
void Check(bool value, const char* what) { ++checks; if (!value) throw std::runtime_error(what); }
struct Memory {
    std::unordered_map<uintptr_t, uint8_t> bytes;
    template<typename T> void Put(uintptr_t address, T value) {
        auto data = reinterpret_cast<uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) bytes[address + i] = data[i];
    }
    void Ptr(uintptr_t address, uintptr_t value) { Put(address, static_cast<uint32_t>(value)); }
    static bool Read(void* context, uintptr_t address, void* output, size_t size) {
        auto& memory = *static_cast<Memory*>(context);
        for (size_t i = 0; i < size; ++i) {
            auto found = memory.bytes.find(address + i);
            if (found == memory.bytes.end()) return false;
            static_cast<uint8_t*>(output)[i] = found->second;
        }
        return true;
    }
    EarlyMemory Reader() { return {Read, this}; }
};
constexpr uintptr_t game = 0x1000000, player = 0x1100000, items = 0x1200000, scene = 0x1300000;
template<typename F> void Rejected(F operation) {
    bool rejected = false;
    try { auto value = operation(); rejected = value.phase == Phase::Unknown || value.phase == Phase::Loading; }
    catch (const std::runtime_error&) { rejected = true; }
    Check(rejected, "Invalid scene must not retain live details");
}

void Stb() {
    Memory m;
    m.Ptr(STB_GAME, 0); m.Put<uint8_t>(STB_MODE, 1);
    Check(Capture095(m.Reader()).phase == Phase::Title, "STB title with no stale stats");
    m.Ptr(STB_GAME, game); m.Ptr(STB_OWNER, game);
    m.Put(game + 0xfc, 0u); m.Put(game + 0x100, 0); m.Put(game + 0x114, 12345);
    m.Put(game + 0x30, 600); m.Ptr(game + 0xc, player);
    m.Put(player + 0x29e4, 2); m.Put(player + 0x29ec, 6);
    auto s = Capture095(m.Reader());
    Check(s.phase == Phase::Playing && s.score == 12345 && s.elapsedSeconds == 10, "STB score and current timer units");
    Check(s.stage == "Level 1 | Scene 1" && s.extra == "撮影 2/6", "STB level/photo fields");
    m.Put<uint8_t>(STB_MODE, 2); m.Put(game + 0xfc, 0x10u);
    s = Capture095(m.Reader()); Check(s.mode == "リプレイ鑑賞" && s.phase == Phase::Paused, "STB replay pause");
    m.Put(game + 0xfc, 0x20u); Check(Capture095(m.Reader()).phase == Phase::GameOver, "STB failed scene");
    m.Put(game + 0xfc, 0x40u); Check(Capture095(m.Reader()).phase == Phase::Clear, "STB cleared scene");
    m.Put(game + 0xfc, 4u); Rejected([&] { return Capture095(m.Reader()); });
    m.Put(game + 0xfc, 0u); m.Put(game + 0x100, 9); Rejected([&] { return Capture095(m.Reader()); });
    m.Put<uint8_t>(STB_MODE, 5); s = Capture095(m.Reader()); Check(s.phase == Phase::Menu && !s.score && s.stage.empty(), "STB selection drops run data");
}
void Ds() {
    Memory m; m.Ptr(DS_MENU, game);
    Check(Capture125(m.Reader()).phase == Phase::Menu, "DS menu doesn't read stale game");
    m.Ptr(DS_MENU, 0); m.Ptr(DS_GAME, game); m.Ptr(DS_PLAYER, player);
    m.Put(DS_ACTIVITY, 2); m.Put(player + 0x10, 1); m.Put(DS_CHARACTER, 1);
    m.Put(DS_SCENE, 120); m.Put(DS_SCORE, 5000); m.Put(DS_TIME, 120);
    m.Put(player + 0x3834, 3); m.Put(player + 0x383c, 6);
    auto s = Capture125(m.Reader());
    Check(s.phase == Phase::Playing && s.score == 5000 && s.character == "はたて", "DS character and unscaled score");
    Check(s.stage == "Level EX | Scene 1", "DS EX scene");
    m.Put(DS_ACTIVITY, 0); Check(Capture125(m.Reader()).phase == Phase::Paused, "DS pause");
    m.Put(DS_ACTIVITY, 1); m.Put(player + 0x10, 2); Check(Capture125(m.Reader()).phase == Phase::GameOver, "DS failed scene");
    m.Put(player + 0x10, 3); Check(Capture125(m.Reader()).phase == Phase::Clear, "DS clear");
    m.Put(DS_CHARACTER, 2); Rejected([&] { return Capture125(m.Reader()); });
    m.Put(DS_CHARACTER, 0); m.Put(player + 0x3834, 7); Rejected([&] { return Capture125(m.Reader()); });
    m.Ptr(DS_GAME, 0); Rejected([&] { return Capture125(m.Reader()); });
}
void Isc() {
    Memory m; m.Ptr(ISC_MENU, game);
    Check(Capture143(m.Reader()).phase == Phase::Menu, "ISC menu doesn't read stale run");
    m.Ptr(ISC_MENU, 0); m.Ptr(ISC_GAME, game); m.Ptr(ISC_PLAYER, player);
    m.Put(ISC_ACTIVITY, 2); m.Put(ISC_MODE, 1); m.Put(ISC_LIVES, 0);
    m.Put(ISC_SCENE, 35); m.Put(ISC_SCORE, 200); m.Put(ISC_GRAZE, 50); m.Put(ISC_TIME, 180);
    m.Ptr(ISC_ITEMS, items); m.Put(ISC_MAIN_ITEM, 8); m.Put(items + 0x2bc, 3);
    m.Ptr(ISC_SCOREFILE, 0); m.Put(ISC_SUB_ITEM, 6); m.Put(items + 0x2c0, 1);
    auto s = Capture143(m.Reader());
    Check(s.phase == Phase::Playing && s.score == 2000 && s.graze == 50, "ISC scaled score/graze");
    Check(s.stage == "Day 6 | Scene 1" && s.equipment.find("身代わり地蔵") != std::string::npos, "ISC day and unlocked sub item");
    m.Put(ISC_MODE, 2); m.Put(ISC_ACTIVITY, 0); s=Capture143(m.Reader());
    Check(s.phase == Phase::Paused && s.mode == "リプレイ鑑賞", "ISC paused replay");
    m.Put(ISC_ACTIVITY, 1); Check(Capture143(m.Reader()).phase == Phase::Clear, "ISC success");
    m.Put(ISC_LIVES, -1); Check(Capture143(m.Reader()).phase == Phase::GameOver, "ISC failure");
    m.Put(ISC_SCENE, 76); Rejected([&] { return Capture143(m.Reader()); });
}
void Vd() {
    Memory m; m.Ptr(VD_MENU, game);
    Check(Capture165(m.Reader()).phase == Phase::Menu, "VD menu doesn't read saved progress");
    m.Ptr(VD_MENU, 0); m.Put(VD_SCENE_MODE, 2); m.Ptr(VD_GAME, game); m.Ptr(VD_PLAYER, player);
    m.Ptr(VD_SCENE_RECORD, scene); m.Put(scene, 102); m.Put(game + 0x84, 0u); m.Put(game + 0x98, 0);
    auto s = Capture165(m.Reader());
    Check(s.phase == Phase::Playing && s.stage == "Scene 103" && s.character == "菫子", "VD live scene and character");
    m.Put(game + 0x84, 0x10u); m.Put(game + 0x98, 1); s=Capture165(m.Reader());
    Check(s.phase == Phase::Paused && s.mode == "リプレイ鑑賞", "VD paused replay");
    m.Put(scene, 103); Rejected([&] { return Capture165(m.Reader()); });
    m.Put(VD_SCENE_MODE, 1); s=Capture165(m.Reader());
    Check(s.phase == Phase::Menu && s.stage.empty(), "VD returns to menu");
    m.Put(VD_SCENE_MODE, 2); m.Ptr(VD_GAME, 0); Rejected([&] { return Capture165(m.Reader()); });
}
}
int main() {
    try { Stb(); Ds(); Isc(); Vd(); std::cout << "PASS " << checks << " photography/scene snapshot checks\n"; return 0; }
    catch(const std::exception& e) { std::cerr << "FAIL after " << checks << ": " << e.what() << '\n'; return 1; }
}
