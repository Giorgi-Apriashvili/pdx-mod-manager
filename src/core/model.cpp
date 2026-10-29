#include "core/model.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace pmm {

std::string toString(const ModIdentity& identity) {
    std::string out;
    out.reserve(identity.game.size() + identity.source.size() + identity.id.size() + 2);
    out += identity.game;
    out += ':';
    out += identity.source;
    out += ':';
    out += identity.id;
    return out;
}

OsPlatform currentOsPlatform() noexcept {
#if defined(_WIN32)
    return OsPlatform::Windows;
#elif defined(__APPLE__)
    return OsPlatform::MacOS;
#else
    return OsPlatform::Linux;
#endif
}

const char* toString(OsPlatform platform) noexcept {
    switch (platform) {
    case OsPlatform::Windows:
        return "windows";
    case OsPlatform::Linux:
        return "linux";
    case OsPlatform::MacOS:
        return "macos";
    }
    return "unknown";
}

const char* toString(DistributionSource source) noexcept {
    switch (source) {
    case DistributionSource::Steam:
        return "steam";
    case DistributionSource::Manual:
        return "manual";
    case DistributionSource::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* toString(InstallState state) noexcept {
    switch (state) {
    case InstallState::Ready:
        return "ready";
    case InstallState::NeedsUpdate:
        return "needs_update";
    case InstallState::Downloading:
        return "downloading";
    case InstallState::Broken:
        return "broken";
    }
    return "broken";
}

namespace {
constexpr std::array<std::pair<AcquisitionState, std::string_view>, 10> kAcquisitionNames{{
    {AcquisitionState::Pending, "pending"},
    {AcquisitionState::Subscribing, "subscribing"},
    {AcquisitionState::Subscribed, "subscribed"},
    {AcquisitionState::DownloadRequested, "download_requested"},
    {AcquisitionState::Downloading, "downloading"},
    {AcquisitionState::Installed, "installed"},
    {AcquisitionState::Failed, "failed"},
    {AcquisitionState::Unavailable, "unavailable"},
    {AcquisitionState::NeedsUserAction, "needs_user_action"},
    {AcquisitionState::Cancelled, "cancelled"},
}};
} // namespace

const char* toString(AcquisitionState state) noexcept {
    for (const auto& [s, name] : kAcquisitionNames) {
        if (s == state)
            return name.data();
    }
    return "pending";
}

std::optional<AcquisitionState> acquisitionStateFromString(const std::string& text) noexcept {
    for (const auto& [s, name] : kAcquisitionNames) {
        if (name == text)
            return s;
    }
    return std::nullopt;
}

bool isTerminal(AcquisitionState state) noexcept {
    switch (state) {
    case AcquisitionState::Installed:
    case AcquisitionState::Failed:
    case AcquisitionState::Unavailable:
    case AcquisitionState::NeedsUserAction:
    case AcquisitionState::Cancelled:
        return true;
    default:
        return false;
    }
}

const char* toString(EntryStatus status) noexcept {
    switch (status) {
    case EntryStatus::Ready:
        return "ready";
    case EntryStatus::Missing:
        return "missing";
    case EntryStatus::Pending:
        return "pending";
    case EntryStatus::Downloading:
        return "downloading";
    case EntryStatus::Failed:
        return "failed";
    case EntryStatus::Unavailable:
        return "unavailable";
    case EntryStatus::NeedsUserAction:
        return "needs_user_action";
    case EntryStatus::Broken:
        return "broken";
    }
    return "missing";
}

} // namespace pmm
