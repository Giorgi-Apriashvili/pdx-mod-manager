#pragma once

// Small helpers shared by the service implementations. Not part of the
// public services API.

#include "core/dependencies.hpp"
#include "core/model.hpp"
#include "core/result.hpp"
#include "sharing/manifest.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::services::detail {

// ASCII whitespace trimmed from both ends.
std::string trimmed(std::string_view text);

// Longest prefix of well-formed UTF-8 `text` that has at most `maxBytes`
// bytes and does not end inside a character.
std::string_view utf8Prefix(std::string_view text, std::size_t maxBytes);

// `name` trimmed and checked as a playset name: plain UTF-8 text without
// control characters, at most sharing::kMaxPlaysetNameBytes long so that it
// fits into share codes. May be empty.
Result<std::string> checkedPlaysetName(std::string_view name);

// Length-aware variant of core uniquePlaysetName(): `name` + `suffix`, then
// with " (2)", " (3)", ... appended until no existing name matches (ASCII
// case-insensitive). Only `name` is shortened, on a character boundary and
// without trailing whitespace, so that the result never exceeds `maxBytes`.
// An empty name becomes "Imported playset".
std::string uniquePlaysetName(std::string_view name, std::span<const std::string> existingNames,
                              std::string_view suffix = {},
                              std::size_t maxBytes = sharing::kMaxPlaysetNameBytes);

InstalledModMap toMap(std::span<const InstalledMod> inventory);

std::vector<std::string> names(std::span<const PlaysetSummary> playsets);

// nameHint, or the provider id when there is no hint.
std::string entryLabel(const PlaysetEntry& entry);

// Source, then provider id (bytewise), like InventoryRepository::list().
void sortInventory(std::vector<InstalledMod>& inventory);

// "\"Name\"" for one entry, "3 mods" for several.
std::string countLabel(std::span<const PlaysetEntry> entries, std::span<const std::size_t> indices);

// Most recent job per identity (updatedAt, then id), as core/readiness picks it.
std::vector<AcquisitionJob> latestPerIdentity(std::span<const AcquisitionJob> jobs);

// `persisted` with every job of `live` replacing the one with the same id
// (live jobs carry fresher progress than the coalesced database rows).
std::vector<AcquisitionJob> overlayJobs(std::vector<AcquisitionJob> persisted,
                                        std::span<const AcquisitionJob> live);

} // namespace pmm::services::detail
