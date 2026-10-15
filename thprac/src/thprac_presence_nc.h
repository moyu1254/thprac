#pragma once
#include "thprac_discord.h"

namespace DiscordPresence {
// Pure decoding over TH06NC's existing typed GameManager/Supervisor. Keeping
// this separate lets fixtures cover lifecycle precedence without loading a game
// or requiring the hook/Direct3D runtime. The production structs assert offsets.
template<class Game, class Supervisor>
Snapshot CaptureNewClassic(const Game& game, const Supervisor& supervisor,
    const char* title, const char* character, bool spellActive, uint32_t spellId, bool boss)
{
    Snapshot s;
    s.title = title;
    if (supervisor.curState != supervisor.wantedState) { s.phase = Phase::Loading; return s; }
    switch (static_cast<uint32_t>(supervisor.curState)) {
    case 0: case 3: case 12: s.phase = Phase::Loading; return s;
    case 1: case 8: s.phase = Phase::Title; return s;
    case 7: s.phase = Phase::Results; return s;
    case 9: s.phase = Phase::Menu; return s;
    case 10: s.phase = Phase::Ending; return s;
    case 2: break;
    default: return s;
    }
    if (game.retryState > 1 || game.isClear > 1 || game.spellPracEndFlag < 0 || game.spellPracEndFlag > 1) return s;
    if (game.retryState) { s.phase = Phase::GameOver; return s; }
    if (game.isClear) { s.phase = Phase::Clear; return s; }
    if (game.spellPracEndFlag) { s.phase = Phase::Results; return s; }
    if (game.difficulty > 4 || game.character > 1 || game.subShot > 1 || game.stage < 1 || game.stage > 7
        || game.pauseState > 2 || game.inReplay < 0 || game.inReplay > 1 || game.inPracticeMode < 0 || game.inPracticeMode > 1
        || game.inSpellPrac < 0 || game.inSpellPrac > 1 || game.mode > 1 || game.livesRemaining < 0 || game.livesRemaining > 8
        || game.bombsRemaining < 0 || game.bombsRemaining > 8 || game.curPower > 128 || game.actualScore < 0 || game.actualScore > 9999999990LL
        || game.totalGraze < 0 || game.stageGraze < 0 || game.stageTime < 0 || (spellActive && spellId >= 134))
        return s;
    s.phase = game.pauseState ? Phase::Paused : Phase::Playing;
    s.mode = game.inReplay ? "リプレイ鑑賞" : game.inSpellPrac ? "スペルプラクティス" : game.inPracticeMode ? "プラクティス" : "通常プレイ";
    static const char* difficulties[] = { "Easy", "Normal", "Hard", "Lunatic", "Extra" };
    s.difficulty = difficulties[game.difficulty];
    s.character = character; s.equipment = game.subShot ? "B" : "A";
    s.stage = game.stage == 7 ? "Extra" : "Stage " + std::to_string(game.stage);
    s.lives = game.livesRemaining; s.bombs = game.bombsRemaining; s.power = game.curPower;
    s.score = game.actualScore; s.graze = game.totalGraze; s.stageGraze = game.stageGraze;
    s.pointItems = game.totalPointItems; s.stagePointItems = game.stagePointItems;
    s.elapsedSeconds = game.stageTime / 60.0; s.elapsedLabel = "面経過";
    s.segment = spellActive || boss ? "ボス戦" : "道中";
    if (spellActive) s.spell = "スペル #" + std::to_string(spellId + 1);
    s.extra = game.mode ? "New Classic" : "Classic";
    return s;
}
}
