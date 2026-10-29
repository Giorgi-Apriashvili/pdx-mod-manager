#include "services/diagnostics.hpp"

#include "core/catalog.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/installation_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/launch_history_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "platform/fs.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <system_error>

namespace pmm::services {
namespace {

namespace fs = std::filesystem;

// "2026-10-29T11:22:33Z" for unix milliseconds.
std::string isoTime(std::int64_t unixMs) {
    using namespace std::chrono;
    const sys_seconds seconds{duration_cast<std::chrono::seconds>(milliseconds(unixMs))};
    const sys_days day = floor<days>(seconds);
    const year_month_day date{day};
    const hh_mm_ss<std::chrono::seconds> time{seconds - day};
    char buffer[80]; // room for any int values, so -Wformat-truncation stays quiet
    std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02uT%02d:%02d:%02dZ",
                  static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
                  static_cast<unsigned>(date.day()), static_cast<int>(time.hours().count()),
                  static_cast<int>(time.minutes().count()),
                  static_cast<int>(time.seconds().count()));
    return buffer;
}

std::string pathState(const fs::path& path) {
    if (path.empty())
        return "(not set)";
    std::error_code ec;
    return platform::pathToUtf8(path) + (fs::exists(path, ec) ? "" : " (missing)");
}

void writeGame(std::ostringstream& out, const ServiceContext& ctx, const GameCatalogEntry& entry,
               const DiagnosticsOptions& options) {
    const GameKey game(entry.key);
    out << "\n== " << entry.displayName << " (" << game << ") ==\n";
    const games::GameAdapter* adapter = ctx.adapterFor(game);
    out << "Adapter: "
        << (adapter ? "available" : (entry.adapterAvailable ? "not loaded" : "not supported yet"))
        << "\n";

    persistence::InstallationRepository installations(ctx.database);
    auto installation = installations.get(game);
    if (!installation) {
        out << "Installation: unreadable (" << installation.error().message << ")\n";
    } else if (!installation.value()) {
        out << "Installation: not found\n";
    } else {
        const GameInstallation& install = *installation.value();
        out << "Installation: " << toString(install.distribution) << ", "
            << toString(install.platform) << ", version "
            << (install.versionLabel.empty() ? install.version : install.versionLabel)
            << (install.manuallyConfigured ? " (manually located)" : "") << "\n";
        out << "  Install path: " << pathState(install.installPath) << "\n";
        out << "  User data: " << pathState(install.userDataPath) << "\n";
        out << "  Executable: " << pathState(install.executablePath) << "\n";
        if (!install.steamAppId.empty())
            out << "  Steam app id: " << install.steamAppId << "\n";
        if (adapter) {
            const Capabilities caps = adapter->capabilities(install);
            out << "  Capabilities: directLaunch=" << (caps.directLaunch ? "yes" : "no")
                << " workshopAcquisition=" << (caps.workshopAcquisition ? "yes" : "no")
                << " localMods=" << (caps.localMods ? "yes" : "no") << "\n";
        }
    }

    persistence::InventoryRepository inventory(ctx.database);
    if (auto mods = inventory.list(game)) {
        std::size_t workshop = 0, local = 0, broken = 0, withProblems = 0;
        for (const InstalledMod& mod : mods.value()) {
            (mod.identity.source == source::SteamWorkshop ? workshop : local)++;
            if (mod.state == InstallState::Broken)
                ++broken;
            if (!mod.problems.empty())
                ++withProblems;
        }
        out << "Inventory: " << mods.value().size() << " mods (" << workshop << " Workshop, "
            << local << " local, " << broken << " broken, " << withProblems << " with problems)\n";
    }

    persistence::PlaysetRepository playsets(ctx.database);
    persistence::SettingsRepository settings(ctx.database);
    if (auto list = playsets.listForGame(game)) {
        out << "Playsets: " << list.value().size();
        auto active = settings.getInt(settings_key::activePlayset(game));
        if (active && active.value()) {
            for (const PlaysetSummary& summary : list.value()) {
                if (summary.id == *active.value())
                    out << " (active: #" << summary.id << ", " << summary.entryCount << " entries, "
                        << summary.enabledCount << " enabled)";
            }
        }
        out << "\n";
    }

    for (const AcquisitionSnapshot& snapshot : options.acquisition) {
        if (snapshot.game != game)
            continue;
        out << "Workshop provider: " << toString(snapshot.providerStatus);
        if (!snapshot.providerMessage.empty())
            out << " - " << snapshot.providerMessage;
        out << "\n";
        if (!snapshot.summary.empty())
            out << "Acquisition: " << snapshot.summary << "\n";
    }

    persistence::AcquisitionJobRepository jobs(ctx.database);
    if (auto list = jobs.listForGame(game); list && !list.value().empty()) {
        std::vector<AcquisitionJob> recent = std::move(list).value();
        std::sort(recent.begin(), recent.end(),
                  [](const AcquisitionJob& a, const AcquisitionJob& b) {
                      return a.updatedAt != b.updatedAt ? a.updatedAt > b.updatedAt : a.id > b.id;
                  });
        if (recent.size() > options.recentJobs)
            recent.resize(options.recentJobs);
        out << "Recent acquisition jobs:\n";
        for (const AcquisitionJob& job : recent) {
            out << "  #" << job.id << " " << job.identity.source << ":" << job.identity.id << " "
                << toString(job.state) << " attempts=" << job.attempts;
            if (job.bytesTotal > 0)
                out << " bytes=" << job.bytesDownloaded << "/" << job.bytesTotal;
            if (!job.errorCode.empty())
                out << " error=" << job.errorCode;
            out << " updated=" << isoTime(job.updatedAt) << "\n";
            if (!job.errorMessage.empty())
                out << "    " << job.errorMessage << "\n";
        }
    }

    persistence::LaunchHistoryRepository history(ctx.database);
    if (auto launches = history.latest(game, static_cast<int>(options.recentLaunches));
        launches && !launches.value().empty()) {
        out << "Recent launches:\n";
        for (const persistence::LaunchRecord& record : launches.value()) {
            out << "  " << isoTime(record.startedAt) << " " << record.outcome;
            if (record.pid)
                out << " pid=" << *record.pid;
            if (record.playsetId)
                out << " playset=#" << *record.playsetId;
            if (!record.transactionId.empty())
                out << " transaction=" << record.transactionId;
            out << "\n";
            if (!record.detail.empty())
                out << "    " << record.detail << "\n";
        }
    }
}

} // namespace

std::string diagnosticsText(const ServiceContext& context, const DiagnosticsOptions& options) {
    std::ostringstream out;
    out << "Paradox Mod Manager diagnostics\n";
    out << "Generated: " << isoTime(context.now()) << "\n";
    out << "App version: " << (options.appVersion.empty() ? "unknown" : options.appVersion) << "\n";
    out << "Share-code protocol: " << options.protocolVersion << "\n";
    out << "OS: " << toString(currentOsPlatform()) << "\n";
    out << "Database: schema " << context.database.schemaVersion() << ", "
        << (context.database.isInMemory() ? std::string("in memory")
                                          : platform::pathToUtf8(context.database.file()))
        << "\n";
    out << "Configuration journal: " << platform::pathToUtf8(context.transactions.journalRoot())
        << "\n";
    if (auto incomplete = context.transactions.listIncomplete()) {
        out << "Interrupted configuration transactions: " << incomplete.value().size() << "\n";
        for (const platform::TransactionRecord& record : incomplete.value())
            out << "  " << record.id << " " << toString(record.state) << " " << record.label
                << "\n";
    } else {
        out << "Interrupted configuration transactions: unreadable (" << incomplete.error().message
            << ")\n";
    }

    for (const GameCatalogEntry& entry : gameCatalog())
        writeGame(out, context, entry, options);

    const RedactionContext redaction =
        options.redaction ? *options.redaction : currentRedactionContext();
    return redact(out.str(), redaction);
}

} // namespace pmm::services
