#include "steam_api_loader.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define PMM_STEAM_CALL __cdecl
#else
#define PMM_STEAM_CALL
#endif

namespace pmm::steamhelper {
namespace {

namespace fs = std::filesystem;
using providers::HelperFailure;

// Flat API signatures (public steam_api_flat documentation).
using FnBool = bool(PMM_STEAM_CALL*)();
using FnVoid = void(PMM_STEAM_CALL*)();
using FnInitFlat = int(PMM_STEAM_CALL*)(char* errMsg1024); // ESteamAPIInitResult(SteamErrMsg*)
using FnHSteamUser = HSteamUser(PMM_STEAM_CALL*)();
using FnAccessor = void*(PMM_STEAM_CALL*)();
using FnFindOrCreateUserInterface = void*(PMM_STEAM_CALL*)(HSteamUser, const char*);
using FnUgcGetItemState = std::uint32_t(PMM_STEAM_CALL*)(void*, PublishedFileId);
using FnUgcGetItemInstallInfo = bool(PMM_STEAM_CALL*)(void*, PublishedFileId, std::uint64_t*, char*,
                                                      std::uint32_t, std::uint32_t*);
using FnUgcGetItemDownloadInfo = bool(PMM_STEAM_CALL*)(void*, PublishedFileId, std::uint64_t*,
                                                       std::uint64_t*);
using FnUgcSubscribeItem = SteamApiCall(PMM_STEAM_CALL*)(void*, PublishedFileId);
using FnUgcDownloadItem = bool(PMM_STEAM_CALL*)(void*, PublishedFileId, bool);
// Newer SDKs added `bool bIncludeLocallyDisabled`; older ones take only the
// interface. Passing the extra `false` is harmless for both: the caller owns
// argument cleanup (cdecl) and register arguments are simply ignored.
using FnUgcGetNumSubscribedItems = std::uint32_t(PMM_STEAM_CALL*)(void*, bool);
using FnUtilsIsApiCallCompleted = bool(PMM_STEAM_CALL*)(void*, SteamApiCall, bool*);
using FnUtilsGetApiCallResult = bool(PMM_STEAM_CALL*)(void*, SteamApiCall, void*, int, int, bool*);
using FnUtilsGetAppId = std::uint32_t(PMM_STEAM_CALL*)(void*);
using FnUserBLoggedOn = bool(PMM_STEAM_CALL*)(void*);
using FnAppsBIsSubscribedApp = bool(PMM_STEAM_CALL*)(void*, AppId);

template <class Fn> Fn as(void* p) {
    static_assert(sizeof(Fn) == sizeof(void*));
    Fn f;
    // Exported functions arrive as data pointers; copy the bits (same size).
    // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion,bugprone-bitwise-pointer-cast)
    std::memcpy(&f, &p, sizeof f);
    return f;
}

std::string threeDigits(int n) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%03d", n);
    return buf;
}

std::string initFlatResultName(int r) {
    switch (r) {
    case 1:
        return "FailedGeneric";
    case 2:
        return "NoSteamClient";
    case 3:
        return "VersionMismatch";
    default:
        return "result " + std::to_string(r);
    }
}

// Interface version numbers embedded in the library as "<prefix>NNN".
std::set<int> embeddedVersions(std::string_view image, std::string_view prefix, int lowest,
                               int highest) {
    std::set<int> found;
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    for (std::size_t pos = image.find(prefix); pos != std::string_view::npos;
         pos = image.find(prefix, pos + 1)) {
        const std::size_t d = pos + prefix.size();
        if (d + 3 > image.size() || !digit(image[d]) || !digit(image[d + 1]) ||
            !digit(image[d + 2]))
            continue;
        if (d + 3 < image.size() && digit(image[d + 3]))
            continue;
        const int v = (image[d] - '0') * 100 + (image[d + 1] - '0') * 10 + (image[d + 2] - '0');
        if (v >= lowest && v <= highest)
            found.insert(v);
    }
    return found;
}

std::string readImage(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::string data;
    char buf[64 * 1024];
    while (in && data.size() < std::size_t{64} * 1024 * 1024) {
        in.read(buf, sizeof buf);
        data.append(buf, static_cast<std::size_t>(in.gcount()));
    }
    return data;
}

} // namespace

std::string lastLoaderError() {
#if defined(_WIN32)
    const DWORD code = GetLastError();
    wchar_t* text = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string out;
    if (len > 0 && text) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(len), nullptr, 0,
                                          nullptr, nullptr);
        out.resize(static_cast<std::size_t>(n));
        WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(len), out.data(), n, nullptr,
                            nullptr);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
            out.pop_back();
    }
    if (text)
        LocalFree(text);
    return out + " (error " + std::to_string(code) + ")";
#else
    const char* text = dlerror();
    return text ? std::string(text) : std::string("unknown loader error");
#endif
}

SteamApi::~SteamApi() {
    shutdown();
}

void* SteamApi::symbol(const char* name) const {
#if defined(_WIN32)
    const FARPROC p = GetProcAddress(static_cast<HMODULE>(m_library), name);
    void* v = nullptr;
    static_assert(sizeof(p) == sizeof(v));
    // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion,bugprone-bitwise-pointer-cast)
    std::memcpy(&v, &p, sizeof v);
    return v;
#else
    return dlsym(m_library, name);
#endif
}

std::optional<Failure> SteamApi::load(const fs::path& library) {
    std::error_code ec;
    fs::path absolute = fs::absolute(library, ec);
    if (ec)
        absolute = library;
    m_image = readImage(absolute);
#if defined(_WIN32)
    // Altered search path: the library's own dependencies resolve from its folder.
    HMODULE handle = LoadLibraryExW(absolute.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!handle) {
        return Failure{HelperFailure::LibraryLoadFailed,
                       "Could not load the game's Steam API library: " + lastLoaderError()};
    }
    m_library = handle;
#else
    void* handle = dlopen(absolute.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        return Failure{HelperFailure::LibraryLoadFailed,
                       "Could not load the game's Steam API library: " + lastLoaderError()};
    }
    m_library = handle;
#endif
    return std::nullopt;
}

bool SteamApi::resolveAccessor(const char* accessorPrefix, const char* versionPrefix, int highest,
                               int lowest, void*& accessorFn, std::string& version) const {
    for (int v = highest; v >= lowest; --v) {
        const std::string name = accessorPrefix + threeDigits(v);
        if (void* p = symbol(name.c_str())) {
            accessorFn = p;
            version = versionPrefix + threeDigits(v);
            return true;
        }
    }
    // Older libraries without versioned accessors: only use a version string
    // the library itself contains, and only when it is unambiguous.
    if (!m_findOrCreateUserInterface || !m_getHSteamUser || m_image.empty())
        return false;
    const std::set<int> found = embeddedVersions(m_image, versionPrefix, lowest, highest);
    if (found.size() != 1)
        return false;
    accessorFn = nullptr;
    version = versionPrefix + threeDigits(*found.begin());
    return true;
}

std::optional<Failure> SteamApi::resolveSymbols() {
    m_initFlat = symbol("SteamAPI_InitFlat");
    m_initSafe = symbol("SteamAPI_InitSafe");
    m_init = symbol("SteamAPI_Init");
    m_shutdown = symbol("SteamAPI_Shutdown");
    m_runCallbacks = symbol("SteamAPI_RunCallbacks");
    m_isSteamRunning = symbol("SteamAPI_IsSteamRunning");
    m_getHSteamUser = symbol("SteamAPI_GetHSteamUser");
    m_findOrCreateUserInterface = symbol("SteamInternal_FindOrCreateUserInterface");

    m_ugcGetItemState = symbol("SteamAPI_ISteamUGC_GetItemState");
    m_ugcGetItemInstallInfo = symbol("SteamAPI_ISteamUGC_GetItemInstallInfo");
    m_ugcGetItemDownloadInfo = symbol("SteamAPI_ISteamUGC_GetItemDownloadInfo");
    m_ugcSubscribeItem = symbol("SteamAPI_ISteamUGC_SubscribeItem");
    m_ugcDownloadItem = symbol("SteamAPI_ISteamUGC_DownloadItem");
    m_ugcGetNumSubscribedItems = symbol("SteamAPI_ISteamUGC_GetNumSubscribedItems");
    m_utilsIsApiCallCompleted = symbol("SteamAPI_ISteamUtils_IsAPICallCompleted");
    m_utilsGetApiCallResult = symbol("SteamAPI_ISteamUtils_GetAPICallResult");
    m_utilsGetAppId = symbol("SteamAPI_ISteamUtils_GetAppID");
    m_userBLoggedOn = symbol("SteamAPI_ISteamUser_BLoggedOn");
    m_appsBIsSubscribedApp = symbol("SteamAPI_ISteamApps_BIsSubscribedApp");

    std::vector<std::string> missing;
    auto need = [&missing](void* p, const char* name) {
        if (!p)
            missing.emplace_back(name);
    };
    if (!m_initFlat && !m_initSafe && !m_init)
        missing.emplace_back("SteamAPI_InitFlat/InitSafe/Init");
    need(m_shutdown, "SteamAPI_Shutdown");
    need(m_runCallbacks, "SteamAPI_RunCallbacks");
    need(m_ugcGetItemState, "SteamAPI_ISteamUGC_GetItemState");
    need(m_ugcGetItemInstallInfo, "SteamAPI_ISteamUGC_GetItemInstallInfo");
    need(m_ugcGetItemDownloadInfo, "SteamAPI_ISteamUGC_GetItemDownloadInfo");
    need(m_ugcSubscribeItem, "SteamAPI_ISteamUGC_SubscribeItem");
    need(m_ugcDownloadItem, "SteamAPI_ISteamUGC_DownloadItem");
    need(m_utilsIsApiCallCompleted, "SteamAPI_ISteamUtils_IsAPICallCompleted");
    need(m_utilsGetApiCallResult, "SteamAPI_ISteamUtils_GetAPICallResult");

    if (!resolveAccessor("SteamAPI_SteamUGC_v", "STEAMUGC_INTERFACE_VERSION", 21, 10, m_ugcAccessor,
                         m_ugcVersion))
        missing.emplace_back("ISteamUGC accessor (SteamAPI_SteamUGC_v010..v021)");
    if (!resolveAccessor("SteamAPI_SteamUtils_v", "SteamUtils", 12, 7, m_utilsAccessor,
                         m_utilsVersion))
        missing.emplace_back("ISteamUtils accessor (SteamAPI_SteamUtils_v007..v012)");
    // Optional: session facts degrade gracefully without them.
    if (!resolveAccessor("SteamAPI_SteamUser_v", "SteamUser", 25, 17, m_userAccessor,
                         m_userVersion))
        m_userVersion.clear();
    if (!resolveAccessor("SteamAPI_SteamApps_v", "STEAMAPPS_INTERFACE_VERSION", 10, 6,
                         m_appsAccessor, m_appsVersion))
        m_appsVersion.clear();

    m_image.clear();
    m_image.shrink_to_fit();
    if (missing.empty())
        return std::nullopt;
    std::string list;
    for (const auto& name : missing) {
        if (!list.empty())
            list += ", ";
        list += name;
    }
    return Failure{HelperFailure::SymbolsMissing,
                   "The game's Steam API library does not export what the helper needs: " + list};
}

void* SteamApi::acquireInterface(void* accessorFn, const std::string& version) const {
    if (accessorFn)
        return as<FnAccessor>(accessorFn)();
    if (!m_getHSteamUser || !m_findOrCreateUserInterface || version.empty())
        return nullptr;
    const HSteamUser user = as<FnHSteamUser>(m_getHSteamUser)();
    return as<FnFindOrCreateUserInterface>(m_findOrCreateUserInterface)(user, version.c_str());
}

std::optional<Failure> SteamApi::init(AppId appId) {
    if (m_initFlat) {
        m_initName = "SteamAPI_InitFlat";
        char message[1024] = {};
        const int r = as<FnInitFlat>(m_initFlat)(message);
        message[sizeof message - 1] = '\0';
        if (r != 0) {
            return Failure{r == 2 ? HelperFailure::SteamNotRunning : HelperFailure::InitFailed,
                           "SteamAPI_InitFlat failed (" + initFlatResultName(r) + "): " + message};
        }
    } else {
        m_initName = m_initSafe ? "SteamAPI_InitSafe" : "SteamAPI_Init";
        if (!as<FnBool>(m_initSafe ? m_initSafe : m_init)()) {
            const bool running = m_isSteamRunning ? as<FnBool>(m_isSteamRunning)() : true;
            if (!running)
                return Failure{HelperFailure::SteamNotRunning, "Steam is not running."};
            return Failure{HelperFailure::InitFailed,
                           m_initName + " returned false: Steam refused a session for app " +
                               std::to_string(appId) +
                               " (the signed-in account may not own it, or Steam is still "
                               "starting)."};
        }
    }
    m_initialized = true;

    if (m_getHSteamUser && as<FnHSteamUser>(m_getHSteamUser)() == 0)
        return Failure{HelperFailure::NotLoggedOn, "Steam has no signed-in user session."};
    m_ugc = acquireInterface(m_ugcAccessor, m_ugcVersion);
    m_utils = acquireInterface(m_utilsAccessor, m_utilsVersion);
    if (!m_userVersion.empty())
        m_user = acquireInterface(m_userAccessor, m_userVersion);
    if (!m_appsVersion.empty())
        m_apps = acquireInterface(m_appsAccessor, m_appsVersion);
    if (!m_ugc || !m_utils) {
        return Failure{HelperFailure::InitFailed, "Steam did not provide the " +
                                                      (m_ugc ? m_utilsVersion : m_ugcVersion) +
                                                      " interface."};
    }
    if (const auto running = runningAppId(); running && *running != appId) {
        return Failure{HelperFailure::InitFailed,
                       "Steam opened a session for app " + std::to_string(*running) +
                           " instead of " + std::to_string(appId) +
                           " (a steam_appid.txt in the working directory may override it)."};
    }
    return std::nullopt;
}

void SteamApi::runCallbacks() {
    if (m_initialized)
        as<FnVoid>(m_runCallbacks)();
}

void SteamApi::shutdown() {
    if (m_initialized) {
        m_initialized = false;
        as<FnVoid>(m_shutdown)();
    }
}

bool SteamApi::loggedOn() const {
    return m_user && m_userBLoggedOn && as<FnUserBLoggedOn>(m_userBLoggedOn)(m_user);
}

bool SteamApi::ownsApp(AppId appId) const {
    // Without ISteamApps, a successful init is the best evidence: the client
    // only grants an API session for apps the account may run.
    if (!m_apps || !m_appsBIsSubscribedApp)
        return m_initialized;
    return as<FnAppsBIsSubscribedApp>(m_appsBIsSubscribedApp)(m_apps, appId);
}

std::optional<AppId> SteamApi::runningAppId() const {
    if (!m_utils || !m_utilsGetAppId)
        return std::nullopt;
    return as<FnUtilsGetAppId>(m_utilsGetAppId)(m_utils);
}

std::int64_t SteamApi::subscribedCount() const {
    if (!m_ugc || !m_ugcGetNumSubscribedItems)
        return -1;
    return as<FnUgcGetNumSubscribedItems>(m_ugcGetNumSubscribedItems)(m_ugc, false);
}

std::uint32_t SteamApi::itemState(PublishedFileId id) const {
    return as<FnUgcGetItemState>(m_ugcGetItemState)(m_ugc, id);
}

std::optional<InstallInfo> SteamApi::installInfo(PublishedFileId id) const {
    InstallInfo info;
    std::string folder(4096, '\0');
    if (!as<FnUgcGetItemInstallInfo>(m_ugcGetItemInstallInfo)(
            m_ugc, id, &info.sizeOnDisk, folder.data(), static_cast<std::uint32_t>(folder.size()),
            &info.timestamp))
        return std::nullopt;
    folder.back() = '\0';
    folder.resize(
        static_cast<std::size_t>(std::find(folder.begin(), folder.end(), '\0') - folder.begin()));
    info.folder = std::move(folder);
    return info;
}

bool SteamApi::downloadInfo(PublishedFileId id, std::uint64_t& downloaded,
                            std::uint64_t& total) const {
    downloaded = 0;
    total = 0;
    return as<FnUgcGetItemDownloadInfo>(m_ugcGetItemDownloadInfo)(m_ugc, id, &downloaded, &total);
}

SteamApiCall SteamApi::subscribe(PublishedFileId id) {
    return as<FnUgcSubscribeItem>(m_ugcSubscribeItem)(m_ugc, id);
}

bool SteamApi::download(PublishedFileId id, bool highPriority) {
    return as<FnUgcDownloadItem>(m_ugcDownloadItem)(m_ugc, id, highPriority);
}

CallStatus SteamApi::pollSubscribeResult(SteamApiCall call, int& eresult) const {
    bool failed = false;
    if (!as<FnUtilsIsApiCallCompleted>(m_utilsIsApiCallCompleted)(m_utils, call, &failed))
        return failed ? CallStatus::Failed : CallStatus::Pending;
    SubscribeResultPayload payload{};
    failed = false;
    if (!as<FnUtilsGetApiCallResult>(m_utilsGetApiCallResult)(
            m_utils, call, &payload, static_cast<int>(sizeof payload), kSubscribeResultCallbackId,
            &failed) ||
        failed)
        return CallStatus::Failed;
    eresult = payload.result;
    return CallStatus::Completed;
}

} // namespace pmm::steamhelper
