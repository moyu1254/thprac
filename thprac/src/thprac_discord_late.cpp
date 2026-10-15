#include "thprac_discord_late.h"
#include "thprac_gui_locale.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <unordered_set>

// Port of TouhouStatus d45f0729: LateGames.cs, Original13And14.cs,
// Th17Attacks.cs, Th18Cards.cs, and Th19.cs. Address tables are RVAs.
// Captures run on the game thread: no worker thread ever dereferences game
// objects. An invalid pointer, enum, or list aborts the entire capture.
namespace DiscordPresence {
namespace {
#ifdef THPRAC_PRESENCE_TESTS
    LateMemoryReader testReader = nullptr;
    uintptr_t At(uintptr_t rva) { return 0x400000 + rva; }
#else
    uintptr_t At(uintptr_t rva) { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + rva; }
#endif

    template<typename T> T Read(uintptr_t address)
    {
        T result{};
#ifdef THPRAC_PRESENCE_TESTS
        if (!testReader || !testReader(address, &result, sizeof(result)))
            throw std::runtime_error("Unreadable test snapshot");
#else
        SIZE_T read = 0;
        if (address < 0x10000 || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &result, sizeof(result), &read) || read != sizeof(result))
            throw std::runtime_error("Unreadable Discord game snapshot");
#endif
        return result;
    }
    int I(uintptr_t rva) { return Read<int32_t>(At(rva)); }
    uintptr_t P(uintptr_t rva) { return Read<uint32_t>(At(rva)); }
    uintptr_t Required(uintptr_t rva)
    {
        const auto value = P(rva);
        if (value < 0x10000) throw std::runtime_error("Missing live game object");
        return value;
    }
    int Range(int value, int low, int high)
    {
        if (value < low || value > high) throw std::runtime_error("Invalid Discord game field");
        return value;
    }
    int Nonnegative(uintptr_t rva, int high = INT_MAX) { return Range(I(rva), 0, high); }
    int64_t Score(uintptr_t rva, bool wide = false)
    {
        const int64_t value = wide ? Read<int64_t>(At(rva)) : Read<uint32_t>(At(rva));
        if (value < 0 || value > 999999999999LL) throw std::runtime_error("Invalid game score");
        return value * 10;
    }
    template<size_t N> std::string Choice(int index, const char* const (&choices)[N])
    {
        return choices[Range(index, 0, static_cast<int>(N) - 1)];
    }
    std::string ListChoice(th_glossary_t glossary, int index, int count)
    {
        Range(index, 0, count - 1);
        auto text = S(glossary);
        for (int i = 0; i < index; ++i) {
            if (!*text) throw std::runtime_error("Invalid localized character list");
            text += strlen(text) + 1;
        }
        if (!*text) throw std::runtime_error("Missing localized character");
        return text;
    }
    bool PauseSubstate(int value) { return (value >= 6 && value <= 12) || value == 14 || value == 16; }
    bool Active(Phase phase) { return phase == Phase::Playing || phase == Phase::Paused; }
    template<typename... T> std::string Text(const T&... fields)
    {
        std::ostringstream output;
        (output << ... << fields);
        return output.str();
    }
    std::string Fixed(double value, int precision)
    {
        std::ostringstream output;
        output << std::fixed << std::setprecision(precision) << value;
        return output.str();
    }
    std::string Fragment(uintptr_t rva, int maximum) { return Text(Nonnegative(rva, maximum), "/", maximum); }
    std::string Stage(int value) { return S(TH_STAGE_SELECT[Range(value, 1, 7) - 1]); }
    const char* const difficulties[] = { "Easy", "Normal", "Hard", "Lunatic", "Extra", "Overdrive" };

    struct Layout {
        int game;
        th_glossary_t title;
        uintptr_t screen, activity, thread;
        int frameOffset, flagsOffset;
        uintptr_t transition, mode, stage, difficulty, character, equipment;
        uintptr_t score, lives, bombs, power, spell;
        int spellActive, spellId;
        uintptr_t enemy;
        int enemyList, enemyFlags;
        uint32_t bossMask;
        uintptr_t gui;
        int dialogue;
    };
    constexpr Layout layouts[] = {
        {13, TH13_TITLE, 0xdcc20,0xdf120,0xc2194,0x14,0x60,0xc2168,0xdc670,0xbe81c,0xbe7c4,0xbe7b8,0,0xbe7c0,0xbe7f4,0xbe800,0xbe7e8,0xc2178,0x20,0x78,0xc2188,0xb0,0x521c,0x1000000,0xc2190,0x198},
        {14, TH14_TITLE, 0xd9648,0xf7ac8,0xdb558,0x14,0x80,0xdb524,0xdb6a0,0xf58a4,0xf5834,0xf5828,0xf582c,0xf5830,0xf5864,0xf5870,0xf5858,0xdb534,0x20,0x78,0xdb544,0xd0,0x5244,0x800000,0xdb550,0x194},
        {15, TH15_TITLE, 0xe7ec8,0x11bc60,0xe9a94,0x10,0x90,0xe9a5c,0xe9bdc,0xe73f0,0xe7410,0xe7404,0,0xe740c,0xe7450,0xe745c,0xe7440,0xe9a70,0x1c,0x74,0xe9a80,0x180,0x526c,0x800000,0xe9a8c,0x1b8},
        {16, TH16_TITLE, 0xc17c0,0xd9d90,0xa6dd4,0x10,0x88,0xa6d9c,0xa6f1c,0xa5790,0xa57b4,0xa57a4,0xa57ac,0xa57b0,0xa57f4,0xa5800,0xa57e4,0xa6db0,0x1c,0x74,0xa6dc0,0x180,0x526c,0x800000,0xa6dcc,0x1c8},
        {17, TH17_TITLE, 0xb61d0,0x124778,0xb76b0,0x10,0x8c,0xb767c,0xb77ec,0xb59dc,0xb5a00,0xb59f4,0xb59f8,0xb59fc,0xb5a40,0xb5a4c,0xb5a30,0xb7690,0x1c,0x74,0xb76a0,0x180,0x528c,0x800000,0xb76ac,0x1c0},
        {18, TH18_TITLE, 0xcd5e4,0x16ad00,0xcf2e4,0x10,0xb0,0xcf2b0,0xcf438,0xcccdc,0xccd00,0xcccf4,0,0xcccfc,0xccd48,0xccd58,0xccd38,0xcf2c0,0x1c,0x74,0xcf2d0,0x18c,0x635c,0x800000,0xcf2e0,0x1b0},
    };

    std::string ReadCp932Name(uintptr_t address)
    {
        const auto text = Read<std::array<char,64>>(address);
        const auto end = std::find(text.begin(), text.end(), '\0');
        if (end == text.end() || end == text.begin()) throw std::runtime_error("Invalid spell name");
        for (auto i = text.begin(); i != end; ++i)
            if (static_cast<unsigned char>(*i) < 0x20) throw std::runtime_error("Invalid spell character");
        const int bytes = static_cast<int>(end - text.begin());
        wchar_t wide[64];
        const int length = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, text.data(), bytes, wide, 64);
        if (!length) throw std::runtime_error("Invalid spell encoding");
        char utf8[256];
        const int encoded = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, length, utf8, sizeof(utf8), nullptr, nullptr);
        if (!encoded) throw std::runtime_error("Invalid spell encoding");
        return {utf8, static_cast<size_t>(encoded)};
    }
    std::string ReadSpellName(uintptr_t card, int maximum)
    {
        if (!(Read<uint32_t>(card + 0x78) & 1)) return {};
        Range(Read<int>(card + 0x74), 0, maximum);
        return ReadCp932Name(card + 0x34);
    }

    std::string BossSegment(const Layout& l, bool normal)
    {
        const auto manager = Required(l.enemy);
        const int bossId = l.game == 17 ? Range(Read<int>(manager + 0x48), 0, INT_MAX) : 0;
        auto node = Read<uint32_t>(manager + l.enemyList);
        std::unordered_set<uintptr_t> visited;
        bool boss = false;
        uintptr_t selected = 0;
        while (node) {
            if (node < 0x10000 || !visited.insert(node).second || visited.size() > 2048) throw std::runtime_error("Invalid enemy list");
            const auto enemy = Read<uint32_t>(node);
            if (enemy && (Read<uint32_t>(enemy + l.enemyFlags) & l.bossMask)) {
                boss = true;
                if (l.game == 17 && normal && bossId && Read<int>(enemy + 0x5760) == bossId) {
                    if (selected) throw std::runtime_error("Duplicate boss ID");
                    selected = enemy;
                }
            }
            node = Read<uint32_t>(node + 4);
        }
        if (selected) {
            const int sub = Read<int>(selected + 0x14);
            if (sub >= 0) {
                const auto file = Read<uint32_t>(manager + 0x17c);
                const int count = Range(Read<int>(file + 8), 1, 4096);
                if (sub < count) {
                    const auto table = Read<uint32_t>(file + 0x8c);
                    const auto name = Read<std::array<char,64>>(Read<uint32_t>(table + sub * 8));
                    const auto end = std::find(name.begin(), name.end(), '\0');
                    if (end == name.end()) throw std::runtime_error("Invalid ECL name");
                    const std::string text(name.begin(), end);
                    // Exact primary sub names only: Boss1_at and other helpers
                    // are not evidence of the current attack.
                    if (text == "MBoss1") return "中ボス戦 | " + std::string(S(TH_NONSPELL)) + "1";
                    if (text.size() == 5 && text.starts_with("Boss") && text[4] >= '1' && text[4] <= '8')
                        return std::string(S(TH_BOSS)) + " | " + S(TH_NONSPELL) + text.substr(4);
                }
            }
        }
        return boss ? S(TH_BOSS) : "道中";
    }

    std::string Cards18()
    {
        const auto manager = Required(0xcf298);
        auto node = Read<uint32_t>(manager + 0x1c);
        const int count = Range(Read<int>(manager + 0x28), 0, 128);
        const int activeCount = Range(Read<int>(manager + 0x2c), 0, count);
        const auto selected = Read<uint32_t>(manager + 0x38);
        std::unordered_set<uintptr_t> nodes, cards;
        std::string names, selectedName;
        int active = 0;
        while (node) {
            if (node < 0x10000 || !nodes.insert(node).second || nodes.size() > static_cast<size_t>(count)) throw std::runtime_error("Invalid card list");
            const auto card = Read<uint32_t>(node);
            if (card < 0x10000 || !cards.insert(card).second) throw std::runtime_error("Duplicate card pointer");
            const int id = Range(Read<int>(card + 4), 0, 55);
            const auto flags = Read<uint32_t>(card + 0x50);
            const std::string name = S(TH18::TH18_CARD_LIST[id]);
            if (!names.empty()) names += "・";
            names += name;
            if (flags & 8) {
                ++active;
                const int maximum = Range(Read<int>(card + 0x48), 1, 360000);
                const int remaining = Range(Read<int>(card + 0x38), 0, maximum);
                if (card == selected) selectedName = name + (flags & 0x20 ? " 使用中" : remaining == 0 ? " 使用可" : Text(" 残", Fixed(remaining / 60.0, 1), "秒"));
            } else if (card == selected) throw std::runtime_error("Selected card is not active");
            node = Read<uint32_t>(node + 4);
        }
        if (cards.size() != static_cast<size_t>(count) || active != activeCount || (selected && selectedName.empty())) throw std::runtime_error("Card counts disagree");
        return (selectedName.empty() ? "" : "選択 " + selectedName + "\n") + "装備 " + (names.empty() ? S(TH_NONE) : names);
    }

    void Extras(const Layout& l, Snapshot& s, bool pointdevice, int difficulty)
    {
        uintptr_t graze = 0, piv = 0, life = 0, bomb = 0;
        int lifeMax = 3, bombMax = 3;
        switch (l.game) {
        case 13: {
            graze=0xbe7d0; piv=0xbe7dc; life=0xbe7f8; bomb=0xbe804; bombMax=8;
            constexpr int thresholds[] = {8,10,12,15,18,20,25};
            lifeMax = thresholds[Nonnegative(0xbe7fc,6)];
            s.extra = Text(S(TH13_TRANCE_METER), " ", Fixed(Nonnegative(0xbe808,600)/200.0, 2), "/3 / ", Nonnegative(0xbe80c) ? "霊界中" : "霊界外");
            break;
        }
        case 14: graze=0xf5840; piv=0xf584c; life=0xf5868; bomb=0xf5874; bombMax=8; break;
        case 15:
            graze=0xe741c; piv=0xe7434; life=0xe7454; bomb=0xe7460; lifeMax=difficulty == 4 ? 5 : 3; bombMax=5;
            if (pointdevice) s.extra=Text("チャプター ",Nonnegative(0xe73f8,100)," / 総リトライ ",Nonnegative(0xe7594)," / 現在チャプターリトライ ",Nonnegative(0xe75b8));
            break;
        case 16: {
            graze=0xa57c0; piv=0xa57d8; bomb=0xa5804; bombMax=5;
            const int power=Nonnegative(0xa5808,100000);
            int level=0, previous=0, next=0;
            for (int i=0;i<6;++i) {
                const int threshold=Nonnegative(0xa583c+i*4,100000);
                if (threshold<=previous) throw std::runtime_error("Invalid season thresholds");
                if (power>=threshold) ++level;
                else if (!next) next=threshold;
                previous=threshold;
            }
            if (power>previous) throw std::runtime_error("Invalid season gauge");
            const bool released=Range(Read<int>(Required(0xa6da4)+0x30),0,1)==1;
            s.extra=Text("季節Lv ",level,"/6 / ",S(TH16_SEASON_GAUGE_ALT)," ",power,"/",next?next:previous," / 季節解放",released?"中":"なし");
            break;
        }
        case 17: {
            piv=0xb5a24; life=0xb5a44; bomb=0xb5a50;
            const auto flags=Read<uint8_t>(At(0xb5ac4));
            s.extra="ロアリングなし";
            if (flags&2) {
                const char* const types[]={"", "オオカミ", "カワウソ", "オオワシ", "通常"};
                s.extra=Text("ロアリング ",Choice(Range(I(0xb5abc),1,4),types)," 残",Fixed(Nonnegative(0xb5aa8)/60.0,1),"秒",flags&4?"（解除中）":"");
            }
            s.extra += " / " + std::string(S(TH17_GOAST)) + " ";
            for (int i=0;i<5;++i) {
                if(i) s.extra += "・";
                s.extra += S(TH17::TH17_GOAST_SELECT[Nonnegative(0xb5a64+i*4,14)]);
            }
            break;
        }
        case 18:
            life=0xccd4c; bomb=0xccd5c;
            s.extra=Text(S(TH18_FUNDS)," ",Nonnegative(0xccd34)," / カード ",Range(Read<int>(Required(0xcf298)+0x28),0,128)," 枚");
            s.equipment=Cards18();
            break;
        }
        if (graze) s.graze=Read<uint32_t>(At(graze));
        if (piv) { const auto value=Read<uint32_t>(At(piv)); s.pointItemValue=l.game==15?value/1000*10:value/100; }
        if (life && !pointdevice) s.lifePieces=Fragment(life,lifeMax);
        if (bomb) s.bombPieces=Fragment(bomb,bombMax);
    }

    Snapshot CaptureModern(const Layout& l, int practiceMode)
    {
        Snapshot s;
        s.title=S(l.title);
        const int screen=I(l.screen), next=I(l.screen+4);
        if(screen!=next || screen==1) { s.phase=Phase::Loading; return s; }
        if(screen==15 || (l.game==16 && screen==16)) { s.phase=Phase::Ending; return s; }
        if(screen==4) {
            s.phase=Phase::Title;
            if(l.game<=14) {
                const auto menu=P(l.game==13?0xc22e0:0xdb6a4);
                const int state=menu?Read<int>(menu+0x1c):0;
                s.phase=state==1?Phase::Title:state>=3&&state<=19?Phase::Menu:Phase::Loading;
            }
            return s;
        }
        if(screen!=7) return s;
        const uintptr_t demoAddresses[]={0xbe830,0xf58b8,0xe7794,0xa5bec,0xb59c8,0xcccc8};
        if(I(demoAddresses[l.game-13])&0x40) { s.phase=Phase::Title; s.mode="デモ"; return s; }
        const auto thread=P(l.thread);
        if(!thread || P(l.transition)) { s.phase=Phase::Loading; return s; }
        const uintptr_t pauses[]={0xc22c0,0xdb678,0xe9bb4,0xa6ef4,0xb77cc,0xcf40c};
        const auto pause=Required(pauses[l.game-13]);
        const int flags=Read<int>(thread+l.flagsOffset), activity=I(l.activity), mode=I(l.mode);
        const bool original=l.game<=14;
        const int state=Read<int>(pause+(original?4:0x1ec));
        const int completed=Read<int>(pause+(original?0x1fc:0x204));
        const int prior=Read<int>(pause+(original?0x200:0x208));
        const int substate=original?0:Read<int>(pause+0x1f4);
        const int stopped=0x4000|0xf4|(l.game==17?0x10000:l.game==18?0x30000:0);
        if(original) {
            if(flags&0x84) s.phase=Phase::Loading;
            else if(state<0 || state>31 || completed<0 || completed>1) return s;
            else if(state>=22 || state==2 || completed==1) s.phase=Phase::Results;
            else if(state>=13) s.phase=Phase::GameOver;
            else if(mode==3) s.phase=Phase::Ending;
            else if(state==0 && activity==2 && !(flags&stopped)) s.phase=Phase::Playing;
            else if((state==1 || (state>=3 && state<=12)) && activity==0 && prior==2 && (flags&stopped)==0x10) s.phase=Phase::Paused;
        } else {
            if(mode==3) s.phase=Phase::Ending;
            else if(l.game==18 && P(0xcf2a4)) { s.phase=Phase::Menu; s.mode="カードショップ"; return s; }
            else if(l.game==16 && activity==1 && (state==2 || state==3)) s.phase=state==2?Phase::GameOver:Phase::Results;
            else if(l.game==16 && mode==2 && state==1 && activity==0 && (flags&(stopped&~0x4000))==0x10 && prior==2 && (substate==1 || (completed==1&&PauseSubstate(substate)))) s.phase=Phase::Results;
            else if(state==0 && substate==0 && activity==2 && !(flags&stopped)) s.phase=Phase::Playing;
            else if(state==1 && activity==0 && (flags&stopped)==0x10 && completed==0 && prior==2 && PauseSubstate(substate)) s.phase=Phase::Paused;
        }
        if(!Active(s.phase)) return s;
        switch(mode) {
        case 1: s.mode="通常プレイ"; break;
        case 2: s.mode="リプレイ鑑賞"; break;
        case 4: s.mode="プラクティス"; break;
        case 5: s.mode=S(TH_SPELL_PRAC); break;
        default: throw std::runtime_error("Invalid game mode");
        }
        const int difficulty=Range(I(l.difficulty),0,l.game==13?5:4);
        if (practiceMode == 1 && mode != 2) s.mode="プラクティス";
        const bool pointdevice=l.game==15 && Range(I(0xe9bec),0,1)==0 && difficulty!=4 && mode==1 && practiceMode!=1;
        if(l.game==15) s.mode+=pointdevice?" / 完全無欠":" / レガシー";
        s.difficulty=Choice(difficulty,difficulties);
        const int character=I(l.character);
        switch(l.game) {
        case 13: s.character=ListChoice(THPRAC_GAMEROLL_TH13_SHOTTYPES,character,4); break;
        case 14: s.character=ListChoice(THPRAC_GAMEROLL_TH14_SHOTTYPES,Range(character,0,2)*2+Nonnegative(l.equipment,1),6); break;
        case 15: s.character=ListChoice(THPRAC_GAMEROLL_TH15_SHOTTYPES,character,4); break;
        case 16: {
            const th_glossary_t chars[]={TH_TRACKER_REIMU,TH_TRACKER_CIRNO,TH_TRACKER_AYA,TH_TRACKER_MARISA};
            s.character=S(chars[Range(character,0,3)]);
            s.equipment=S(TH16::TH16_SUBSEASON_ALL[Nonnegative(l.equipment,4)]);
            break;
        }
        case 17: s.character=ListChoice(THPRAC_GAMEROLL_TH17_SHOTTYPES,Range(character,0,2)*3+Nonnegative(l.equipment,2),9); break;
        case 18: s.character=ListChoice(THPRAC_GAMEROLL_TH18_SHOTTYPES,character,4); break;
        }
        s.stage=Stage(I(l.stage));
        s.score=Score(l.score);
        if(!pointdevice) s.lives=Range(I(l.lives),-1,99);
        s.bombs=Nonnegative(l.bombs,99);
        s.power=Nonnegative(l.power,10000)/100.0;
        s.elapsedSeconds=Range(Read<int>(thread+l.frameOffset),0,INT_MAX)/60.0;
        s.elapsedLabel="面経過";
        const auto spell=Required(l.spell);
        if(l.game==16 || l.game==17) s.spell=ReadSpellName(spell,l.game==17?100:2047);
        else if(Read<int>(spell+l.spellActive)) s.spell=Text(S(TH_SPELL)," ID ",Range(Read<int>(spell+l.spellId),0,2047));
        s.segment=BossSegment(l,s.spell.empty());
        if(Read<int>(Required(l.gui)+l.dialogue)) s.segment="会話中";
        else if(!s.spell.empty()) s.segment=S(TH_SPELL);
        Extras(l,s,pointdevice,difficulty);
        return s;
    }

    Snapshot Capture19(bool newer)
    {
        Snapshot s; s.title=S(TH19_TITLE);
        const auto screenRva=newer?0x22c44c:0x208c8c;
        const int screen=I(screenRva);
        if(screen!=I(screenRva+4) || screen==1) {s.phase=Phase::Loading;return s;}
        if(screen==4) {s.phase=Phase::Title;return s;}
        if(screen==15 || screen==16) {s.phase=Phase::Ending;return s;}
        if(screen!=7) return s;
        const uintptr_t globals=newer?0x22b0c0:0x207910;
        if(I(globals-0x14)&0x80) {s.phase=Phase::Title;s.mode="デモ";return s;}
        const auto thread=P(newer?0x1d1a54:0x1ae464);
        if(!thread || P(newer?0x1d1a38:0x1ae448)) {s.phase=Phase::Loading;return s;}
        const auto pause=Required(newer?0x1d1bf8:0x1ae608);
        const int state=Read<int>(pause+0x1f4),sub=Read<int>(pause+0x1fc),completed=Read<int>(pause+0x20c),prior=Read<int>(pause+0x210);
        const int activity=I(newer?0x22eef0:0x20b210),flags=Read<int>(thread+0x140);
        if(state==2 || state==3 || (state==1&&completed==1) || (flags&0x80000)) s.phase=Phase::Results;
        else if(flags&0x80) s.phase=Phase::Ending;
        else if(state==0 && sub==0 && activity==2 && !(flags&(0xf4|0x80000))) s.phase=Phase::Playing;
        else if(state==1 && activity==0 && (flags&(0xf4|0x80000))==0x10 && completed==0 && prior==2 && PauseSubstate(sub)) s.phase=Phase::Paused;
        if(!Active(s.phase)) return s;
        const int side=newer?0xc8:0xc0,life=newer?0x54:0x50,bomb=newer?0x68:0x64,gauge=newer?0x24:0x20,wins=newer?0xbc:0xb8;
        const int kind=newer?Range(I(globals+0x194),0,3):-1;
        const int replay=Range(Read<int>(thread+0x160),0,1);
        s.mode=replay?"リプレイ鑑賞":kind==0?"ストーリー":"対戦";
        s.difficulty=Choice(Nonnegative(globals+side*2,3),difficulties);
        // Reuse thprac's names, fixing the existing JP list's Sanae typo via
        // the TH18 list entry, whose ID is reviewed in both implementations.
        auto name=[&](int index) { return index==2?ListChoice(THPRAC_GAMEROLL_TH18_SHOTTYPES,3,4):ListChoice(THPRAC_GAMEROLL_TH19_SHOTTYPES,index,19); };
        const auto ai1=P(newer?0x1d1a94:0x1ae4a4),ai2=P(newer?0x1d1ad0:0x1ae4e0);
        if((ai1&&ai1<0x10000)||(ai2&&ai2<0x10000)) throw std::runtime_error("Invalid CPU object");
        s.character=Text("P1 ",name(Nonnegative(globals+0xc,18)),"（",ai1?"CPU":"人間","）");
        s.opponent=Text("P2 ",name(Nonnegative(globals+side+0xc,18)),"（",ai2?"CPU":"人間","）");
        s.score=Score(globals,true);s.opponentScore=Score(globals+side,true);
        s.lives=Nonnegative(globals+life,8);s.opponentLives=Nonnegative(globals+side+life,8);
        s.bombs=Nonnegative(globals+bomb,8);s.opponentBombs=Nonnegative(globals+side+bomb,8);
        const uintptr_t stageRva=newer?0x22ba80:0x2082c0;
        if(kind!=2) { const int stage=Nonnegative(stageRva,99);if(stage) s.stage=Text("Stage ",stage); }
        s.elapsedLabel="面経過";s.elapsedSeconds=Range(Read<int>(thread+0x14),0,INT_MAX)/60.0;
        if(kind==2) {
            const int limit=Nonnegative(stageRva+0x9c,0x57030),remaining=Nonnegative(stageRva+0xa0,limit);
            if(limit) {s.elapsedLabel="対戦残り";s.elapsedSeconds=std::ceil(remaining/60.0);}
            s.extra=Text("取得ラウンド 1P ",Nonnegative(globals+wins,4)," / 2P ",Nonnegative(globals+side+wins,4),"\n");
        }
        s.extra+=Text("1P ゲージ ",Nonnegative(globals+gauge,2500),"/2500 / 2P ゲージ ",Nonnegative(globals+side+gauge,2500),"/2500");
        s.segment="対戦中";
        return s;
    }

    Snapshot CaptureAlcostg(int practiceMode)
    {
        enum addrs : uintptr_t {
            CURRENT_SCREEN = 0x8e9e4,
            NEXT_SCREEN = 0x8e9e8,
            GAME_THREAD = 0x74170,
            PAUSE_MENU = 0x74190,
            PLAYER = 0x74194,
            REPLAY = 0x74198,
            SPELL_CARD = 0x74054,
            ENEMY_MANAGER = 0x74064,
            GLOBALS = 0x8e580,
            STAGE_TABLE = 0x70738,
        };
        // Official web build (PE timestamp 1198873702, .text size 397312).
        // Super::on_tick 421850; GameThread::on_tick 418c60;
        // PauseMenu 422310/422a10; SpellCard 4097c0/409e70.
        Snapshot s;s.title=S(ALCOSTG_TITLE);
        const int screen=I(CURRENT_SCREEN);
        if(screen!=I(NEXT_SCREEN)) {s.phase=Phase::Loading;return s;}
        if(screen==4) {s.phase=Phase::Menu;return s;}
        if(screen==14) {s.phase=Phase::Ending;return s;}
        if(screen!=7) return s;
        const auto thread=P(GAME_THREAD);
        if(!thread||!P(PLAYER)) {s.phase=Phase::Loading;return s;}
        constexpr uintptr_t g=GLOBALS;
        if(I(g+0x90)&0x20) {s.phase=Phase::Title;s.mode="デモ";return s;}
        const auto pause=Required(PAUSE_MENU);
        const int state=Range(Read<int>(pause+4),0,13),flags=Read<int>(thread+0x58);
        if(flags&0x84) {s.phase=Phase::Loading;return s;}
        if(state>=6) {s.phase=Phase::Results;return s;}
        if(state==0&&!(flags&0x70)) s.phase=Phase::Playing;
        else if(state>=1&&state<=5&&(flags&0x70)==0x10) s.phase=Phase::Paused;
        if(!Active(s.phase)) return s;
        const bool watching=Range(Read<int>(Required(REPLAY)+0x10),0,1)==1;
        s.mode=watching?"リプレイ鑑賞":S(practiceMode==1?ALCOSTG_PRACTICE_MODE:ALCOSTG_NORMAL_PLAY);
        // The live value is a stage-table index (7/8/9), not the displayed
        // stage number. 430ce0 selects the table row by its stage ID.
        const int index=Nonnegative(g+0x34,10);
        const int stage=Range(I(STAGE_TABLE+index*0x2c),1,3);
        s.stage=S(Alcostg::ALCOSTG_STAGE_SELECT[stage-1]);
        s.score=Read<uint32_t>(At(g+4));
        s.character="浅間 伊佐美";
        const int beer=Range(Read<int16_t>(At(g+0x88)),0,SHRT_MAX);
        const int maximum=Range(Read<int16_t>(At(g+0x8a)),beer,SHRT_MAX);
        // Match the HUD clock calculation at 416c02. The raw timer advances
        // at a different rate in each stage and also advances on a miss.
        const int start=Nonnegative(g+0x64,86400),end=Range(I(g+0x68),start,86400);
        const int duration=Range(I(g+0x7c),1,INT_MAX),timer=Nonnegative(g+0x54);
        const int64_t seconds=start+static_cast<int64_t>(end-start)*timer/duration;
        if(seconds>86400) throw std::runtime_error("Invalid Alcostg clock");
        std::ostringstream clock;
        clock<<std::setfill('0')<<std::setw(2)<<seconds/3600<<':'<<std::setw(2)<<seconds/60%60;
        s.extra=Text(S(ALCOSTG_TIME)," ",clock.str()," / ",S(ALCOSTG_BEER)," ",Fixed(beer/1000.0,1),"/",Fixed(maximum/1000.0,1),"L");
        s.elapsedLabel="面経過";s.elapsedSeconds=Range(Read<int>(thread+0x14),0,INT_MAX)/60.0;
        const auto card=Required(SPELL_CARD);
        if(Read<uint32_t>(card+0x378c)&1) {
            s.spell=ReadCp932Name(card+0x3748);
            s.segment=S(TH_SPELL);
        } else if(Read<uint32_t>(Required(ENEMY_MANAGER)+0x1c)) s.segment=S(TH_BOSS);
        else s.segment=S(TH_STAGE);
        return s;
    }

    Snapshot Capture185(int practiceMode)
    {
        enum addrs : uintptr_t {
            GAME_THREAD = 0xd7b0c,
            BLACK_MARKET = 0xd7ac4,
            PERMANENT_SHOP = 0xd7ac8,
            STAGE = 0xd7ad8,
            PLAYER = 0xd7c3c,
            MARKET_NUMBER = 0xd1004,
            DIFFICULTY = 0xd1038,
            LIVES = 0xd10bc,
            POWER = 0xd1078,
            WAVE = 0xd1024,
            BULLET_MONEY = 0xd1070,
            FUNDS = 0xd106c,
        };
        // thprac_th185.cpp and ArchipelagoTouhou ee460665:
        // worlds/th185/variables/address_gameplay.py. Unlike the selected
        // market in MainMenu, d1004 belongs to the live stage. The last boss
        // ID is deliberately not used: it survives later normal waves.
        Snapshot s;s.title=S(TH185_TITLE);
        const auto thread=P(GAME_THREAD);
        if(!thread) {s.phase=Phase::Menu;return s;}
        if(P(BLACK_MARKET)||P(PERMANENT_SHOP)) {s.phase=Phase::Menu;s.mode="闇市場";return s;}
        if(!P(STAGE)||!P(PLAYER)) {s.phase=Phase::Loading;return s;}
        const auto flags=Read<uint32_t>(thread+0xb0);
        // 0x10 is the pause bit used by thprac's wave-selection guard.
        // Until another flag is reviewed, do not label it normal gameplay.
        if(flags&~0x10u) return s;
        s.phase=flags&0x10?Phase::Paused:Phase::Playing;
        s.mode=practiceMode==1?"プラクティス":"通常プレイ";
        const char* const markets[]={"Tutorial","1st Market","2nd Market","3rd Market","4th Market","5th Market","6th Market","End of Market","Challenge Market"};
        const char* const ranks[]={"Very Easy","Easy","Normal","Little Hard","Hard","Very Hard","Lunatic","Over Drive"};
        s.stage=Choice(I(MARKET_NUMBER),markets);s.difficulty=Choice(I(DIFFICULTY),ranks);
        s.character=S(TH_TRACKER_MARISA);
        s.lives=Nonnegative(LIVES,99);s.power=1+Nonnegative(POWER,900)/100.0;
        const int wave=Nonnegative(WAVE,99);
        if(wave) s.segment=Text("Wave ",wave);
        s.extra=Text(S(TH185_BULLET_MONEY)," ",Nonnegative(BULLET_MONEY)," / ",S(TH18_FUNDS)," ",Nonnegative(FUNDS));
        return s;
    }

    Snapshot Capture20()
    {
        enum addrs : uintptr_t {
            CURRENT_SCREEN = 0x1c5848,
            NEXT_SCREEN = 0x1c584c,
            GAME_THREAD = 0x1ba828,
            TRANSITION = 0x1c06a0,
            GAME_FLAGS = 0x1ba5d4,
            PAUSE_MENU = 0x1c60bc,
            REPLAY = 0x1c60fc,
            ACTIVITY = 0x1b8858,
            MODE = 0x1ba5d8,
            SELECTED_SPELL = 0x1ba7f4,
            GLOBALS = 0x1ba5f0,
            DIFFICULTY = 0x1b0a60,
            STAGE = 0x1ba7e4,
            SPELL_CARD = 0x1ba578,
            GUI = 0x1c06a4,
        };
        Snapshot s;s.title=S(TH20_TITLE);
        const int screen=I(CURRENT_SCREEN);
        if(screen!=I(NEXT_SCREEN)) {s.phase=Phase::Loading;return s;}
        if(screen==4) {s.phase=Phase::Title;return s;}
        if(screen==15||screen==16) {s.phase=Phase::Ending;return s;}
        if(screen!=7) return s;
        const auto thread=P(GAME_THREAD);
        if(!thread||P(TRANSITION)) {s.phase=Phase::Loading;return s;}
        if(I(GAME_FLAGS)&0x20) {s.phase=Phase::Title;s.mode="デモ";return s;}
        const auto pause=Required(PAUSE_MENU),replay=P(REPLAY);
        const int state=Read<int>(pause+0xd0),activity=I(ACTIVITY),flags=Read<int>(thread+0xe8);
        const int mode=I(MODE), replayFlags=replay?Read<int>(replay+0x25c):0;
        if(state==2&&activity==1) s.phase=Phase::GameOver;
        else if(state==3&&activity==1) s.phase=Phase::Results;
        else if(state==1&&activity==0&&(flags&0xf4)==0x10) s.phase=(replayFlags&2)?Phase::Results:Phase::Paused;
        else if(flags&0x80) s.phase=Phase::Ending;
        else if(state==0&&activity==2&&!(flags&0xf4)) s.phase=Phase::Playing;
        if(!Active(s.phase)) return s;
        Range(mode,0,2);const int selectedSpell=Range(I(SELECTED_SPELL),-1,112);
        const bool watching=replay&&Range(Read<int>(replay+0x10),0,1)==1;
        s.mode=watching?"リプレイ鑑賞":mode==0?"通常プレイ":mode==1?"プラクティス":selectedSpell>=0?S(TH_SPELL_PRAC):"";
        if(s.mode.empty()) throw std::runtime_error("Invalid TH20 mode");
        constexpr uintptr_t g=GLOBALS;
        const th_glossary_t characters[]={TH_TRACKER_REIMU,TH_TRACKER_MARISA};
        const char* const stones[]={"スカーレットデビル","クリーチャーレッド","スノーブロッサム","ブルーシーズン","イエローサブタレイニアン","インペリシャブルムーン","ビーストハードネス","シントイズムウィンド","コモン魔石"};
        s.character=S(characters[Range(I(g+8),0,1)]);
        s.equipment=Text("異変石 ",Choice(I(g+0x1c),stones)," / 集中石 ",Choice(I(g+0x20),stones)," / 拡散石 ",Choice(I(g+0x24),stones)," / 支援石 ",Choice(I(g+0x28),stones));
        s.difficulty=Choice(Nonnegative(DIFFICULTY,4),difficulties);s.stage=Stage(I(STAGE));
        s.score=Score(g,true);s.lives=Range(I(g+0xb8),-1,7);s.bombs=Nonnegative(g+0xcc,7);s.power=Nonnegative(g+0x30,400)/100.0;
        s.spell=ReadSpellName(Required(SPELL_CARD),112);
        if(Read<uint32_t>(Required(GUI)+0x1bc)) s.segment="会話中";
        else if(!s.spell.empty()) s.segment=S(TH_SPELL);
        s.elapsedLabel="面経過";s.elapsedSeconds=Range(Read<int>(thread+0x14),0,INT_MAX)/60.0;
        s.extra=Text(S(TH20_HYPER)," ",Nonnegative(g+0x4c,500),"/",Range(I(g+0x50),100,500)," / ",S(TH20_STONE_GAUGE)," ",Nonnegative(g+0x5c,10000),"/",Nonnegative(g+0x60,5000)," / 石Lv 赤",Nonnegative(g+0x74,4)+1," 青",Nonnegative(g+0x78,4)+1," 黄",Nonnegative(g+0x7c,4)+1," 緑",Nonnegative(g+0x80,4)+1);
        return s;
    }
}

Snapshot CaptureLate(int game, bool th19v110c, int practiceMode)
{
    if(game==19) return Capture19(th19v110c);
    if(game==20) return Capture20();
    if(game==185) return Capture185(practiceMode);
    if(game==0) return CaptureAlcostg(practiceMode);
    if(game<13||game>18) throw std::runtime_error("Unregistered late game");
    return CaptureModern(layouts[game-13],practiceMode);
}
#ifdef THPRAC_PRESENCE_TESTS
void SetLateMemoryReader(LateMemoryReader reader) { testReader = reader; }
#endif
}
