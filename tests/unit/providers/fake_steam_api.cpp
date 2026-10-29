// Test double for a game's Steam API library. It exports the flat functions
// pmm-steam-helper resolves, with in-memory behaviour, so the helper can be
// tested end to end without Steam. Written from the public flat API
// signatures; contains no Valve code. Tests copy the built library to a temp
// directory under the accepted name (steam_api64.dll / libsteam_api.so / ...).
//
// PMM_FAKE_STEAM_MODE selects a scenario: ok (default), no_client, init_fail,
// no_user, offline, not_owned.
//
// Items: 111111111 installed; 222222222 subscribable then downloadable
// (progresses on each state query); 333333333 subscribe fails with
// FileNotFound (9); 444444444 SubscribeItem returns an invalid call handle.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#if defined(_WIN32)
#define FAKE_EXPORT extern "C" __declspec(dllexport)
#define FAKE_CALL __cdecl
#else
#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#define FAKE_CALL
#endif

namespace {

struct Item {
    std::uint32_t flags = 0;
    std::uint64_t downloaded = 0;
    std::uint64_t total = 0;
    int progressStep = -1; // >= 0 while a requested download advances
};

struct PendingCall {
    std::uint64_t item = 0;
    int result = 1;
    int pollsLeft = 2;
};

struct State {
    bool initialized = false;
    std::string mode = "ok";
    std::uint32_t appId = 0;
    std::map<std::uint64_t, Item> items;
    std::map<std::uint64_t, PendingCall> calls;
    std::uint64_t nextCall = 1000;
};

State& state() {
    static State s;
    return s;
}

int interfaceToken = 0; // address used as the fake interface pointers

std::string env(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr)
        return {};
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

std::string installPath(std::uint64_t id) {
    return "/fake/\xC3\x9Cn\xC3\xAF" "c\xC3\xB8" "d\xC3\xA9 library/steamapps/workshop/content/" +
           std::to_string(state().appId) + "/" + std::to_string(id);
}

void copyMessage(char* out, const char* text) {
    std::snprintf(out, 1024, "%s", text);
}

} // namespace

FAKE_EXPORT int FAKE_CALL SteamAPI_InitFlat(char* errMsg) {
    State& s = state();
    s.mode = env("PMM_FAKE_STEAM_MODE");
    if (s.mode.empty())
        s.mode = "ok";
    if (s.mode == "no_client") {
        copyMessage(errMsg, "Steam client is not running");
        return 2;
    }
    if (s.mode == "init_fail") {
        copyMessage(errMsg, "fake init failure");
        return 1;
    }
    const std::string appId = env("SteamAppId");
    if (appId.empty() || env("SteamGameId") != appId) {
        copyMessage(errMsg, "SteamAppId/SteamGameId not set");
        return 1;
    }
    s.appId = static_cast<std::uint32_t>(std::strtoul(appId.c_str(), nullptr, 10));
    s.items.clear();
    s.items[111111111] = Item{5, 0, 0, -1};
    s.items[222222222] = Item{0, 0, 0, -1};
    s.initialized = true;
    // The real library logs to stdout like this; the helper must keep it out
    // of the protocol stream.
    std::printf("Setting breakpad minidump AppID = %u\n", s.appId);
    std::fflush(stdout);
    errMsg[0] = '\0';
    return 0;
}

FAKE_EXPORT void FAKE_CALL SteamAPI_Shutdown() {
    state().initialized = false;
}

FAKE_EXPORT void FAKE_CALL SteamAPI_RunCallbacks() {}

FAKE_EXPORT bool FAKE_CALL SteamAPI_IsSteamRunning() {
    return state().mode != "no_client";
}

FAKE_EXPORT std::int32_t FAKE_CALL SteamAPI_GetHSteamUser() {
    return state().mode == "no_user" ? 0 : 1;
}

FAKE_EXPORT void* FAKE_CALL SteamAPI_SteamUGC_v017() {
    return state().initialized ? &interfaceToken : nullptr;
}
FAKE_EXPORT void* FAKE_CALL SteamAPI_SteamUtils_v010() {
    return state().initialized ? &interfaceToken : nullptr;
}
FAKE_EXPORT void* FAKE_CALL SteamAPI_SteamUser_v023() {
    return state().initialized ? &interfaceToken : nullptr;
}
FAKE_EXPORT void* FAKE_CALL SteamAPI_SteamApps_v008() {
    return state().initialized ? &interfaceToken : nullptr;
}

FAKE_EXPORT std::uint32_t FAKE_CALL SteamAPI_ISteamUGC_GetItemState(void*, std::uint64_t id) {
    auto& items = state().items;
    const auto it = items.find(id);
    if (it == items.end())
        return 0;
    Item& item = it->second;
    const std::uint32_t reported = item.flags;
    if (item.progressStep >= 0) {
        // pending -> downloading 0% -> 50% -> installed
        ++item.progressStep;
        if (item.progressStep == 1) {
            item.flags = 1 | 16;
            item.total = 1000;
        } else if (item.progressStep == 2) {
            item.downloaded = 500;
        } else {
            item.flags = 1 | 4;
            item.downloaded = 1000;
            item.progressStep = -1;
        }
    }
    return reported;
}

FAKE_EXPORT bool FAKE_CALL SteamAPI_ISteamUGC_GetItemInstallInfo(void*, std::uint64_t id,
                                                                 std::uint64_t* sizeOnDisk,
                                                                 char* folder,
                                                                 std::uint32_t folderSize,
                                                                 std::uint32_t* timestamp) {
    const auto& items = state().items;
    const auto it = items.find(id);
    if (it == items.end() || (it->second.flags & 4) == 0)
        return false;
    const std::string path = installPath(id);
    if (folderSize <= path.size())
        return false;
    std::memcpy(folder, path.c_str(), path.size() + 1);
    *sizeOnDisk = 4096;
    *timestamp = 1700000000;
    return true;
}

FAKE_EXPORT bool FAKE_CALL SteamAPI_ISteamUGC_GetItemDownloadInfo(void*, std::uint64_t id,
                                                                  std::uint64_t* downloaded,
                                                                  std::uint64_t* total) {
    const auto& items = state().items;
    const auto it = items.find(id);
    if (it == items.end() || it->second.total == 0)
        return false;
    *downloaded = it->second.downloaded;
    *total = it->second.total;
    return true;
}

FAKE_EXPORT std::uint64_t FAKE_CALL SteamAPI_ISteamUGC_SubscribeItem(void*, std::uint64_t id) {
    State& s = state();
    if (id == 444444444)
        return 0;
    const std::uint64_t call = s.nextCall++;
    PendingCall pending;
    pending.item = id;
    pending.result = id == 333333333 ? 9 : 1;
    s.calls[call] = pending;
    return call;
}

FAKE_EXPORT bool FAKE_CALL SteamAPI_ISteamUGC_DownloadItem(void*, std::uint64_t id, bool) {
    auto& items = state().items;
    const auto it = items.find(id);
    if (it == items.end() || (it->second.flags & 1) == 0)
        return false;
    it->second.flags |= 32;
    it->second.progressStep = 0;
    return true;
}

FAKE_EXPORT std::uint32_t FAKE_CALL SteamAPI_ISteamUGC_GetNumSubscribedItems(void*, bool) {
    std::uint32_t n = 0;
    for (const auto& [id, item] : state().items)
        n += (item.flags & 1) ? 1u : 0u;
    return n;
}

// The helper must never call this; crashing makes any regression visible.
FAKE_EXPORT std::uint64_t FAKE_CALL SteamAPI_ISteamUGC_UnsubscribeItem(void*, std::uint64_t) {
    std::fprintf(stderr, "fake steam api: UnsubscribeItem called\n");
    std::abort();
}

FAKE_EXPORT bool FAKE_CALL SteamAPI_ISteamUtils_IsAPICallCompleted(void*, std::uint64_t call,
                                                                   bool* failed) {
    auto& calls = state().calls;
    const auto it = calls.find(call);
    if (it == calls.end()) {
        *failed = true;
        return false;
    }
    *failed = false;
    if (it->second.pollsLeft > 0) {
        --it->second.pollsLeft;
        return false;
    }
    return true;
}

FAKE_EXPORT bool FAKE_CALL SteamAPI_ISteamUtils_GetAPICallResult(void*, std::uint64_t call,
                                                                 void* out, int size,
                                                                 int expected, bool* failed) {
    State& s = state();
    const auto it = s.calls.find(call);
#if defined(_WIN32)
    constexpr int kExpectedSize = 16;
#else
    constexpr int kExpectedSize = 12;
#endif
    if (it == s.calls.end() || expected != 1313 || size != kExpectedSize) {
        *failed = true;
        return false;
    }
    unsigned char buffer[16] = {};
    const std::int32_t result = it->second.result;
    const std::uint64_t id = it->second.item;
    std::memcpy(buffer, &result, sizeof result);
    std::memcpy(buffer + (kExpectedSize == 16 ? 8 : 4), &id, sizeof id);
    std::memcpy(out, buffer, static_cast<std::size_t>(size));
    if (result == 1) {
        Item& item = s.items[id];
        item.flags |= 1;
    }
    s.calls.erase(it);
    *failed = false;
    return true;
}

FAKE_EXPORT std::uint32_t FAKE_CALL SteamAPI_ISteamUtils_GetAppID(void*) {
    return state().appId;
}

FAKE_EXPORT bool FAKE_CALL SteamAPI_ISteamUser_BLoggedOn(void*) {
    return state().mode != "offline";
}

FAKE_EXPORT bool FAKE_CALL SteamAPI_ISteamApps_BIsSubscribedApp(void*, std::uint32_t appId) {
    return state().mode != "not_owned" && appId == state().appId;
}
