#include "../thprac/src/thprac_presence_early.h"
#include "../thprac/src/thprac_presence_nc.h"
#include "../thprac/src/thprac_gui_locale.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace Gui { Locale __glocale_current = LOCALE_JA_JP; }
using namespace DiscordPresence;

namespace {
struct Fixture {
    std::unordered_map<uintptr_t, uint8_t> bytes;
    void Zero(uintptr_t address, size_t count) { for (size_t i = 0; i < count; ++i) bytes[address + i] = 0; }
    template<class T> void Put(uintptr_t address, T value)
    {
        const auto* data = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(value); ++i) bytes[address + i] = data[i];
    }
    static bool Read(void* context, uintptr_t address, void* destination, size_t count)
    {
        const auto& bytes = static_cast<Fixture*>(context)->bytes;
        auto* output = static_cast<uint8_t*>(destination);
        for (size_t i = 0; i < count; ++i) {
            const auto found = bytes.find(address + i);
            if (found == bytes.end()) return false;
            output[i] = found->second;
        }
        return true;
    }
    EarlyMemory Memory() { return { Read, this }; }
};
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void Reject(F action)
{
    bool rejected = false;
    try { action(); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected, "Invalid/missing memory was not rejected");
}
void NoRun(const Snapshot& s, Phase phase)
{
    Check(s.phase == phase, "Unexpected non-play phase");
    Check(!s.score && !s.lives && !s.bombs && s.stage.empty() && s.character.empty() && s.extra.empty(), "Stale run leaked into menu/result");
}

void Test06()
{
    Fixture f; constexpr uintptr_t g = 0x69bca0;
    f.Zero(g, 0x1b00); f.Put<int>(0x6c6ea0, 2); f.Put<int>(0x6c6ea4, 2);
    f.Put<int>(g + 0x1a34, 1); f.Put<int>(g + 0x10, 3); f.Put<int>(g, 123456);
    f.Put<int>(0x5a5f90, 0); f.Put<uint8_t>(0x69bc50, 0);
    auto s = Capture06(true, f.Memory());
    Check(s.phase == Phase::Playing && s.mode == "プラクティス" && s.score == 123456 && s.stage == "Stage 1", "TH06 practice/resources");
    f.Put<int>(g + 0x1c, 1); f.Put<uint8_t>(g + 0x181f, 2);
    s = Capture06(true, f.Memory()); Check(s.mode == "リプレイ鑑賞" && s.phase == Phase::Paused, "TH06 replay precedence/pause");
    f.Put<uint8_t>(g + 0x1822, 1); NoRun(Capture06(true, f.Memory()), Phase::Clear);
    f.Put<int>(0x6c6ea0, 1); f.Put<int>(0x6c6ea4, 1); NoRun(Capture06(true, f.Memory()), Phase::Title);
    f.Put<int>(0x6c6ea4, 2); NoRun(Capture06(true, f.Memory()), Phase::Loading);
}

void Test07()
{
    Fixture f; constexpr uintptr_t g = 0x200000;
    f.Zero(g, 0x100); f.Zero(0x626270, 0x9644);
    f.Put<int>(0x575aa4, 2); f.Put<int>(0x575aa8, 2); f.Put<int>(0x134d9cc, 60);
    f.Put<uint32_t>(0x626278, g); f.Put<uint8_t>(0x62f648, 5); f.Put<int>(0x62f85c, 8); f.Put<int>(0x626280, 5);
    f.Put<int>(0x12fe0c8, 0); f.Put<int>(0x49fc14, 0); f.Put<uint8_t>(0x4bfee5, 1);
    f.Put<float>(g + 0x5c, 4); f.Put<float>(g + 0x68, 3); f.Put<float>(g + 0x7c, 128);
    f.Put<int>(g + 0x88, 1000); f.Put<int>(0x62f888, 51000); f.Put<int>(0x62f88c, 61000); f.Put<int>(0x62f890, 11000);
    auto s = Capture07(f.Memory()); Check(s.stage == "Phantasm" && s.difficulty == "Phantasm" && s.lives == 4 && s.extra.find("60000/50000") != std::string::npos, "TH07 resources/cherry decoding");
    f.Put<uint8_t>(0x62f648, 13); s = Capture07(f.Memory()); Check(s.mode == "リプレイ鑑賞", "TH07 replay precedence");
    f.Put<float>(g + 0x5c, 3.5f); Reject([&] { Capture07(f.Memory()); });
    f.Put<int>(0x575aa4, 1); f.Put<int>(0x575aa8, 1); NoRun(Capture07(f.Memory()), Phase::Title);
}

void Test08()
{
    Fixture f; constexpr uintptr_t g = 0x200000;
    f.Zero(g, 0x100); f.Zero(0x164d0ac, 0x10); f.Put<int>(0x17ce8b0, 2); f.Put<int>(0x17ce8b4, 2);
    f.Put<uint32_t>(0x160f510, g); f.Put<int>(0x160f538, 2); f.Put<int>(0x160f540, 0);
    f.Put<int>(0x164d0b4, 0x4009); f.Put<uint8_t>(0x164d0b1, 0); f.Put<uint8_t>(0x4e4850, 6);
    f.Put<uint32_t>(0x4ea670, 1); f.Put<int>(0x4ea678, 42); f.Put<uint8_t>(0x18b89b8, 1);
    f.Put<float>(g + 0x74, 4); f.Put<float>(g + 0x80, 3); f.Put<float>(g + 0x98, 128); f.Put<int>(g, 321); f.Put<uint8_t>(g + 0x29, 2);
    auto s = Capture08(f.Memory()); Check(s.mode == "リプレイ鑑賞" && s.stage == "Stage 6A" && s.score == 3212 && s.spell == "スペル #43", "TH08 mode/resources");
    f.Put<int>(0x164d0b4, 0x4001); s = Capture08(f.Memory()); Check(s.mode == "スペルプラクティス", "TH08 spell practice precedence");
    f.Put<uint8_t>(0x164d0bb, 1); NoRun(Capture08(f.Memory()), Phase::Results);
    f.Put<int>(0x17ce8b0, 1); f.Put<int>(0x17ce8b4, 1); NoRun(Capture08(f.Memory()), Phase::Title);
}

void Test09()
{
    Fixture f; f.Zero(0x4a7d94, 0x160); f.Put<int>(0x4b368c, 2); f.Put<int>(0x4b3690, 2); f.Put<int>(0x4a7ec4, 12);
    f.Put<int>(0x4a7ea8, 2); f.Put<int>(0x4a7db8, 1); f.Put<int>(0x4a7df0, 0); f.Put<int>(0x4a7db0, 0); f.Put<int>(0x4a7de8, 1);
    f.Put<uint32_t>(0x4a7d94, 0x200000); f.Put<uint32_t>(0x4a7dcc, 0x210000); f.Put<uint32_t>(0x4a7dac, 0x220000); f.Put<uint32_t>(0x4a7de4, 0x230000);
    f.Put<int>(0x2000a8, 8); f.Put<int>(0x2100a8, 6); f.Put<int>(0x230008, 500); f.Put<int>(0x230014, 0);
    f.Put<int>(0x4a7e90, 1); f.Put<int>(0x4a7e98, 0); f.Put<int>(0x4a7e9c, 1); f.Put<int>(0x4a7e60, 3600);
    auto s = Capture09(f.Memory()); Check(s.character.find("魔理沙") != std::string::npos && s.stage == "Round 2" && s.score == 5000 && s.elapsedSeconds == 60 && s.mode == "対戦・リプレイ鑑賞", "TH09 P2/round/score/replay");
    const char* japanese[] = {"霊夢", "魔理沙", "咲夜", "妖夢", "鈴仙", "チルノ", "リリカ", "ミスティア", "てゐ", "幽香", "文", "メディスン", "小町", "映姫", "メルラン", "ルナサ"};
    const char* english[] = {"Reimu", "Marisa", "Sakuya", "Youmu", "Reisen", "Cirno", "Lyrica", "Mystia", "Tewi", "Yuuka", "Aya", "Medicine", "Komachi", "Eiki", "Merlin", "Lunasa"};
    for (int character = 0; character < 16; ++character) {
        f.Put<int>(0x4a7de8, character);
        Gui::__glocale_current = LOCALE_JA_JP;
        Check(Capture09(f.Memory()).character.find(japanese[character]) == 0, "TH09 Japanese character mapping");
        Gui::__glocale_current = LOCALE_EN_US;
        Check(Capture09(f.Memory()).character.find(english[character]) == 0, "TH09 English character mapping");
    }
    Gui::__glocale_current = LOCALE_JA_JP;
    f.Put<int>(0x4a7de8, 1);
    f.Put<uint8_t>(0x4a7ecc, 1); Check(Capture09(f.Memory()).phase == Phase::Paused, "TH09 paused");
    f.Put<uint8_t>(0x4a7ece, 1); NoRun(Capture09(f.Memory()), Phase::Results);
    f.Put<uint8_t>(0x4a7ecd, 1); NoRun(Capture09(f.Memory()), Phase::Unknown);
}

void Modern(Fixture& f, uintptr_t supervisor, uintptr_t mode, uintptr_t game, uintptr_t pause, uintptr_t replay, uintptr_t globals, uintptr_t enemy, int flagsOffset, int replayOffset, int bossOffset)
{
    f.Put<int>(supervisor, 7); f.Put<int>(supervisor + 4, 7); f.Put<int>(mode, 0x10);
    f.Put<uint32_t>(game, 0x200000); f.Put<uint32_t>(pause, 0x210000); f.Put<uint32_t>(replay, 0x220000); f.Put<uint32_t>(enemy, 0x230000);
    f.Put<int>(0x200000 + flagsOffset, 0); f.Put<int>(0x200000 + replayOffset, 1); f.Put<int>(0x210004, 0); f.Put<int>(0x220010, 1); f.Put<uint32_t>(0x230000 + bossOffset, 0);
    f.Zero(globals, 0xa0); f.Put<int>(mode, 0x10);
}

void Test10()
{
    Fixture f; constexpr uintptr_t g = 0x474c40;
    Modern(f, 0x491fb4, 0x474ca0, 0x477810, 0x477830, 0x477838, g, 0x477704, 0x58, 0x5c, 0x10);
    f.Put<int>(g + 0x3c, 7); f.Put<int>(g + 4, 1234); f.Put<int>(g + 8, 100); f.Put<int>(g + 0xc, 5000); f.Put<int>(g + 0x18, 130);
    auto s = Capture10(f.Memory()); Check(s.mode == "リプレイ鑑賞" && s.stage == "Extra" && s.power == 5 && s.score == 12340 && s.extra.find("50000") != std::string::npos, "TH10 resources/replay");
    f.Put<int>(0x210004, 6); NoRun(Capture10(f.Memory()), Phase::Results);
    f.Put<int>(0x210004, 0); f.Put<int>(0x220010, 0); NoRun(Capture10(f.Memory()), Phase::Loading);
    f.Put<int>(0x491fb4, 4); f.Put<int>(0x491fb8, 4); f.Put<uint32_t>(0x47784c, 0x240000); f.Put<int>(0x24001c, 1); NoRun(Capture10(f.Memory()), Phase::Title);
}

void Test11()
{
    Fixture f; constexpr uintptr_t g = 0x4a56e0;
    Modern(f, 0x4c37d4, 0x4a5758, 0x4a8e88, 0x4a8eb0, 0x4a8eb8, g, 0x4a8d7c, 0x60, 0x74, 0x1c);
    f.Put<int>(g + 0x48, 2); f.Put<int>(g + 0x68, 800); f.Put<int>(g + 0x6c, 100); f.Put<int>(g + 8, 400); f.Put<int>(g + 0x3c, 4); f.Put<int>(g + 0x10, 10000); f.Put<int>(g + 0x74, 150);
    auto s = Capture11(f.Memory()); Check(s.mode == "リプレイ鑑賞" && s.power == 4 && s.lifePieces == "4/5" && s.pointItemValue == 100 && s.graze == 150, "TH11 resources");
    f.Put<int>(0x210004, 12); NoRun(Capture11(f.Memory()), Phase::GameOver); f.Put<int>(0x210004, 20); NoRun(Capture11(f.Memory()), Phase::Results);
    f.Put<int>(0x210004, 0); f.Put<int>(g + 0x6c, 0); Reject([&] { Capture11(f.Memory()); });
    f.Put<int>(0x4c37d4, 4); f.Put<int>(0x4c37d8, 4); f.Put<uint32_t>(0x4a8ecc, 0x240000); f.Put<int>(0x24001c, 3); NoRun(Capture11(f.Memory()), Phase::Menu);
}

void Test12()
{
    Fixture f; constexpr uintptr_t g = 0x4b0c40;
    Modern(f, 0x4cee3c, 0x4b0ce0, 0x4b44e8, 0x4b4510, 0x4b4518, g, 0x4b43dc, 0x60, 0x74, 0x1c);
    f.Put<int>(g + 0x70, 3); f.Put<int>(g + 0x94, 100); f.Put<int>(g + 8, 400); f.Put<int>(g + 0x5c, 4); f.Put<int>(g + 0x64, 4); f.Put<int>(g + 0x38, 1234567);
    f.Put<int>(g + 0x18, 2); f.Put<int>(g + 0xc, 1); f.Put<int>(g + 0x10, 3); f.Put<uint32_t>(0x4b4534, 0x250000); f.Put<int>(0x25002c, 1); f.Put<int>(0x250038, 1); f.Put<int>(0x250030, -1);
    f.Put<uint32_t>(0x4b43cc, 0x260000); f.Put<uint32_t>(0x26007c, 1);
    auto s = Capture12(f.Memory()); Check(s.lifePieces == "3/4" && s.bombPieces == "2/3" && s.pointItemValue == 12340 && s.extra.find("赤→緑") != std::string::npos && s.extra.find("UFO 虹") != std::string::npos && s.spell == "スペル発動中", "TH12 fragments/UFO/PIV/spell");
    f.Put<int>(0x250030, 0); Reject([&] { Capture12(f.Memory()); });
    f.Put<int>(0x210004, 20); NoRun(Capture12(f.Memory()), Phase::Results);
    f.Put<int>(0x4cee3c, 4); f.Put<int>(0x4cee40, 4); f.Put<uint32_t>(0x4b4530, 0x240000); f.Put<int>(0x24001c, 1); NoRun(Capture12(f.Memory()), Phase::Title);
}

void Test128()
{
    Fixture f; f.Put<uint32_t>(0x4b8a9c, 0); f.Put<uint32_t>(0x4b8944, 0); f.Put<uint32_t>(0x4b8954, 0x200000); f.Put<uint32_t>(0x4b8a80, 0x210000); f.Put<uint32_t>(0x4b8a7c, 0x220000); f.Put<int>(0x220004, 0);
    f.Zero(0x4b4cc0, 0xc0); f.Put<int>(0x4b4d14, 15); f.Put<int>(0x4d2e30, 2); f.Put<int>(0x4b4d50, 1);
    f.Put<int>(0x4b4d64, 10000); f.Put<int>(0x4b4d70, 30000); f.Put<int>(0x4b4d6c, 5000); f.Put<float>(0x4b4d60, 3000);
    auto s = Capture128(f.Memory()); Check(s.mode == "リプレイ鑑賞" && s.stage == "Stage C2-3" && s.extra.find("100%") != std::string::npos && s.extra.find("50%") != std::string::npos && s.extra.find("3000%") != std::string::npos, "TH128 resources/route/replay");
    f.Put<int>(0x4b4d14, 16); f.Put<int>(0x4b4d0c, 4); s = Capture128(f.Memory()); Check(s.stage == "Extra" && s.difficulty == "Extra", "TH128 Extra route");
    f.Put<int>(0x220004, 1); NoRun(Capture128(f.Memory()), Phase::Menu);
    f.Put<uint32_t>(0x4b8a9c, 0x230000); f.Put<int>(0x23001c, 1); NoRun(Capture128(f.Memory()), Phase::Title);
}

void TestNewClassic()
{
    // Field types match the production structs; those structs separately assert
    // their versioned offsets. These fixtures exercise the shared decoder.
    struct Game {
        uint8_t character = 0, subShot = 0, pauseState = 0, retryState = 0, isClear = 0, mode = 1;
        uint32_t stage = 1, difficulty = 0;
        uint16_t curPower = 128, totalPointItems = 100, stagePointItems = 20;
        int8_t inReplay = 0, inPracticeMode = 1, inSpellPrac = 0, spellPracEndFlag = 0, livesRemaining = 3, bombsRemaining = 2;
        int64_t actualScore = 1234567890;
        int32_t totalGraze = 200, stageGraze = 50, stageTime = 600;
    } game;
    struct Supervisor { uint32_t curState = 2, wantedState = 2; } supervisor;
    const auto capture = [&] { return CaptureNewClassic(game, supervisor, "New Classic", "霊夢", 0, 0, false); };
    auto s = capture(); Check(s.mode == "プラクティス" && s.score == 1234567890 && s.stageGraze == 50 && s.elapsedSeconds == 10, "NC live resources/practice");
    game.inSpellPrac = 1; s = capture(); Check(s.mode == "スペルプラクティス", "NC spell practice precedence");
    game.inReplay = 1; game.pauseState = 2; s = capture(); Check(s.mode == "リプレイ鑑賞" && s.phase == Phase::Paused, "NC replay/pause precedence");
    game.isClear = 1; NoRun(capture(), Phase::Clear);
    game.retryState = 1; NoRun(capture(), Phase::GameOver);
    game.isClear = 0; game.retryState = 0; game.spellPracEndFlag = 1; NoRun(capture(), Phase::Results);
    game.spellPracEndFlag = 0; game.stage = 7; game.difficulty = 4; s = capture(); Check(s.stage == "Extra" && s.difficulty == "Extra", "NC Extra");
    game.stage = 8; NoRun(capture(), Phase::Unknown);
    supervisor.curState = 1; supervisor.wantedState = 1; NoRun(capture(), Phase::Title);
    supervisor.curState = 2; NoRun(capture(), Phase::Loading);
}
}

int main()
{
    try {
        Test06(); Test07(); Test08(); Test09(); Test10(); Test11(); Test12(); Test128(); TestNewClassic();
        Fixture missing; Reject([&] { Capture06(false, missing.Memory()); });
        std::cout << "PASS early capture fixtures: TH06, TH06NC, TH07, TH08, TH09, TH10, TH11, TH12, TH128 (synthetic memory only)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
