#pragma once

// Late-bound access to a game's own Steam API library (steam_api64.dll) through
// its exported "flat" C API.
//
// No Steamworks SDK header is used: the handful of declarations below are
// written from the public ISteamUGC / steam_api_flat documentation. Flat
// functions take the interface pointer as first argument and use the C
// calling convention (S_CALLTYPE is __cdecl).
//
// Interface pointers MUST match the vtable layout the library's flat functions
// were compiled for. They are therefore obtained, in order of preference, from
// the library's own versioned accessor (SteamAPI_SteamUGC_v0NN, highest first)
// or, for libraries without accessors, from
// SteamInternal_FindOrCreateUserInterface with an interface version string
// that is embedded in the library image itself (never a guessed version: a
// mismatched vtable could invoke an unrelated method such as UnsubscribeItem).

#include "providers/helper_protocol.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace pmm::steamhelper {

using HSteamUser = std::int32_t;
using SteamApiCall = std::uint64_t; // SteamAPICall_t; 0 = k_uAPICallInvalid
using PublishedFileId = std::uint64_t;
using AppId = std::uint32_t;

inline constexpr SteamApiCall kInvalidApiCall = 0;

// RemoteStorageSubscribePublishedFileResult_t = k_iClientRemoteStorageCallbacks (1300) + 13.
inline constexpr int kSubscribeResultCallbackId = 1313;

// Steam callback structs use 8-byte packing (VALVE_CALLBACK_PACK_LARGE).
// GetAPICallResult rejects a buffer whose size differs from the struct the
// client expects.
#pragma pack(push, 8)
struct SubscribeResultPayload {
    std::int32_t result; // EResult
    PublishedFileId publishedFileId;
};
#pragma pack(pop)
static_assert(sizeof(SubscribeResultPayload) == 16);

struct Failure {
    providers::HelperFailure kind = providers::HelperFailure::InitFailed;
    std::string message;
};

struct InstallInfo {
    std::uint64_t sizeOnDisk = 0;
    std::string folder; // UTF-8 as returned by Steam
    std::uint32_t timestamp = 0;
};

enum class CallStatus { Pending, Completed, Failed };

class SteamApi {
public:
    SteamApi() = default;
    ~SteamApi(); // calls shutdown(); the library stays mapped until process exit
    SteamApi(const SteamApi&) = delete;
    SteamApi& operator=(const SteamApi&) = delete;

    // Each step returns a Failure describing why the helper cannot continue.
    std::optional<Failure> load(const std::filesystem::path& library);
    std::optional<Failure> resolveSymbols();
    std::optional<Failure> init(AppId appId);
    void runCallbacks();
    void shutdown();

    // Session facts; valid after init().
    bool loggedOn() const;
    bool ownsApp(AppId appId) const;
    std::optional<AppId> runningAppId() const;
    std::int64_t subscribedCount() const; // -1 when unavailable
    const std::string& ugcInterface() const { return m_ugcVersion; }
    const std::string& initFunction() const { return m_initName; }

    std::uint32_t itemState(PublishedFileId id) const;
    std::optional<InstallInfo> installInfo(PublishedFileId id) const;
    bool downloadInfo(PublishedFileId id, std::uint64_t& downloaded, std::uint64_t& total) const;
    SteamApiCall subscribe(PublishedFileId id);
    bool download(PublishedFileId id, bool highPriority);
    CallStatus pollSubscribeResult(SteamApiCall call, int& eresult) const;

private:
    void* symbol(const char* name) const;
    bool resolveAccessor(const char* accessorPrefix, const char* versionPrefix, int highest,
                         int lowest, void*& accessorFn, std::string& version) const;
    void* acquireInterface(void* accessorFn, const std::string& version) const;

    void* m_library = nullptr;
    std::string m_image; // library bytes, only kept while resolving symbols
    bool m_initialized = false;
    std::string m_initName;

    // Resolved exports (void* to keep the platform headers out of this file).
    void* m_initFlat = nullptr;
    void* m_initSafe = nullptr;
    void* m_init = nullptr;
    void* m_shutdown = nullptr;
    void* m_runCallbacks = nullptr;
    void* m_isSteamRunning = nullptr;
    void* m_getHSteamUser = nullptr;
    void* m_findOrCreateUserInterface = nullptr;

    void* m_ugcAccessor = nullptr;
    void* m_utilsAccessor = nullptr;
    void* m_userAccessor = nullptr;
    void* m_appsAccessor = nullptr;
    std::string m_ugcVersion, m_utilsVersion, m_userVersion, m_appsVersion;

    void* m_ugcGetItemState = nullptr;
    void* m_ugcGetItemInstallInfo = nullptr;
    void* m_ugcGetItemDownloadInfo = nullptr;
    void* m_ugcSubscribeItem = nullptr;
    void* m_ugcDownloadItem = nullptr;
    void* m_ugcGetNumSubscribedItems = nullptr;
    void* m_utilsIsApiCallCompleted = nullptr;
    void* m_utilsGetApiCallResult = nullptr;
    void* m_utilsGetAppId = nullptr;
    void* m_userBLoggedOn = nullptr;
    void* m_appsBIsSubscribedApp = nullptr;

    // Interface instances; valid after init().
    void* m_ugc = nullptr;
    void* m_utils = nullptr;
    void* m_user = nullptr;
    void* m_apps = nullptr;
};

// UTF-8 description of the last OS loader error (GetLastError / dlerror).
std::string lastLoaderError();

} // namespace pmm::steamhelper
