// Offline only: all IPC names are unique thprac-test names, never discord-ipc-*.
#define THPRAC_DISCORD_TESTS
#include "../thprac/src/thprac_discord.cpp"

#include <atomic>
#include <cstdio>
#include <thread>

namespace {
using namespace DiscordPresence;

void Check(bool value, const char* reason)
{
    if (!value) throw std::runtime_error(reason);
}

void FormatChecks()
{
    Check(IsValidApplicationId(DefaultApplicationId), "default application ID");
    for (const auto id : {"", "0", "00000000000000000", "1234567890123456x", "18446744073709551616", "123456789012345678901"})
        Check(!IsValidApplicationId(id), "invalid application ID accepted");
    Snapshot snapshot;
    snapshot.title = "東方紅魔郷";
    snapshot.phase = Phase::Playing;
    snapshot.mode = "通常";
    snapshot.difficulty = "Normal";
    snapshot.stage = "Stage 1";
    snapshot.character = "霊夢";
    snapshot.equipment = "A";
    snapshot.score = 12345678;
    snapshot.lives = 3;
    snapshot.bombs = 2;
    snapshot.power = 1.5;
    snapshot.graze = 1234;
    snapshot.pointItemValue = 10000;
    snapshot.elapsedSeconds = 120;
    auto activity = *Format("th06", snapshot, 1000);
    Check(activity.startUnixSeconds == 880, "elapsed timestamp");
    Check(activity.details.find("霊夢A") != std::string::npos, "shot suffix split");
    Check(activity.state.find("現在スコア 12,345,678") != std::string::npos, "current score format");
    Check(activity.state.find("最大得点 10,000") != std::string::npos, "PIV format");
    snapshot.score = 12345679;
    snapshot.elapsedSeconds = 121;
    Check(Format("th06", snapshot, 1001)->significantKey == activity.significantKey, "continuous counters became major changes");
    snapshot.phase = Phase::Paused;
    auto paused = *Format("th06", snapshot, 1001);
    Check(!paused.startUnixSeconds && paused.details.find("2:01") != std::string::npos, "paused timer advances");
    Check(paused.significantKey != activity.significantKey, "phase not significant");
    snapshot.phase = Phase::Title;
    auto title = *Format("th06", snapshot);
    Check(title.state == "プレイ開始待ち" && title.details == "タイトル" && !title.startUnixSeconds, "title exposes retained run values");
    snapshot.mode = "デモ";
    Check(Format("th06", snapshot)->details == "デモ再生中", "title demo misclassified");
    snapshot.phase = Phase::Unknown;
    Check(!Format("th06", snapshot), "unknown not cleared");
    snapshot.phase = Phase::Loading;
    Check(!Format("th06", snapshot), "loading not cleared");
    snapshot.phase = Phase::Playing;
    snapshot.elapsedLabel = "対戦残り";
    Check(!Format("th19", snapshot)->startUnixSeconds, "countdown emitted elapsed timestamp");
    Check(!Format("th09", snapshot)->startUnixSeconds, "TH09 clock advances independently");
    snapshot.elapsedSeconds.reset();
    snapshot.character.assign(126, 'a');
    snapshot.character += "😀😀😀";
    snapshot.extra = "改行\n維持\t\r\x01";
    auto longText = *Format("th06", snapshot);
    Check(Wide(longText.details).size() <= 128 && Wide(longText.state).size() <= 128, "UTF-16 limit");
    Check(!Wide(longText.details).empty(), "truncation splits Unicode scalar");
    Check(longText.state.find('\t') == std::string::npos && longText.state.find('\r') == std::string::npos, "control text leaked");
    snapshot.character = "霊夢";
    snapshot.equipment = "選択 Screen A 残10秒\n装備 Screen A・Screen B";
    const auto cards = Format("th18", snapshot)->significantKey;
    snapshot.equipment = "選択 Screen A 残9秒\n装備 Screen A・Screen B";
    Check(Format("th18", snapshot)->significantKey == cards, "card cooldown consumes major-change budget");
    snapshot.equipment = "選択 Screen B 残9秒\n装備 Screen A・Screen B";
    Check(Format("th18", snapshot)->significantKey != cards, "selected card not significant");
    snapshot.equipment.clear();
    snapshot.extra = "取得ラウンド 1P 1 / 2P 0\n1P ゲージ 100/2500 / 2P ゲージ 200/2500";
    auto match = Format("th19", snapshot)->significantKey;
    snapshot.extra = "取得ラウンド 1P 1 / 2P 0\n1P ゲージ 101/2500 / 2P ゲージ 200/2500";
    Check(Format("th19", snapshot)->significantKey == match, "TH19 gauge consumes major-change budget");
    snapshot.extra = "取得ラウンド 1P 2 / 2P 0\n1P ゲージ 101/2500 / 2P ゲージ 200/2500";
    Check(Format("th19", snapshot)->significantKey != match, "TH19 round win not significant");

    auto payload = Parse(ActivityJson(title, "42"));
    auto* args = yyjson_obj_get(yyjson_doc_get_root(payload.get()), "args");
    auto* presence = yyjson_obj_get(args, "activity");
    Check(yyjson_get_uint(yyjson_obj_get(args, "pid")) == GetCurrentProcessId(), "payload PID");
    Check(!yyjson_obj_get(presence, "timestamps") && !yyjson_obj_get(presence, "assets"), "optional objects must be omitted");
    payload = Parse(ActivityJson(std::nullopt, "43"));
    Check(yyjson_is_null(yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(payload.get()), "args"), "activity")), "explicit clear omitted");

    Budget budget;
    for (int i = 0; i != 4; ++i) { Check(budget.Available(false), "normal budget too small"); budget.Record(); }
    Check(!budget.Available(false) && budget.Available(true), "clear slot not reserved");
    budget.Record();
    Check(!budget.Available(true), "rate window overrun");
    budget.sent.front() = GetTickCount64() - RateWindowMs;
    Check(budget.Available(true), "rate window does not expire");
}

struct PipePair {
    Handle server, client;
    explicit PipePair(const std::wstring& name)
        : server(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr)),
          client(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr))
    {
        Check(!!server && !!client, "test pipe pair create");
        OVERLAPPED overlap{};
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        overlap.hEvent = event.value;
        Check(ConnectNamedPipe(server.value, &overlap) || GetLastError() == ERROR_PIPE_CONNECTED, "test pipe connect");
    }
};

void FrameChecks(const std::wstring& name)
{
    PipePair pipes(name);
    Check(SameUser(pipes.client.value), "same Windows account rejected");
    Frames frames;
    uint32_t header[] = {1, 2};
    Transfer(pipes.server.value, header, 3, true, 1000);
    Check(!frames.Next(pipes.client.value), "split header accepted early");
    Transfer(pipes.server.value, reinterpret_cast<unsigned char*>(header) + 3, 5, true, 1000);
    char body[] = "{}";
    Transfer(pipes.server.value, body, 1, true, 1000);
    Check(!frames.Next(pipes.client.value), "split body accepted early");
    Transfer(pipes.server.value, body + 1, 1, true, 1000);
    auto frame = frames.Next(pipes.client.value);
    Check(frame && frame->payload == "{}", "fragmented frame lost");
    header[1] = static_cast<uint32_t>(MaxFrameSize + 1);
    Transfer(pipes.server.value, header, 8, true, 1000);
    bool rejected = false;
    try { frames.Next(pipes.client.value); } catch (...) { rejected = true; }
    Check(rejected, "oversized frame not rejected before allocation");
    for (const auto text : {"{", "[]", "null"}) {
        rejected = false;
        try { Parse(text); } catch (...) { rejected = true; }
        Check(rejected, "malformed/non-object JSON accepted");
    }
    auto error = Parse("{\"evt\":\"ERROR\",\"data\":{\"retry_after\":60}}");
    rejected = false;
    try { CheckError(yyjson_doc_get_root(error.get())); } catch (const RpcError& e) { rejected = e.retryMs == 60000; }
    Check(rejected, "server retry-after discarded");
}

std::atomic<bool> throwing{true}, pumping{true}, enabled{true}, done{false};
std::atomic<bool> shutdownRequested{false};
std::atomic<Phase> phase{Phase::Playing};
DWORD frameThread;
Snapshot CaptureSample()
{
    Check(GetCurrentThreadId() == frameThread, "worker read game memory");
    if (throwing.load()) throw std::runtime_error("synthetic unavailable game memory");
    Snapshot snapshot;
    snapshot.title = "Offline test game";
    snapshot.phase = phase.load();
    snapshot.stage = "Stage 2";
    snapshot.score = GetTickCount64();
    return snapshot;
}

Frame Receive(Frames& frames, HANDLE pipe, DWORD timeout = 4000)
{
    const auto deadline = GetTickCount64() + timeout;
    while (GetTickCount64() < deadline) {
        if (auto frame = frames.Next(pipe)) return *frame;
        Sleep(5);
    }
    throw std::runtime_error("mock Discord receive timeout");
}

bool Clear(const Frame& frame)
{
    auto json = Parse(frame.payload);
    auto* root = yyjson_doc_get_root(json.get());
    Check(Text(root, "cmd") == "SET_ACTIVITY", "unexpected command");
    return yyjson_is_null(yyjson_obj_get(yyjson_obj_get(root, "args"), "activity"));
}

void Ack(HANDLE pipe, const Frame& frame, bool obsolete = false)
{
    auto json = Parse(frame.payload);
    auto nonce = std::string(Text(yyjson_doc_get_root(json.get()), "nonce"));
    if (obsolete) nonce = "obsolete";
    Send(pipe, 1, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" + nonce + "\"}");
}

void WorkerChecks(const std::wstring& name, bool stopWhileActive, bool stopDuringClear)
{
    Testing::SetPipeName(name.c_str());
    Initialize("th06", CaptureSample);
    frameThread = GetCurrentThreadId();
    std::exception_ptr failure;
    std::thread server([&]() {
        const char* stage = "connect";
        try {
            Handle pipe(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr));
            Check(!!pipe, "mock pipe creation");
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            OVERLAPPED overlap{};
            overlap.hEvent = event.value;
            Check(!ConnectNamedPipe(pipe.value, &overlap) && GetLastError() == ERROR_IO_PENDING, "mock connect setup");
            Check(WaitForSingleObject(event.value, 250) == WAIT_TIMEOUT, "capture failure still published a game");
            throwing = false;
            Check(WaitForSingleObject(event.value, 4000) == WAIT_OBJECT_0, "worker did not connect");
            stage = "first handshake";
            // The handshake uses opcode 0, which the production server-frame parser rejects.
            uint32_t header[2];
            Transfer(pipe.value, header, 8, false, 4000);
            Check(header[0] == 0 && header[1] < 200, "handshake header");
            std::string handshake(header[1], 0);
            Transfer(pipe.value, handshake.data(), header[1], false, 4000);
            auto hello = Parse(handshake);
            Check(Text(yyjson_doc_get_root(hello.get()), "client_id") == DefaultApplicationId, "handshake application ID");
            // A pipe opening is not a healthy session. Reject before READY and
            // verify that the worker reconnects and performs a new handshake.
            Send(pipe.value, 2, "{\"code\":1000}");
            CloseHandle(pipe.value);
            pipe.value = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
            Check(!!pipe, "reconnect server creation");
            ResetEvent(event.value);
            overlap = {};
            overlap.hEvent = event.value;
            Check(!ConnectNamedPipe(pipe.value, &overlap) && GetLastError() == ERROR_IO_PENDING, "reconnect setup");
            Check(WaitForSingleObject(event.value, 4000) == WAIT_OBJECT_0, "worker did not reconnect after handshake rejection");
            stage = "reconnect handshake";
            Transfer(pipe.value, header, 8, false, 4000);
            Check(header[0] == 0 && header[1] < 200, "reconnect handshake header");
            handshake.assign(header[1], 0);
            Transfer(pipe.value, handshake.data(), header[1], false, 4000);
            Frames frames;
            stage = "ping";
            Send(pipe.value, 3, "ping payload");
            auto pong = Receive(frames, pipe.value);
            Check(pong.opcode == 4 && pong.payload == "ping payload", "PING not echoed");
            pumping = false;
            Sleep(150);
            const std::string ready = "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\"}";
            stage = "ready";
            uint32_t readyHeader[] = {1, static_cast<uint32_t>(ready.size())};
            Transfer(pipe.value, readyHeader, 4, true, 1000);
            Sleep(10);
            Transfer(pipe.value, readyHeader + 1, 4, true, 1000);
            Transfer(pipe.value, const_cast<char*>(ready.data()), static_cast<DWORD>(ready.size()), true, 1000);
            auto initial = Receive(frames, pipe.value);
            Check(Clear(initial), "pre-READY sample replayed");
            stage = "initial clear acknowledgement";
            Ack(pipe.value, initial, true);
            pumping = true;
            Sleep(200);
            Check(!frames.Next(pipe.value), "obsolete nonce acknowledged current update");
            Ack(pipe.value, initial);
            auto playing = Receive(frames, pipe.value);
            stage = "playing acknowledgement";
            Check(!Clear(playing), "fresh game sample missing");
            Ack(pipe.value, playing);
            Sleep(250);
            Check(!frames.Next(pipe.value), "continuous score changes bypass 15-second cadence");
            pumping = false;
            stage = "stale clear";
            auto stale = Receive(frames, pipe.value);
            Check(Clear(stale), "stale sample retained");
            Ack(pipe.value, stale);
            phase = Phase::Paused;
            pumping = true;
            auto paused = Receive(frames, pipe.value);
            stage = "paused acknowledgement";
            Check(!Clear(paused), "fresh paused sample missing");
            auto pausedJson = Parse(paused.payload);
            auto* activity = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(pausedJson.get()), "args"), "activity");
            Check(Text(activity, "details").starts_with("ポーズ"), "phase transition coalesced incorrectly");
            Ack(pipe.value, paused);
            if (stopWhileActive) shutdownRequested = true;
            else enabled = false;
            stage = "stop clear";
            auto disabled = Receive(frames, pipe.value);
            Check(Clear(disabled), "reserved clear slot not used when disabled/stopped");
            if (stopDuringClear) {
                // The fifth update already used the reserved slot. Shutdown
                // must await this clear instead of sending a sixth or closing
                // the stream before the outstanding clear is acknowledged.
                shutdownRequested = true;
                Sleep(50);
            }
            Ack(pipe.value, disabled);
            const auto deadline = GetTickCount64() + 2000;
            bool disconnected = false;
            while (GetTickCount64() < deadline) {
                DWORD available = 0;
                if (!PeekNamedPipe(pipe.value, nullptr, 0, nullptr, &available, nullptr)) { disconnected = true; break; }
                Sleep(5);
            }
            Check(disconnected, "disabled/stopped IPC connection remains open");
        } catch (const std::exception& error) {
            failure = std::make_exception_ptr(std::runtime_error(std::string(stage) + ": " + error.what()));
        }
        done = true;
    });
    ULONGLONG maxTick = 0;
    const auto deadline = GetTickCount64() + 20000;
    while (!done.load() && GetTickCount64() < deadline) {
        if (shutdownRequested.exchange(false)) Shutdown();
        if (pumping.load()) {
            const auto start = GetTickCount64();
            Tick(enabled.load());
            maxTick = std::max(maxTick, GetTickCount64() - start);
        }
        Sleep(10);
    }
    Shutdown();
    server.join();
    if (failure) std::rethrow_exception(failure);
    Check(done.load(), "worker tests deadline");
    Check(maxTick < 100, "frame thread blocked on Discord IO");
    std::printf("Max synthetic Tick duration: %llu ms\n", maxTick);
}
}

int main(int argc, char** argv)
{
    try {
        const auto prefix = L"\\\\.\\pipe\\thprac-test-" + std::to_wstring(GetCurrentProcessId());
        FormatChecks();
        FrameChecks(prefix + L"-frames");
        const auto mode = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
        WorkerChecks(prefix + L"-worker", mode == "--shutdown", mode == "--shutdown-during-clear");
        std::puts("Discord formatter, frame parser, mock IPC lifecycle: PASS (offline only)");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
