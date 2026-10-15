#define THPRAC_PRESENCE_TESTS
#define NOMINMAX
#include "../thprac/src/thprac_discord_late.cpp"
#include "../thprac/src/thprac_locale_def.cpp"
#include <iostream>
#include <vector>

namespace Gui { Locale __glocale_current = LOCALE_JA_JP; }
namespace {
    std::vector<uint8_t> globals(0x240000), heap(0x100000);
    int checks = 0;
    void Check(bool value, const char* message)
    {
        ++checks;
        if (!value) throw std::runtime_error(message);
    }
    uint8_t* Address(uintptr_t address, size_t size)
    {
        if (address >= 0x400000 && address - 0x400000 + size <= globals.size()) return globals.data() + address - 0x400000;
        if (address >= 0x1000000 && address - 0x1000000 + size <= heap.size()) return heap.data() + address - 0x1000000;
        return nullptr;
    }
    bool FakeRead(uintptr_t address, void* output, size_t size)
    {
        auto source = Address(address, size);
        if (!source) return false;
        std::memcpy(output, source, size);
        return true;
    }
    template<typename T> void Put(uintptr_t address, T value)
    {
        auto target = Address(address, sizeof(value));
        if (!target) throw std::runtime_error("Bad fixture address");
        std::memcpy(target, &value, sizeof(value));
    }
    template<typename T> void Rva(uintptr_t address, T value) { Put(0x400000 + address, value); }
    void Reset() { std::fill(globals.begin(), globals.end(), uint8_t{}); std::fill(heap.begin(), heap.end(), uint8_t{}); }
    template<typename F> void Rejected(F capture)
    {
        bool rejected=false;
        try { capture(); } catch(const std::runtime_error&) { rejected=true; }
        Check(rejected,"Invalid snapshot must be rejected");
    }
    using namespace DiscordPresence;
    constexpr uintptr_t thread=0x1000000, pause=0x1010000, spell=0x1020000, enemy=0x1030000, gui=0x1040000, ability=0x1050000;

    void ModernFixture(const Layout& l)
    {
        Reset();
        Rva(l.screen,7);Rva(l.screen+4,7);Rva(l.activity,2);Rva(l.thread,static_cast<uint32_t>(thread));
        Rva(l.mode,1);Rva(l.stage,2);Rva(l.difficulty,1);Rva(l.character,0);
        Rva(l.score,123456u);Rva(l.lives,2);Rva(l.bombs,3);Rva(l.power,200);
        Rva(l.spell,static_cast<uint32_t>(spell));Rva(l.enemy,static_cast<uint32_t>(enemy));Rva(l.gui,static_cast<uint32_t>(gui));
        Put(thread+l.frameOffset,120);
        constexpr uintptr_t pauses[]={0xc22c0,0xdb678,0xe9bb4,0xa6ef4,0xb77cc,0xcf40c};
        Rva(pauses[l.game-13],static_cast<uint32_t>(pause));
        if(l.game==15) Rva(0xe9bec,1);
        if(l.game==16) {
            Rva(0xa6da4,static_cast<uint32_t>(ability));
            const int thresholds[]={100,230,390,590,840,1140};
            for(int i=0;i<6;++i) Rva(0xa583c+i*4,thresholds[i]);
        }
        if(l.game==18) Rva(0xcf298,static_cast<uint32_t>(ability));
    }

    void ModernTests()
    {
        for(const auto& l:layouts) {
            ModernFixture(l);
            const auto live=CaptureLate(l.game);
            Check(live.phase==Phase::Playing,"Late game playing state");
            Check(live.score==1234560 && live.lives==2 && live.bombs==3 && live.power==2,"Late game resource units");
            Check(live.elapsedSeconds==2 && !live.character.empty() && !live.stage.empty(),"Late game durable fields");
            Rva(l.mode,2);
            Check(CaptureLate(l.game,false,1).mode.starts_with("リプレイ鑑賞"),"Replay outranks custom practice");
            Rva(l.mode,1);
            Check(CaptureLate(l.game,false,1).mode.starts_with("プラクティス"),"Custom practice detected");

            Put(pause+(l.game<=14?4:0x1ec),1);Rva(l.activity,0);Put(thread+l.flagsOffset,0x10);
            Put(pause+(l.game<=14?0x200:0x208),2);
            if(l.game>=15) Put(pause+0x1f4,6);
            Check(CaptureLate(l.game).phase==Phase::Paused,"Late game genuine pause");
            Put(pause+(l.game<=14?0x1fc:0x204),1);
            Check(!Active(CaptureLate(l.game).phase),"Completion is not a user pause");

            ModernFixture(l);Rva(l.screen,4);Rva(l.screen+4,4);
            const auto menu=CaptureLate(l.game);
            Check(!Active(menu.phase)&&!menu.score&&!menu.lives&&menu.character.empty(),"Menus clear stale resources");
            ModernFixture(l);Rva(l.screen+4,4);
            Check(CaptureLate(l.game).phase==Phase::Loading&&!CaptureLate(l.game).score,"Transition clears stale data");
            ModernFixture(l);Rva(l.character,99);Rejected([&]{CaptureLate(l.game);});
            ModernFixture(l);Rva(l.spell,0u);Rejected([&]{CaptureLate(l.game);});
            ModernFixture(l);Rva(l.mode,99);Rejected([&]{CaptureLate(l.game);});
        }
        ModernFixture(layouts[2]);Rva(0xe9bec,0);Rva(0xe7594,12);Rva(0xe75b8,3);Rva(0xe73f8,2);
        auto pointdevice=CaptureLate(15);
        Check(!pointdevice.lives&&pointdevice.lifePieces.empty()&&pointdevice.extra.find("総リトライ 12")!=std::string::npos,"TH15 Pointdevice resources");
        Check(CaptureLate(15,false,1).lives==2&&CaptureLate(15,false,1).extra.empty(),"TH15 custom practice uses Legacy resources");
        Rva(0xe7434,9876599u);
        Check(CaptureLate(15).pointItemValue==98760,"TH15 maximum point-item units");

        ModernFixture(layouts[3]);Rva(0xa5808,1140);
        Check(CaptureLate(16).extra.find("季節Lv 6/6")!=std::string::npos,"TH16 full season gauge");
        Rva(0xa5840,100);Rejected([]{CaptureLate(16);});

        ModernFixture(layouts[4]);Rva(0xb5ac4,static_cast<uint8_t>(2));Rva(0xb5abc,1);Rva(0xb5aa8,120);
        Check(CaptureLate(17).extra.find("残2.0秒")!=std::string::npos,"TH17 roaring timer");
        Rva(0xb5a64,99);Rejected([]{CaptureLate(17);});

        ModernFixture(layouts[5]);
        constexpr uint32_t node=0x1060000,card=0x1070000;
        Put(ability+0x1c,node);Put(ability+0x28,1);Put(ability+0x2c,1);Put(ability+0x38,card);
        Put(node,card);Put(card+4,46);Put(card+0x50,8u);Put(card+0x48,600);Put(card+0x38,120);
        Check(CaptureLate(18).equipment.find("残2.0秒")!=std::string::npos,"TH18 selected card cooldown");
        Check(CaptureLate(18).equipment.find("\n装備 ")!=std::string::npos,"TH18 stable card list remains separate from cooldown");
        Put(node+4,node);Rejected([]{CaptureLate(18);});
        ModernFixture(layouts[5]);Rva(0xcf2a4,0x1060000u);
        Check(CaptureLate(18).phase==Phase::Menu&&!CaptureLate(18).score,"TH18 shop clears playing data");
    }

    void Fixture19(bool newer)
    {
        Reset();const uintptr_t screen=newer?0x22c44c:0x208c8c,g=newer?0x22b0c0:0x207910;
        Rva(screen,7);Rva(screen+4,7);Rva(newer?0x1d1a54:0x1ae464,static_cast<uint32_t>(thread));
        Rva(newer?0x1d1bf8:0x1ae608,static_cast<uint32_t>(pause));Rva(newer?0x22eef0:0x20b210,2);
        const int side=newer?0xc8:0xc0,life=newer?0x54:0x50,bomb=newer?0x68:0x64;
        Rva(g,static_cast<int64_t>(123));Rva(g+side,static_cast<int64_t>(456));Rva(g+side*2,2);
        Rva(g+life,3);Rva(g+side+life,2);Rva(g+bomb,2);Rva(g+side+bomb,1);
        Rva(g+0xc,0);Rva(g+side+0xc,2);Rva(newer?0x22ba80:0x2082c0,3);
        Put(thread+0x14,300);
    }
    void Tests19()
    {
        for(bool newer:{false,true}) {
            Fixture19(newer);auto s=CaptureLate(19,newer);
            Check(s.phase==Phase::Playing&&s.score==1230&&s.opponentScore==4560,"Both TH19 version score layouts");
            Check(s.lives==3&&s.opponentLives==2&&s.bombs==2&&s.opponentBombs==1,"Both TH19 side resource layouts");
            Check(s.opponent.find("早苗")!=std::string::npos,"TH19 Sanae character mapping");
            Put(thread+0x160,1);Check(CaptureLate(19,newer).mode=="リプレイ鑑賞","TH19 replay mode");
            Put(thread+0x140,0x10);Put(pause+0x1f4,1);Put(pause+0x1fc,6);Put(pause+0x210,2);Rva(newer?0x22eef0:0x20b210,0);
            Check(CaptureLate(19,newer).phase==Phase::Paused,"TH19 pause");
            Put(pause+0x20c,1);Check(CaptureLate(19,newer).phase==Phase::Results&&!CaptureLate(19,newer).score,"TH19 replay completion clears data");
            Fixture19(newer);const uintptr_t g=newer?0x22b0c0:0x207910;
            Rva(g+0xc,19);Rejected([&]{CaptureLate(19,newer);});
            Fixture19(newer);Rva(newer?0x22c450:0x208c90,4);
            Check(CaptureLate(19,newer).phase==Phase::Loading&&!CaptureLate(19,newer).score,"TH19 transition clears data");
        }
        Fixture19(true);Rva(0x22b254,2);Rva(0x22bb1c,600);Rva(0x22bb20,121);
        auto s=CaptureLate(19,true);
        Check(s.mode=="対戦"&&s.stage.empty()&&s.elapsedSeconds==3&&s.elapsedLabel=="対戦残り","TH19 versus countdown rounds");
        Check(s.extra.find("\n1P ゲージ ")!=std::string::npos,"TH19 round wins remain separate from charge gauges");
    }

    void Fixture20()
    {
        Reset();Rva(0x1c5848,7);Rva(0x1c584c,7);Rva(0x1ba828,static_cast<uint32_t>(thread));
        Rva(0x1c60bc,static_cast<uint32_t>(pause));Rva(0x1b8858,2);Rva(0x1ba7f4,-1);Rva(0x1ba7e4,1);
        Rva(0x1ba5f0,static_cast<int64_t>(12345));Rva(0x1ba5f0+0x50,100);Rva(0x1ba5f0+0xb8,2);Rva(0x1ba5f0+0xcc,3);
        Rva(0x1ba578,static_cast<uint32_t>(spell));Rva(0x1c06a4,static_cast<uint32_t>(gui));
    }
    void Tests20()
    {
        Fixture20();auto s=CaptureLate(20);
        Check(s.phase==Phase::Playing&&s.score==123450&&s.lives==2,"TH20 live snapshot");
        Check(s.equipment.find("スカーレットデビル")!=std::string::npos,"TH20 stone equipment");
        Rva(0x1c60fc,static_cast<uint32_t>(ability));Put(ability+0x10,1);Rva(0x1ba5d8,2);Rva(0x1ba7f4,0);
        Check(CaptureLate(20).mode=="リプレイ鑑賞","TH20 replay outranks spell practice");
        Put(ability+0x10,0);Check(CaptureLate(20).mode==S(TH_SPELL_PRAC),"TH20 recording object is not playback");
        Put(pause+0xd0,2);Rva(0x1b8858,1);s=CaptureLate(20);
        Check(s.phase==Phase::GameOver&&!s.score&&s.equipment.empty(),"TH20 game over clears data");
        Fixture20();Rva(0x1c584c,4);Check(CaptureLate(20).phase==Phase::Loading,"TH20 loading transition");
        Fixture20();Rva(0x1ba5f0+0x1c,9);Rejected([]{CaptureLate(20);});
        Fixture20();Rva(0x1ba578,1u);Rejected([]{CaptureLate(20);});
    }
    void FixtureAlcostg()
    {
        Reset();Rva(0x8e9e4,7);Rva(0x8e9e8,7);
        Rva(0x74170,static_cast<uint32_t>(thread));Rva(0x74190,static_cast<uint32_t>(pause));
        Rva(0x74194,static_cast<uint32_t>(gui));Rva(0x74198,static_cast<uint32_t>(ability));
        Rva(0x74054,static_cast<uint32_t>(spell));Rva(0x74064,static_cast<uint32_t>(enemy));
        Rva(0x8e5b4,7);Rva(0x70738+7*0x2c,1);Rva(0x70738+8*0x2c,2);Rva(0x70738+9*0x2c,3);
        Rva(0x8e584,123456u);Rva(0x8e608,static_cast<int16_t>(1500));Rva(0x8e60a,static_cast<int16_t>(5000));
        Rva(0x8e5e4,61200);Rva(0x8e5e8,75600);Rva(0x8e5fc,7200);Rva(0x8e5d4,1800);Put(thread+0x14,120);
    }
    void TestsAlcostg()
    {
        FixtureAlcostg();auto s=CaptureLate(0);
        Check(s.phase==Phase::Playing&&s.score==123456&&s.elapsedSeconds==2,"Alcostg direct score and elapsed frame units");
        Check(s.stage==S(Alcostg::ALCOSTG_STAGE_SELECT[0]),"Alcostg stage-table index to displayed stage");
        Check(s.extra.find("18:00")!=std::string::npos&&s.extra.find("1.5/5.0L")!=std::string::npos,"Alcostg actual HUD clock and beer litres");
        Check(CaptureLate(0,false,1).mode==S(ALCOSTG_PRACTICE_MODE),"Alcostg thprac mode");
        Put(ability+0x10,1);Check(CaptureLate(0,false,1).mode=="リプレイ鑑賞","Alcostg native replay outranks custom practice");
        for(int state=1;state<=5;++state) {
            Put(pause+4,state);Put(thread+0x58,0x10);
            Check(CaptureLate(0).phase==Phase::Paused,"Alcostg pause and confirmation substates");
        }
        for(int state=6;state<=13;++state) {
            Put(pause+4,state);s=CaptureLate(0);
            Check(s.phase==Phase::Results&&!s.score&&s.extra.empty(),"Alcostg terminal menus cannot retain play resources");
        }
        FixtureAlcostg();Put(thread+0x58,0x20);
        Check(CaptureLate(0).phase==Phase::Unknown&&!CaptureLate(0).score,"Alcostg unreviewed stop flag fails closed");
        FixtureAlcostg();Rva(0x8e9e8,4);
        Check(CaptureLate(0).phase==Phase::Loading&&!CaptureLate(0).score,"Alcostg transition excludes stale score");
        Rva(0x8e9e4,4);Check(CaptureLate(0).phase==Phase::Menu,"Alcostg main menu");
        FixtureAlcostg();Rva(0x8e610,0x20);
        Check(CaptureLate(0).phase==Phase::Title&&!CaptureLate(0).score,"Alcostg attract demo");
        FixtureAlcostg();Put(spell+0x378c,1u);
        const char name[]="Test special menu";for(size_t i=0;i<sizeof(name);++i) Put(spell+0x3748+i,name[i]);
        Check(CaptureLate(0).spell==name,"Alcostg current special menu name");
        Put(spell+0x378c,0u);Check(CaptureLate(0).spell.empty(),"Alcostg completed spell name is not stale");
        Rva(0x8e5b4,10);Rejected([]{CaptureLate(0);});
        FixtureAlcostg();Rva(0x8e608,static_cast<int16_t>(-1));Rejected([]{CaptureLate(0);});
        FixtureAlcostg();Rva(0x8e5fc,0);Rejected([]{CaptureLate(0);});
        FixtureAlcostg();Rva(0x74194,0u);Check(CaptureLate(0).phase==Phase::Loading,"Alcostg stage missing player");
    }
    void Tests185()
    {
        Reset();Rva(0xd7b0c,static_cast<uint32_t>(thread));Rva(0xd7ad8,static_cast<uint32_t>(enemy));Rva(0xd7c3c,static_cast<uint32_t>(gui));
        Rva(0xd1004,8);Rva(0xd1038,7);Rva(0xd10bc,3);Rva(0xd1078,250);Rva(0xd1024,12);Rva(0xd1070,456);Rva(0xd106c,789);
        auto s=CaptureLate(185);
        Check(s.phase==Phase::Playing&&s.stage=="Challenge Market"&&s.segment=="Wave 12","TH185 live market and wave");
        Check(s.lives==3&&s.power==3.5&&s.extra.find("456")!=std::string::npos,"TH185 currency and power units");
        Put(thread+0xb0,0x10u);Check(CaptureLate(185).phase==Phase::Paused,"TH185 pause bit");
        Put(thread+0xb0,0x1000u);Check(CaptureLate(185).phase==Phase::Unknown&&!CaptureLate(185).lives,"TH185 unknown flags clear data");
        Put(thread+0xb0,0u);Rva(0xd7ac4,static_cast<uint32_t>(ability));
        Check(CaptureLate(185).phase==Phase::Menu&&!CaptureLate(185).lives,"TH185 black market clears play data");
        Rva(0xd7ac4,0u);Rva(0xd1004,9);Rejected([]{CaptureLate(185);});
        Rva(0xd7b0c,0u);Check(CaptureLate(185).phase==Phase::Menu&&!CaptureLate(185).score,"TH185 exit clears stale stage");
    }
}
int main()
{
    try {
        DiscordPresence::SetLateMemoryReader(FakeRead);
        ModernTests();Tests19();Tests20();Tests185();TestsAlcostg();
        std::cout<<"Late Discord snapshot checks: "<<checks<<" passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
