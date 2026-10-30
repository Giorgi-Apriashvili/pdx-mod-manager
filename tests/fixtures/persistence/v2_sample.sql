-- Sample application state as stored by schema version 2.
--
-- Loaded into a database created with migrations up to version 2, then opened
-- with the current build: protects reading and upgrading v2 databases. Never
-- edit this file; add a new vN_sample.sql when the schema changes.
--
-- All paths and names are placeholders (C:/Users/Example), not real user data.

INSERT INTO settings (key, value) VALUES
    ('ui.theme', 'dark');

INSERT INTO game_installations (game_key, install_path, user_data_path, executable_path,
    default_args_json, distribution, steam_app_id, steam_library_path, version,
    version_label, manually_configured, updated_at) VALUES
    ('stellaris',
     'C:/Program Files (x86)/Steam/steamapps/common/Stellaris',
     'C:/Users/Example/Documents/Paradox Interactive/Stellaris',
     'C:/Program Files (x86)/Steam/steamapps/common/Stellaris/stellaris.exe',
     '["-gdpr-compliant"]', 'steam', '281990', 'C:/Program Files (x86)/Steam',
     'v4.5.2', 'Cygnus v4.5.2 (9776)', 0, 1761700000000);

INSERT INTO playsets (id, game_key, name, created_at, updated_at, deleted_at) VALUES
    (1, 'stellaris', 'Example playset', 1761600000000, 1761650000000, NULL);

INSERT INTO playset_entries (playset_id, position, source, provider_id, enabled, name_hint,
    version_hint, fingerprint) VALUES
    (1, 0, 'steam-workshop', '1234567890', 1, 'Example UI Mod', '4.5.*', '');
