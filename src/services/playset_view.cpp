#include "services/playset_view.hpp"

#include "persistence/acquisition_job_repository.hpp"
#include "persistence/installation_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "services/internal/util.hpp"

#include <map>
#include <utility>

namespace pmm::services {
namespace {

bool isMissingStatus(EntryStatus status) {
    switch (status) {
    case EntryStatus::Missing:
    case EntryStatus::Pending:
    case EntryStatus::Downloading:
    case EntryStatus::Failed:
    case EntryStatus::Unavailable:
    case EntryStatus::NeedsUserAction:
        return true;
    case EntryStatus::Ready:
    case EntryStatus::Broken:
        return false;
    }
    return false;
}

} // namespace

PlaysetView buildPlaysetView(const Playset& playset, std::span<const InstalledMod> inventory,
                             std::span<const AcquisitionJob> jobs,
                             const GameInstallation* installation,
                             const games::GameAdapter* adapter) {
    PlaysetView view;
    view.playsetId = playset.id;
    view.game = playset.game;
    view.name = playset.name;

    std::vector<PlaysetEntry> entries = playset.entries;
    for (PlaysetEntry& entry : entries) {
        if (entry.identity.game.empty())
            entry.identity.game = playset.game;
    }
    const InstalledModMap installed = detail::toMap(inventory);
    std::vector<EntryReadiness> readiness = computePlaysetReadiness(entries, installed, jobs);
    view.dependencyIssues = checkDependencies(entries, installed);

    std::map<ModIdentity, AcquisitionJob> latestJobs;
    for (AcquisitionJob& job : detail::latestPerIdentity(jobs))
        latestJobs.emplace(job.identity, std::move(job));

    view.rows.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        PlaysetRow row;
        row.index = i;
        row.entry = entries[i];
        const auto mod = installed.find(entries[i].identity);
        if (mod != installed.end()) {
            row.installed = mod->second;
            if (installation && adapter) {
                for (std::string& warning :
                     adapter->compatibilityWarnings(*installation, mod->second))
                    readiness[i].warnings.push_back(std::move(warning));
            }
        }
        const auto job = latestJobs.find(entries[i].identity);
        if (job != latestJobs.end())
            row.job = job->second;
        if (row.installed && !row.installed->descriptor.name.empty())
            row.displayName = row.installed->descriptor.name;
        else
            row.displayName = detail::entryLabel(entries[i]);
        row.readiness = readiness[i];
        row.blocking = entries[i].enabled && readiness[i].status != EntryStatus::Ready;

        ++view.counts.total;
        if (entries[i].enabled)
            ++view.counts.enabled;
        if (readiness[i].status == EntryStatus::Ready)
            ++view.counts.ready;
        if (isMissingStatus(readiness[i].status))
            ++view.counts.missing;
        if (readiness[i].status == EntryStatus::Broken || !readiness[i].warnings.empty())
            ++view.counts.warning;
        if (row.blocking)
            ++view.counts.blocking;
        view.rows.push_back(std::move(row));
    }
    view.launch = evaluateLaunch(entries, readiness);
    return view;
}

Result<PlaysetView> loadPlaysetView(const ServiceContext& context, std::int64_t playsetId,
                                    std::span<const AcquisitionJob> liveJobs) {
    persistence::PlaysetRepository playsets(context.database);
    auto playset = playsets.get(playsetId);
    if (!playset)
        return std::move(playset).error();
    const GameKey& game = playset.value().game;

    persistence::InventoryRepository inventory(context.database);
    auto mods = inventory.list(game);
    if (!mods)
        return std::move(mods).error();
    persistence::AcquisitionJobRepository jobRepo(context.database);
    auto jobs = jobRepo.listForGame(game);
    if (!jobs)
        return std::move(jobs).error();
    persistence::InstallationRepository installations(context.database);
    auto installation = installations.get(game);
    if (!installation)
        return std::move(installation).error();

    std::vector<AcquisitionJob> allJobs;
    for (const AcquisitionJob& job : liveJobs) {
        if (job.identity.game == game)
            allJobs.push_back(job);
    }
    allJobs = detail::overlayJobs(std::move(jobs).value(), allJobs);
    const GameInstallation* install = installation.value() ? &*installation.value() : nullptr;
    return buildPlaysetView(playset.value(), mods.value(), allJobs, install,
                            context.adapterFor(game));
}

} // namespace pmm::services
