-- Sample application state as stored by schema version 1.
--
-- Loaded into a database created with migrations up to version 1, then opened
-- with the current build: protects reading and upgrading v1 databases. Never
-- edit this file; add a new vN_sample.sql when the schema changes.
--
-- All paths and names are placeholders (C:/Users/Example), not real user data.

INSERT INTO settings (key, value) VALUES
    ('ui.theme', 'dark'),
    ('ui.window.width', '1440'),
    ('ui.window.maximized', 'false'),
    ('stellaris.activePlaysetId', '1');

INSERT INTO game_installations (game_key, install_path, user_data_path, executable_path,
    default_args_json, distribution, platform, steam_app_id, steam_library_path, version,
    version_label, manually_configured, updated_at) VALUES
    ('stellaris',
     'C:/Program Files (x86)/Steam/steamapps/common/Stellaris',
     'C:/Users/Example/Documents/Paradox Interactive/Stellaris',
     'C:/Program Files (x86)/Steam/steamapps/common/Stellaris/stellaris.exe',
     '["-gdpr-compliant"]', 'steam', 'windows', '281990', 'C:/Program Files (x86)/Steam',
     'v4.5.2', 'Cygnus v4.5.2 (9776)', 0, 1761700000000),
    ('eu4',
     'D:/Games/Europa Universalis IV',
     'C:/Users/Example/Documents/Paradox Interactive/Europa Universalis IV',
     'D:/Games/Europa Universalis IV/eu4.exe',
     '[]', 'manual', 'windows', '', '', '', '', 1, 1761700000000);

INSERT INTO installed_mods (game_key, source, provider_id, name, version, supported_version,
    picture, declared_path, archive, remote_file_id, tags_json, dependencies_json, extra_json,
    content_path, descriptor_path, descriptor_ref, state, problems_json, content_updated_at,
    fingerprint, scanned_at) VALUES
    ('stellaris', 'steam-workshop', '1234567890', 'Example UI Mod', '4.5.*', 'v4.5.*',
     'thumbnail.png', '', '', '1234567890', '["Fixes","Graphics","Overhaul"]', '[]', '[]',
     'C:/Program Files (x86)/Steam/steamapps/workshop/content/281990/1234567890',
     'C:/Users/Example/Documents/Paradox Interactive/Stellaris/mod/ugc_1234567890.mod',
     'mod/ugc_1234567890.mod', 'ready', '[]', 1761000000000, '', 1761700000000),
    ('stellaris', 'local', 'example_local_mod', 'Ünïcødé local mod 日本語', '1.0', 'v4.5.*',
     '', 'C:/Users/Example/Documents/Paradox Interactive/Stellaris/mod/example_local_mod', '', '',
     '[]', '["Example UI Mod"]', '[["replace_path","common/ethics"]]',
     'C:/Users/Example/Documents/Paradox Interactive/Stellaris/mod/example_local_mod',
     'C:/Users/Example/Documents/Paradox Interactive/Stellaris/mod/example_local_mod.mod',
     'mod/example_local_mod.mod', 'broken', '["descriptor.mod is missing"]', 0,
     'sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', 1761700000000);

INSERT INTO playsets (id, game_key, name, created_at, updated_at, deleted_at) VALUES
    (1, 'stellaris', 'Example playset', 1761600000000, 1761650000000, NULL),
    (2, 'stellaris', 'Deleted playset', 1761500000000, 1761550000000, 1761550000000);

INSERT INTO playset_entries (playset_id, position, source, provider_id, enabled, name_hint,
    version_hint, fingerprint) VALUES
    (1, 0, 'steam-workshop', '1234567891', 1, 'Example enabled mod', '', ''),
    (1, 1, 'steam-workshop', '18446744073709551615', 0, 'Example disabled mod', '2.0', ''),
    (1, 2, 'local', 'example_local_mod', 1, 'Ünïcødé local mod 日本語', '1.0',
     'sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'),
    (1, 3, 'steam-workshop', '1234567890', 1, 'Example UI Mod', '', ''),
    (2, 0, 'steam-workshop', '1234567890', 1, 'Example UI Mod', '', '');

INSERT INTO playset_history (id, playset_id, game_key, reason, snapshot_json, created_at) VALUES
    (1, 2, 'stellaris', 'deleted',
     '{"createdAt":1761500000000,"entries":[{"enabled":true,"fingerprint":"","id":"1234567890","nameHint":"Example UI Mod","source":"steam-workshop","versionHint":""}],"format":1,"name":"Deleted playset","updatedAt":1761540000000}',
     1761550000000);

INSERT INTO acquisition_jobs (id, game_key, source, provider_id, playset_id, state, error_code,
    error_message, attempts, bytes_downloaded, bytes_total, created_at, updated_at) VALUES
    (1, 'stellaris', 'steam-workshop', '1234567891', 1, 'download_requested', '', '', 1, 0, 0,
     1761650000000, 1761650001000),
    (2, 'stellaris', 'steam-workshop', '18446744073709551615', 1, 'failed', 'offline',
     'Steam is offline. Reconnect and retry.', 2, 1048576, 4194304, 1761650000000,
     1761650002000);

INSERT INTO launch_history (id, game_key, playset_id, started_at, pid, outcome, detail,
    transaction_id) VALUES
    (1, 'stellaris', 1, 1761660000000, 12345, 'started', '', 'tx-0001');
