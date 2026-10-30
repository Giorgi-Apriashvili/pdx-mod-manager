// Embedded schema migrations. Each entry is applied once, in order, inside its
// own transaction, and recorded in schema_migrations. Never edit a released
// migration: append a new one and document it in docs/persistence.md.
//
// Conventions: timestamps are unix milliseconds (INTEGER); provider ids,
// paths and enum values are TEXT; booleans are INTEGER 0/1.

#include "persistence/database.hpp"

namespace pmm::persistence {

namespace {

constexpr const char* kSchemaV1 = R"sql(
CREATE TABLE settings (
    key   TEXT PRIMARY KEY NOT NULL,
    value TEXT NOT NULL
);

CREATE TABLE game_installations (
    game_key            TEXT PRIMARY KEY NOT NULL,
    install_path        TEXT NOT NULL,
    user_data_path      TEXT NOT NULL,
    executable_path     TEXT NOT NULL,
    default_args_json   TEXT NOT NULL DEFAULT '[]',
    distribution        TEXT NOT NULL,
    platform            TEXT NOT NULL,
    steam_app_id        TEXT NOT NULL DEFAULT '',
    steam_library_path  TEXT NOT NULL DEFAULT '',
    version             TEXT NOT NULL DEFAULT '',
    version_label       TEXT NOT NULL DEFAULT '',
    manually_configured INTEGER NOT NULL DEFAULT 0 CHECK (manually_configured IN (0, 1)),
    updated_at          INTEGER NOT NULL
);

-- Inventory cache: replaced per game by every scan.
CREATE TABLE installed_mods (
    game_key           TEXT NOT NULL,
    source             TEXT NOT NULL CHECK (source <> ''),
    provider_id        TEXT NOT NULL CHECK (provider_id <> ''),
    name               TEXT NOT NULL DEFAULT '',
    version            TEXT NOT NULL DEFAULT '',
    supported_version  TEXT NOT NULL DEFAULT '',
    picture            TEXT NOT NULL DEFAULT '',
    declared_path      TEXT NOT NULL DEFAULT '',
    archive            TEXT NOT NULL DEFAULT '',
    remote_file_id     TEXT NOT NULL DEFAULT '',
    tags_json          TEXT NOT NULL DEFAULT '[]',
    dependencies_json  TEXT NOT NULL DEFAULT '[]',
    extra_json         TEXT NOT NULL DEFAULT '[]',
    content_path       TEXT NOT NULL DEFAULT '',
    descriptor_path    TEXT NOT NULL DEFAULT '',
    descriptor_ref     TEXT NOT NULL DEFAULT '',
    state              TEXT NOT NULL,
    problems_json      TEXT NOT NULL DEFAULT '[]',
    content_updated_at INTEGER NOT NULL DEFAULT 0,
    fingerprint        TEXT NOT NULL DEFAULT '',
    scanned_at         INTEGER NOT NULL,
    PRIMARY KEY (game_key, source, provider_id)
);

CREATE TABLE playsets (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    game_key   TEXT NOT NULL,
    name       TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    deleted_at INTEGER
);
CREATE INDEX playsets_by_game ON playsets (game_key, deleted_at);

CREATE TABLE playset_entries (
    playset_id   INTEGER NOT NULL REFERENCES playsets (id) ON DELETE CASCADE,
    position     INTEGER NOT NULL CHECK (position >= 0),
    source       TEXT NOT NULL CHECK (source <> ''),
    provider_id  TEXT NOT NULL CHECK (provider_id <> ''),
    enabled      INTEGER NOT NULL CHECK (enabled IN (0, 1)),
    name_hint    TEXT NOT NULL DEFAULT '',
    version_hint TEXT NOT NULL DEFAULT '',
    fingerprint  TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (playset_id, position),
    UNIQUE (playset_id, source, provider_id)
);

-- Undo path for destructive playset changes. No foreign key: history rows
-- must survive the playset they describe.
CREATE TABLE playset_history (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    playset_id    INTEGER NOT NULL,
    game_key      TEXT NOT NULL,
    reason        TEXT NOT NULL,
    snapshot_json TEXT NOT NULL,
    created_at    INTEGER NOT NULL
);
CREATE INDEX playset_history_by_playset ON playset_history (playset_id, created_at);
CREATE INDEX playset_history_by_game ON playset_history (game_key, created_at);

CREATE TABLE acquisition_jobs (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    game_key         TEXT NOT NULL,
    source           TEXT NOT NULL CHECK (source <> ''),
    provider_id      TEXT NOT NULL CHECK (provider_id <> ''),
    playset_id       INTEGER REFERENCES playsets (id) ON DELETE SET NULL,
    state            TEXT NOT NULL,
    error_code       TEXT NOT NULL DEFAULT '',
    error_message    TEXT NOT NULL DEFAULT '',
    attempts         INTEGER NOT NULL DEFAULT 0,
    bytes_downloaded INTEGER NOT NULL DEFAULT 0,
    bytes_total      INTEGER NOT NULL DEFAULT 0,
    created_at       INTEGER NOT NULL,
    updated_at       INTEGER NOT NULL
);
-- At most one unfinished job per mod identity. The list must equal the
-- non-terminal values of pmm::AcquisitionState (a unit test checks this).
CREATE UNIQUE INDEX acquisition_jobs_one_unfinished
    ON acquisition_jobs (game_key, source, provider_id)
    WHERE state IN ('pending', 'subscribing', 'subscribed', 'download_requested', 'downloading');
CREATE INDEX acquisition_jobs_by_game ON acquisition_jobs (game_key, created_at);

CREATE TABLE launch_history (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    game_key       TEXT NOT NULL,
    playset_id     INTEGER REFERENCES playsets (id) ON DELETE SET NULL,
    started_at     INTEGER NOT NULL,
    pid            INTEGER,
    outcome        TEXT NOT NULL,
    detail         TEXT NOT NULL DEFAULT '',
    transaction_id TEXT NOT NULL DEFAULT ''
);
CREATE INDEX launch_history_by_game ON launch_history (game_key, started_at);
)sql";

constexpr const char* kSchemaV2 = R"sql(
ALTER TABLE game_installations DROP COLUMN platform;
)sql";

} // namespace

const std::vector<Migration>& builtinMigrations() {
    static const std::vector<Migration> migrations{
        {1, "initial schema", kSchemaV1, false},
        {2, "drop game_installations.platform", kSchemaV2, false},
    };
    return migrations;
}

} // namespace pmm::persistence
