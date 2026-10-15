// Adapted from TouhouStatus's PresenceFormatter and DiscordPresenceService.
// Copyright (c) 2026 moyu1254. MIT; see docs/touhoustatus-license.txt.
#include "thprac_discord.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <yyjson.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace DiscordPresence {
namespace {
constexpr ULONGLONG FreshnessMs = 2000;
constexpr ULONGLONG RateWindowMs = 20000;
constexpr size_t MaxFrameSize = 65536;

std::wstring Wide(std::string_view text)
{
    if (text.empty() || text.size() > INT_MAX) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) return {};
    std::wstring result(length, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::string Utf8(std::wstring_view text)
{
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) return {};
    std::string result(length, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::string Limit(std::string_view text, bool lines = false)
{
    auto wide = Wide(text);
    std::erase_if(wide, [lines](wchar_t ch) { return (ch < 32 || (ch >= 127 && ch <= 159)) && !(lines && ch == L'\n'); });
    if (wide.size() > 128) {
        size_t length = 127;
        if (wide[length - 1] >= 0xD800 && wide[length - 1] <= 0xDBFF) --length;
        wide.resize(length);
        wide += L'…';
    }
    return Utf8(wide);
}

std::string Join(std::initializer_list<std::string> values, const char* separator = " | ")
{
    std::string result;
    for (const auto& value : values) {
        if (value.empty()) continue;
        if (!result.empty()) result += separator;
        result += value;
    }
    return result;
}

std::string Commas(std::string number)
{
    const auto point = number.find('.');
    const auto end = point == std::string::npos ? number.size() : point;
    const size_t start = !number.empty() && number[0] == '-' ? 1 : 0;
    for (size_t i = end; i > start + 3;) { i -= 3; number.insert(i, ","); }
    return number;
}

std::string Number(double value, bool commas = false)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(2) << value;
    auto result = text.str();
    while (result.back() == '0') result.pop_back();
    if (result.back() == '.') result.pop_back();
    return commas ? Commas(result) : result;
}

std::string Value(const char* label, std::optional<double> value)
{
    return value && std::isfinite(*value) ? std::string(label) + " " + Number(*value) : "";
}

std::string Count(const char* label, std::optional<int64_t> total, std::optional<int64_t> stage = {})
{
    if (stage) return std::string(label) + " " + Commas(std::to_string(*stage)) + "（面内）" +
        (total ? " / " + Commas(std::to_string(*total)) + "（累計）" : "");
    return total ? std::string(label) + " " + Commas(std::to_string(*total)) : "";
}

std::string FirstLine(const std::string& text) { return text.substr(0, text.find('\n')); }

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};

struct Shared {
    SRWLOCK lock = SRWLOCK_INIT;
    HANDLE stop = nullptr;
    HANDLE thread = nullptr;
    bool enabled = false;
    bool stopping = false;
    std::string gameId, applicationId;
    Capture capture = nullptr;
    std::optional<Activity> latest;
    ULONGLONG observedAt = 0, sampledAt = 0, revision = 0;
};

// Deliberately process-lifetime storage: no CRT destructor may wait for a worker
// while the loader lock is held. The module is pinned before starting the thread.
Shared& State() { static Shared* state = new Shared; return *state; }
Shared* existingState = nullptr;

struct ExclusiveLock {
    SRWLOCK& lock;
    explicit ExclusiveLock(SRWLOCK& value) : lock(value) { AcquireSRWLockExclusive(&lock); }
    ~ExclusiveLock() { ReleaseSRWLockExclusive(&lock); }
};

struct Sample {
    bool enabled;
    std::string applicationId;
    std::optional<Activity> activity;
    ULONGLONG revision, observedAt;
};

Sample Latest(Shared& state)
{
    ExclusiveLock lock(state.lock);
    const auto now = GetTickCount64();
    const bool fresh = now >= state.observedAt && now - state.observedAt <= FreshnessMs;
    return {state.enabled && !state.stopping, state.applicationId, fresh ? state.latest : std::nullopt, state.revision, state.observedAt};
}

bool Stopping(Shared& state) { return WaitForSingleObject(state.stop, 0) == WAIT_OBJECT_0; }

void Diagnostic(const char* message)
{
    OutputDebugStringA("[thprac Discord] ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

struct RpcError : std::runtime_error {
    DWORD retryMs;
    RpcError(DWORD wait) : std::runtime_error("Discord rejected a request"), retryMs(wait) {}
};

std::vector<unsigned char> UserSid(HANDLE process)
{
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) throw std::runtime_error("Cannot query IPC account");
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    if (!size || size > 65536) throw std::runtime_error("Invalid IPC account");
    std::vector<unsigned char> data(size);
    if (!GetTokenInformation(token.value, TokenUser, data.data(), size, &size)) throw std::runtime_error("Cannot read IPC account");
    const auto sid = reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
    const auto length = GetLengthSid(sid);
    std::vector<unsigned char> result(length);
    if (!CopySid(length, result.data(), sid)) throw std::runtime_error("Cannot copy IPC account");
    return result;
}

bool SameUser(HANDLE pipe)
{
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid)) return false;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) return false;
    try {
        auto expected = UserSid(GetCurrentProcess());
        auto actual = UserSid(process.value);
        return EqualSid(expected.data(), actual.data()) != FALSE;
    } catch (...) { return false; }
}

#ifdef THPRAC_DISCORD_TESTS
std::wstring testPipeName;
#endif

HANDLE Connect()
{
    for (unsigned index = 0; index != 10; ++index) {
        std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(index);
#ifdef THPRAC_DISCORD_TESTS
        if (testPipeName.empty()) throw std::runtime_error("Test pipe name is required");
        name = testPipeName;
#endif
        HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) continue;
        if (SameUser(pipe)) return pipe;
        CloseHandle(pipe);
    }
    return INVALID_HANDLE_VALUE;
}

// All calls occur on the worker. Finish each bounded transfer before observing
// shutdown: canceling halfway through a frame would corrupt the final clear.
// A stalled pipe still times out, with buffers alive until cancellation completes.
void Transfer(HANDLE pipe, void* data, DWORD size, bool write, DWORD timeout)
{
    auto* cursor = static_cast<unsigned char*>(data);
    const auto deadline = GetTickCount64() + timeout;
    while (size) {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) throw std::runtime_error("Cannot create IPC event");
        OVERLAPPED overlap{};
        overlap.hEvent = event.value;
        DWORD transferred = 0;
        const BOOL completed = write ? WriteFile(pipe, cursor, size, &transferred, &overlap)
                                     : ReadFile(pipe, cursor, size, &transferred, &overlap);
        if (!completed) {
            if (GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("IPC transfer failed");
            const auto now = GetTickCount64();
            const auto remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
            const DWORD result = WaitForSingleObject(event.value, remaining);
            if (result != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &overlap);
                GetOverlappedResult(pipe, &overlap, &transferred, TRUE);
                throw std::runtime_error("IPC transfer canceled or timed out");
            }
            if (!GetOverlappedResult(pipe, &overlap, &transferred, FALSE)) throw std::runtime_error("IPC transfer failed");
        }
        if (!transferred || transferred > size) throw std::runtime_error("IPC closed");
        cursor += transferred;
        size -= transferred;
    }
}

void Send(HANDLE pipe, uint32_t opcode, const std::string& payload, DWORD timeout = 1000)
{
    if (payload.size() > MaxFrameSize) throw std::runtime_error("IPC payload too large");
    const uint32_t header[] = {opcode, static_cast<uint32_t>(payload.size())};
    std::vector<unsigned char> bytes(8 + payload.size());
    memcpy(bytes.data(), header, 8);
    memcpy(bytes.data() + 8, payload.data(), payload.size());
    Transfer(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), true, timeout);
}

struct Frame { uint32_t opcode; std::string payload; };

class Frames {
    std::vector<unsigned char> bytes;
public:
    std::optional<Frame> Next(HANDLE pipe)
    {
        for (;;) {
            if (bytes.size() >= 8) {
                uint32_t header[2];
                memcpy(header, bytes.data(), 8);
                if (header[1] > MaxFrameSize || header[0] < 1 || header[0] > 4) throw std::runtime_error("Invalid IPC frame");
                const auto size = size_t{8} + header[1];
                if (bytes.size() >= size) {
                    Frame frame{header[0], std::string(reinterpret_cast<const char*>(bytes.data() + 8), header[1])};
                    bytes.erase(bytes.begin(), bytes.begin() + size);
                    return frame;
                }
            }
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) throw std::runtime_error("IPC disconnected");
            if (!available) return std::nullopt;
            const auto size = std::min<DWORD>(available, 4096);
            const auto offset = bytes.size();
            bytes.resize(offset + size);
            Transfer(pipe, bytes.data() + offset, size, false, 1000);
        }
    }
};

using JsonDoc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using MutableDoc = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

JsonDoc Parse(const std::string& text)
{
    JsonDoc doc(yyjson_read(text.data(), text.size(), 0), yyjson_doc_free);
    if (!doc || !yyjson_is_obj(yyjson_doc_get_root(doc.get()))) throw std::runtime_error("Invalid IPC JSON");
    return doc;
}

std::string_view Text(yyjson_val* object, const char* key)
{
    const auto value = yyjson_obj_get(object, key);
    return yyjson_is_str(value) ? std::string_view(yyjson_get_str(value), yyjson_get_len(value)) : std::string_view{};
}

std::string Serialize(yyjson_mut_doc* doc)
{
    size_t size = 0;
    char* bytes = yyjson_mut_write(doc, 0, &size);
    if (!bytes) throw std::bad_alloc();
    std::unique_ptr<char, decltype(&free)> buffer(bytes, free);
    return {bytes, size};
}

std::string ActivityJson(const std::optional<Activity>& activity, const std::string& nonce)
{
    MutableDoc doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    if (!doc) throw std::bad_alloc();
    auto* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);
    yyjson_mut_obj_add_str(doc.get(), root, "cmd", "SET_ACTIVITY");
    yyjson_mut_obj_add_str(doc.get(), root, "nonce", nonce.c_str());
    auto* args = yyjson_mut_obj_add_obj(doc.get(), root, "args");
    yyjson_mut_obj_add_uint(doc.get(), args, "pid", GetCurrentProcessId());
    if (!activity) yyjson_mut_obj_add_null(doc.get(), args, "activity");
    else {
        auto* presence = yyjson_mut_obj_add_obj(doc.get(), args, "activity");
        yyjson_mut_obj_add_uint(doc.get(), presence, "type", 0);
        yyjson_mut_obj_add_str(doc.get(), presence, "name", activity->name.c_str());
        yyjson_mut_obj_add_str(doc.get(), presence, "details", activity->details.c_str());
        yyjson_mut_obj_add_str(doc.get(), presence, "state", activity->state.c_str());
        // Discord rejects optional objects set to null. No guessed asset keys.
        if (activity->startUnixSeconds) {
            auto* timestamps = yyjson_mut_obj_add_obj(doc.get(), presence, "timestamps");
            yyjson_mut_obj_add_sint(doc.get(), timestamps, "start", *activity->startUnixSeconds);
        }
    }
    return Serialize(doc.get());
}

void CheckError(yyjson_val* root)
{
    if (Text(root, "evt") != "ERROR") return;
    auto* data = yyjson_obj_get(root, "data");
    auto* retry = yyjson_obj_get(data, "retry_after");
    double seconds = yyjson_is_num(retry) ? yyjson_get_num(retry) : 20;
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400) seconds = 20;
    throw RpcError(static_cast<DWORD>(std::max(20.0, seconds) * 1000));
}

bool Control(HANDLE pipe, const Frame& frame)
{
    if (frame.opcode == 2) throw std::runtime_error("Discord closed IPC");
    if (frame.opcode == 3) { Send(pipe, 4, frame.payload); return true; }
    return frame.opcode == 4;
}

struct Budget {
    std::deque<ULONGLONG> sent;
    bool Available(bool clear) {
        const auto now = GetTickCount64();
        while (!sent.empty() && now - sent.front() >= RateWindowMs) sent.pop_front();
        return sent.size() < (clear ? 5u : 4u);
    }
    void Record() { sent.push_back(GetTickCount64()); }
};

struct Pending { std::string nonce; ULONGLONG sentAt; bool clear; };

void RunConnection(Shared& state, HANDLE pipe, const std::string& applicationId, Budget& budget, uint64_t& serial)
{
    Frames frames;
    Send(pipe, 0, "{\"v\":1,\"client_id\":\"" + applicationId + "\"}");
    const auto handshakeDeadline = GetTickCount64() + 5000;
    bool ready = false;
    while (!Stopping(state) && GetTickCount64() < handshakeDeadline && !ready) {
        if (auto frame = frames.Next(pipe)) {
            if (Control(pipe, *frame)) continue;
            auto doc = Parse(frame->payload);
            auto* root = yyjson_doc_get_root(doc.get());
            CheckError(root);
            ready = Text(root, "cmd") == "DISPATCH" && Text(root, "evt") == "READY";
        } else WaitForSingleObject(state.stop, 25);
    }
    if (!ready) throw std::runtime_error("Discord READY timed out");
    Diagnostic("Connected; waiting for a fresh frame sample.");
    const auto baseline = Latest(state).revision;
    std::optional<Activity> previous;
    std::optional<Pending> pending;
    bool mayBeVisible = false, hasSent = false;
    ULONGLONG lastSend = 0;
    auto sendActivity = [&](const std::optional<Activity>& activity, DWORD timeout = 1000) {
        const auto nonce = std::to_string(++serial);
        const auto payload = ActivityJson(activity, nonce);
        budget.Record();
        pending = Pending{nonce, GetTickCount64(), !activity};
        if (activity) mayBeVisible = true;
        Send(pipe, 1, payload, timeout);
        previous = activity;
        hasSent = true;
        lastSend = GetTickCount64();
    };
    auto finalClear = [&]() noexcept {
        if (!mayBeVisible) return;
        try {
            // Shutdown can race a normal clear. Await that clear's ACK instead
            // of spending another budget slot and closing its connection early.
            if (!pending || !pending->clear) {
                if (!budget.Available(true)) return;
                sendActivity(std::nullopt, 250);
            }
            const auto deadline = GetTickCount64() + 250;
            while (GetTickCount64() < deadline) {
                if (auto frame = frames.Next(pipe)) {
                    if (Control(pipe, *frame)) continue;
                    auto doc = Parse(frame->payload);
                    auto* root = yyjson_doc_get_root(doc.get());
                    if (Text(root, "nonce") == pending->nonce && Text(root, "cmd") == "SET_ACTIVITY") {
                        CheckError(root);
                        mayBeVisible = false;
                        break;
                    }
                } else Sleep(5);
            }
        } catch (...) { Diagnostic("Final clear was not acknowledged."); }
    };
    try {
        while (!Stopping(state)) {
            // Bound input work per iteration so ping floods cannot starve freshness checks.
            for (unsigned count = 0; count != 16 && !Stopping(state); ++count) {
                auto frame = frames.Next(pipe);
                if (!frame) break;
                if (Control(pipe, *frame)) continue;
                auto doc = Parse(frame->payload);
                auto* root = yyjson_doc_get_root(doc.get());
                if (pending && Text(root, "nonce") == pending->nonce) {
                    CheckError(root);
                    if (Text(root, "cmd") == "SET_ACTIVITY") {
                        if (pending->clear) mayBeVisible = false;
                        pending.reset();
                    }
                }
            }
            if (Stopping(state)) break;
            if (pending && GetTickCount64() - pending->sentAt > 5000) throw std::runtime_error("Discord ACK timed out");
            const auto sample = Latest(state);
            const bool close = !sample.enabled || sample.applicationId != applicationId;
            auto desired = !close && sample.revision > baseline ? sample.activity : std::nullopt;
            if (close && !mayBeVisible && !pending) break;
            if (!hasSent || desired != previous) {
                const bool clear = !desired;
                const bool significant = !hasSent || !previous || !desired || desired->significantKey != previous->significantKey;
                if ((clear || significant || GetTickCount64() - lastSend >= 15000) && (!pending || clear)) {
                    if (budget.Available(clear)) {
                        // Recheck coalescing before IO; no game lock is held during a pipe write.
                        if (Latest(state).revision != sample.revision) continue;
                        sendActivity(desired);
                    } else if (clear && mayBeVisible) throw std::runtime_error("Clear budget exhausted; disconnecting");
                }
            }
            WaitForSingleObject(state.stop, 25);
        }
        finalClear();
    } catch (...) { finalClear(); throw; }
}

DWORD WINAPI Worker(void* context) noexcept
{
    try {
        auto& state = *static_cast<Shared*>(context);
        Budget budget;
        uint64_t serial = 0;
        DWORD backoff = 1000;
        while (!Stopping(state)) {
            DWORD retry = backoff;
            try {
                const auto sample = Latest(state);
                if (!sample.enabled || !sample.activity) { WaitForSingleObject(state.stop, 50); continue; }
                Handle pipe(Connect());
                if (!pipe) throw std::runtime_error("Discord desktop IPC unavailable");
                RunConnection(state, pipe.value, sample.applicationId, budget, serial);
                backoff = 1000;
                continue;
            } catch (const RpcError& error) { retry = error.retryMs; Diagnostic("Discord rejected an update; reconnecting after backoff."); }
              catch (...) { Diagnostic("Discord disconnected or unavailable; reconnecting after backoff."); }
            if (WaitForSingleObject(state.stop, retry) == WAIT_OBJECT_0) break;
            backoff = std::min<DWORD>(backoff * 2, 30000);
        }
    } catch (...) { Diagnostic("Discord worker could not allocate its state."); }
    return 0;
}

bool Start(Shared& state)
{
    if (state.thread) return true;
    if (state.stopping) return false;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Worker), &module)) return false;
    state.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state.stop) return false;
    state.thread = CreateThread(nullptr, 0, Worker, &state, 0, nullptr);
    if (!state.thread) { CloseHandle(state.stop); state.stop = nullptr; return false; }
    return true;
}
}

bool IsValidApplicationId(std::string_view value) noexcept
{
    if (value.size() < 17 || value.size() > 20) return false;
    uint64_t number = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<unsigned>(ch - '0');
        if (number > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        number = number * 10 + digit;
    }
    return number != 0;
}

std::optional<Activity> Format(std::string_view gameId, const Snapshot& input, int64_t nowUnixSeconds)
{
    if (input.phase == Phase::Unknown || input.phase == Phase::Loading) return std::nullopt;
    Snapshot snapshot = input;
    if (gameId == "th09") {
        snapshot.lives.reset(); snapshot.health.reset(); snapshot.opponentLives.reset();
        snapshot.opponentHealth.reset(); snapshot.opponentScore.reset(); snapshot.spell.clear();
    }
    const char* phase = nullptr;
    switch (snapshot.phase) {
    case Phase::Title: phase = "タイトル"; break;
    case Phase::Menu: phase = "メニュー"; break;
    case Phase::Playing: phase = "プレイ中"; break;
    case Phase::Paused: phase = "ポーズ"; break;
    case Phase::GameOver: phase = "ゲームオーバー"; break;
    case Phase::Clear: phase = "クリア"; break;
    case Phase::Ending: phase = "エンディング"; break;
    case Phase::Results: phase = "リザルト"; break;
    default: return std::nullopt;
    }
    const auto title = Limit(snapshot.title.empty() ? "thprac" : snapshot.title);
    if (snapshot.phase == Phase::Title || snapshot.phase == Phase::Menu) {
        const bool demo = snapshot.phase == Phase::Title && snapshot.mode == "デモ";
        return Activity{title, demo ? "デモ再生中" : phase, demo ? "タイトル画面のデモ" : "プレイ開始待ち", demo ? "demo" : phase, {}};
    }
    const auto stats = Join({Count("現在スコア", snapshot.score), Value("残機", snapshot.lives), Value("体力", snapshot.health),
        snapshot.lifePieces.empty() ? "" : "残機欠片 " + snapshot.lifePieces, Value("ボム", snapshot.bombs),
        snapshot.bombPieces.empty() ? "" : "ボム欠片 " + snapshot.bombPieces, Value("Power", snapshot.power)});
    const auto resources = Join({Count("Graze", snapshot.graze, snapshot.stageGraze),
        snapshot.pointItemValue && std::isfinite(*snapshot.pointItemValue) ? "最大得点 " + Number(*snapshot.pointItemValue, true) : "",
        Count("点取得数", snapshot.pointItems, snapshot.stagePointItems)});
    const auto opponent = snapshot.opponent.empty() ? "" : Join({"対戦相手 " + snapshot.opponent,
        Count("相手スコア", snapshot.opponentScore), Value("相手残機", snapshot.opponentLives),
        Value("相手ボム", snapshot.opponentBombs), Value("相手体力", snapshot.opponentHealth)});
    std::optional<int64_t> start;
    std::string elapsed;
    if (snapshot.elapsedSeconds && std::isfinite(*snapshot.elapsedSeconds) && *snapshot.elapsedSeconds >= 0 && *snapshot.elapsedSeconds <= 86400 * 7) {
        const auto seconds = static_cast<int64_t>(*snapshot.elapsedSeconds);
        elapsed = snapshot.elapsedLabel.empty() ? "取得した経過時間" : snapshot.elapsedLabel;
        if (snapshot.phase == Phase::Playing && gameId != "th09" && snapshot.elapsedLabel != "対戦残り") {
            start = (nowUnixSeconds ? nowUnixSeconds : static_cast<int64_t>(std::time(nullptr))) - seconds;
        } else elapsed += " " + std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
    }
    auto details = Join({Join({phase, snapshot.mode}), Join({snapshot.difficulty, snapshot.stage, snapshot.segment, elapsed}),
        !snapshot.character.empty() && (snapshot.equipment == "A" || snapshot.equipment == "B" || snapshot.equipment == "C")
            ? snapshot.character + snapshot.equipment : Join({snapshot.character, snapshot.equipment})}, "\n");
    // Timers, score, gauge/cooldown counters never consume the major-change budget.
    auto significantDetails = Join({phase, snapshot.mode, snapshot.difficulty, snapshot.stage, snapshot.segment, snapshot.character, snapshot.equipment});
    if (gameId == "th18") {
        const auto lastLine = snapshot.equipment.substr(snapshot.equipment.rfind('\n') == std::string::npos ? 0 : snapshot.equipment.rfind('\n') + 1);
        const auto firstLine = FirstLine(snapshot.equipment);
        auto selected = firstLine.starts_with("選択 ") ? firstLine : "";
        // thprac card names can contain spaces in English. Strip only the
        // status suffix emitted by the adapter, retaining the complete name.
        for (const auto suffix : {" 使用中", " 使用可", " 残"}) {
            const auto offset = selected.rfind(suffix);
            if (offset != std::string::npos) selected.resize(offset);
        }
        significantDetails = Join({phase, snapshot.mode, snapshot.difficulty, snapshot.stage, snapshot.segment, snapshot.character, lastLine, selected});
    }
    auto seasonStatus = std::string{};
    if (gameId == "th16" && !snapshot.extra.empty()) {
        const auto first = snapshot.extra.find(" / ");
        const auto last = snapshot.extra.rfind(" / ");
        seasonStatus = first == std::string::npos ? snapshot.extra : Join({snapshot.extra.substr(0, first), snapshot.extra.substr(last + 3)});
    }
    const auto significant = Join({std::string(gameId), significantDetails, Value("L", snapshot.lives), Value("H", snapshot.health),
        Value("B", snapshot.bombs), snapshot.spell, snapshot.opponent, Value("O", snapshot.opponentLives),
        Value("OB", snapshot.opponentBombs), Value("OH", snapshot.opponentHealth), gameId == "th09" ? snapshot.extra : "",
        gameId == "th19" && snapshot.extra.starts_with("取得ラウンド ") ? FirstLine(snapshot.extra) : "", seasonStatus});
    auto state = Join({stats, resources, opponent, snapshot.spell, snapshot.extra}, "\n");
    auto wideDetails = Wide(details), wideState = Wide(state);
    while (wideState.size() > 128 && wideDetails.size() + 1 < 128) {
        const auto available = 128 - wideDetails.size() - 1;
        const auto row = wideState.rfind(L'\n', available), field = wideState.rfind(L" | ", available);
        const auto boundary = row == std::wstring::npos ? field : field == std::wstring::npos ? row : std::max(row, field);
        if (boundary == std::wstring::npos || boundary == 0) break;
        wideDetails += L'\n' + wideState.substr(0, boundary);
        wideState.erase(0, boundary + (wideState[boundary] == L'\n' ? 1 : 3));
    }
    if (wideState.empty()) wideState = L"取得できた状態を表示";
    return Activity{title, Limit(Utf8(wideDetails), true), Limit(Utf8(wideState), true), significant, start};
}

void Initialize(const char* gameId, Capture capture) noexcept
{
    try {
        auto& state = State();
        existingState = &state;
        ExclusiveLock lock(state.lock);
        state.gameId = gameId ? gameId : "";
        state.capture = capture;
        state.latest.reset();
        state.sampledAt = 0;
        ++state.revision;
    } catch (...) { Diagnostic("Discord capture could not be initialized."); }
}

void Tick(bool enabled, const char* applicationId) noexcept
{
    try {
        auto& state = State();
        existingState = &state;
        const auto now = GetTickCount64();
        const std::string id = applicationId ? applicationId : "";
        enabled = enabled && state.capture && IsValidApplicationId(id);
        if (enabled == state.enabled && id == state.applicationId && now - state.sampledAt < 100) return;
        state.sampledAt = now;
        std::optional<Activity> activity;
        if (enabled) {
            try { activity = Format(state.gameId, state.capture()); }
            catch (...) { activity.reset(); }
        }
        {
            ExclusiveLock lock(state.lock);
            if (state.stopping) return;
            state.enabled = enabled;
            state.applicationId = id;
            state.latest = std::move(activity);
            state.observedAt = now;
            ++state.revision;
        }
        if (enabled) Start(state);
    } catch (...) {
        // Even allocation failures must not escape into the host game's frame.
        if (existingState) {
            ExclusiveLock lock(existingState->lock);
            existingState->latest.reset();
            ++existingState->revision;
        }
    }
}

void RequestStop() noexcept
{
    if (existingState && existingState->stop) SetEvent(existingState->stop);
}

void Shutdown() noexcept
{
    if (!existingState) return;
    auto& state = *existingState;
    {
        ExclusiveLock lock(state.lock);
        state.stopping = true;
        state.latest.reset();
        ++state.revision;
    }
    RequestStop();
    // A timed-out worker retains process-lifetime state and a pinned module.
    if (state.thread) WaitForSingleObject(state.thread, 2500);
}

#ifdef THPRAC_DISCORD_TESTS
namespace Testing { void SetPipeName(const wchar_t* name) { testPipeName = name ? name : L""; } }
#endif
}
