-- Sample application state as stored by schema version 3.
--
-- Loaded into a database created with migrations up to version 3, then opened
-- with the current build: protects reading and upgrading v3 databases. Never
-- edit this file; add a new vN_sample.sql when the schema changes.
--
-- All paths, names, ids and versions are made up (C:/Users/Example), not real
-- user data.

INSERT INTO settings (key, value) VALUES
    ('ui.theme', 'light');

INSERT INTO game_installations (game_key, install_path, user_data_path, executable_path,
    default_args_json, distribution, steam_app_id, steam_library_path, version,
    version_label, manually_configured, updated_at) VALUES
    ('stellaris',
     'C:/Program Files (x86)/Steam/steamapps/common/Stellaris',
     'C:/Users/Example/Documents/Paradox Interactive/Stellaris',
     'C:/Program Files (x86)/Steam/steamapps/common/Stellaris/stellaris.exe',
     '["-gdpr-compliant"]', 'steam', '281990', 'C:/Program Files (x86)/Steam',
     'v4.5.2', 'Cygnus v4.5.2 (9776)', 0, 1762000000000);

INSERT INTO installed_mods (game_key, source, provider_id, name, state, content_path,
    descriptor_ref, content_updated_at, scanned_at, content_time_from_steam, workshop_manifest,
    latest_manifest, latest_updated_at) VALUES
    ('stellaris', 'steam-workshop', '1234567890', 'Example UI Mod', 'needs_update',
     'C:/Program Files (x86)/Steam/steamapps/workshop/content/281990/1234567890',
     'mod/ugc_1234567890.mod', 1761000000000, 1762000000000, 1, '1100000000000000901',
     '1100000000000000902', 1761500000000),
    ('stellaris', 'steam-workshop', '1234567891', 'Example Folder Mod', 'ready',
     'C:/Program Files (x86)/Steam/steamapps/workshop/content/281990/1234567891',
     'mod/ugc_1234567891.mod', 1760000000000, 1762000000000, 0, '', '', 0);

INSERT INTO playsets (id, game_key, name, created_at, updated_at, deleted_at) VALUES
    (1, 'stellaris', 'Example playset', 1761600000000, 1761650000000, NULL);

INSERT INTO playset_entries (playset_id, position, source, provider_id, enabled, name_hint,
    version_hint, fingerprint) VALUES
    (1, 0, 'steam-workshop', '1234567890', 1, 'Example UI Mod', '4.5.*', ''),
    (1, 1, 'steam-workshop', '1234567891', 0, 'Example Folder Mod', '', '');

INSERT INTO acquisition_jobs (id, game_key, source, provider_id, playset_id, state, error_code,
    error_message, attempts, bytes_downloaded, bytes_total, created_at, updated_at, kind,
    target_time_updated, target_manifest, reinstall_step) VALUES
    (1, 'stellaris', 'steam-workshop', '1234567890', 1, 'failed', 'update_not_seen',
     'Steam has not picked up this update yet.', 1, 0, 0, 1761700000000, 1761700060000,
     'update', 1761500000000, '1100000000000000902', 'start'),
    (2, 'stellaris', 'steam-workshop', '1234567891', 1, 'unsubscribing', '', '', 1, 0, 0,
     1761800000000, 1761800010000, 'reinstall', 0, '', 'unsubscribed');
