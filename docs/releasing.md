# Releasing

How a version gets from `main` to GitHub Releases. The work is done by the
[`Release` workflow](../.github/workflows/release.yml); this page is the
procedure for the repository owner. The workflow refuses to run a release for
anyone else, and it publishes nothing for pull requests.

**0.1.0 was published with this procedure on 2026-11-06** ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42197165030)), after a
dry run of the same checks ([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42164236292)). 0.1.1 followed on 2026-11-07
([run](https://github.com/Giorgi-Apriashvili/pdx-mod-manager/actions/runs/42313402701)), without a separate dry run because
it changed neither the updater nor the packaging. On 2026-11-07 the owner's
installed 0.1.0 updated itself to 0.1.1, the first real update.

Every release must keep the [update contract](#the-update-contract): copies that are
already installed find and install it only through the values frozen in 0.1.0.

What a release contains:

| Asset | For |
| --- | --- |
| `ParadoxModManager-<version>-windows-x86_64-setup.exe` | Installer, also what the application's update button downloads |
| `ParadoxModManager-<version>-windows-x86_64.zip` | Portable package |
| `SHA256SUMS.txt` | `<lowercase hex>  <file name>` per line, one for every other asset; the application verifies the installer against it |

The tag is `v<major>.<minor>.<patch>`, without leading zeros, and higher than every
earlier release. The version comes from `project(VERSION)` in the root
`CMakeLists.txt`, and the workflow fails if the two differ.

## Procedure

1. **Bump the version** in `CMakeLists.txt` (`project(ParadoxModManager VERSION
   x.y.z ...)`).
2. **Update `CHANGELOG.md`**: move the entries under `## [Unreleased]` to a new
   section `## [x.y.z] - YYYY-MM-DD`. That section becomes the release notes:
   [`changelog-section.sh`](../scripts/changelog-section.sh) takes everything below
   the heading, so the heading's date is not part of the notes. Write each paragraph
   and list item of a section on one line and use full `https://` links: GitHub
   shows a line break inside a paragraph of release notes as a line break, and a
   relative link does not resolve on the release page. A version without a section
   still releases, with GitHub's generated notes and a workflow warning.
3. **Check the update contract**: `bash scripts/check-update-contract.sh` must
   pass (`scripts/check.sh` runs it, and so does the release's CI). If it fails, the release would not
   reach installed copies: put the value back, never the contract file
   ([the update contract](#the-update-contract)).
4. **Re-test the update path when the release touches the updater, the installer,
   the packaging or the Qt version** (`src/services/app_update*`,
   `src/app/core/update_client*`, `src/app/gui/update_controller*`,
   `src/app/core/running_instance*`, `packaging/**`, `cmake/Packaging.cmake`, the
   Qt version in the workflows). Before publishing, install the previous release
   in a disposable Windows user or virtual machine, and run the dry run's setup over
   it with exactly `/SILENT /SP- /NORESTART /PMMUPDATE=1` while the previous version
   is running: it must wait for it, install and start the new version. Right after
   publishing, use "Check for updates" and "Update and restart" in that copy; if
   that fails, [withdraw the release](#withdrawing-a-bad-release). The smoke test
   covers the installer's side on every run, but not a released copy finding,
   verifying and starting the new release, which only a published release can show.
5. **Check locally and push**: `bash scripts/check.sh --full` on the release
   commit, then commit to `main` and push. GitHub runs no CI on pushes; the
   release run repeats it on a clean runner before it builds anything.
6. **Dry run**, when step 4 applies: GitHub > Actions > Release > Run workflow,
   branch `main`, the version, *dry run* ticked. See below for what it does.
7. **Release**: push the tag, which must point at a commit on `main`:

   ```sh
   git tag vx.y.z
   git push origin vx.y.z
   ```

   For a test release, use the dialog of step 6 with *dry run* off and
   *prerelease* ticked instead; it creates the tag itself. A pre-release does not
   become the "latest" release, so the updater does not offer it.
8. **Verify** the result (see below).

## What the workflow does

| Job | What it does |
| --- | --- |
| Validate release request | The version is `x.y.z` without leading zeros and equals `project(VERSION)`; the run was started by the owner; the commit is on `main`; the repository's id is the one installed copies ask (`GITHUB_REPOSITORY_ID`, a warning if its name is no longer the one in `HOMEPAGE_URL`); no release with that tag exists; the version is higher than every published release; for a dispatched run, the tag does not exist yet. Warns if `CHANGELOG.md` has no section. |
| CI | The whole of [`ci.yml`](../.github/workflows/ci.yml): build and test with MSVC, ASan, and clang-cl warnings and UBSan on the Qt-free libraries, format, the update contract. |
| Package (Windows) | `release` preset without tests, `cpack`, the installer; checks that `pmm-app.exe` contains the repository id as text, so a build that asks another repository for updates is never published here; then [`smoke-test-installer.ps1`](../packaging/windows/smoke-test-installer.ps1) (silent installs to temp folders: expected files including the TLS plugin, uninstall entry and version, another program's folders left alone, a folder with a non-ASCII name, the single Start Menu shortcut and the desktop shortcut, refusal to replace a newer version, failed upgrades that leave the previous version working, a failed update run that starts it again, a working upgrade, the update run with the application's exact arguments while the mutex is held, which starts the real `pmm-app.exe`, uninstall leaves nothing). Uploads the packages as workflow artifacts. |
| Publish GitHub release | Needs all of the above. Generates and verifies `SHA256SUMS.txt`, checks the assets and the version against the update contract once more, creates a **draft**, uploads every asset, compares names and sizes, and only then flips the draft to published. A pushed tag exists already; for a run started from the dialog, that flip creates the tag. |

The publish job is the only one with `contents: write`. Because the release stays
a draft until it is complete, the `releases/latest` API that the update check
reads can never see a release with missing assets.

The notes always end with a fixed note: which file to download (the setup, which
updates itself; the ZIP does not), the daily check and where to turn it off, and
that the unsigned setup makes the browser and SmartScreen warn.

The smoke test starts the installed application on the runner, and refuses to run
anywhere else unless it is given `-DisposableMachine`: it uses the real uninstall
registration, mutex, shortcuts and standard profile of the user who runs it.

**Dry run**: runs everything except the publish job. The packages are artifacts
of the workflow run (30 days): download them from the run's summary page and try
them. Nothing appears under Releases and no tag is created.

**CI outside releases**: [`ci.yml`](../.github/workflows/ci.yml) runs for every
release and when started by hand (Actions > CI > Run workflow), never on pushes or
pull requests (owner's decision, 2026-11-07). Pushes are checked locally with
[`scripts/check.sh`](../scripts/check.sh), whose `--full` mode runs the same
checks.

If a run fails after creating the draft, the draft stays (invisible to the
public, without a tag). Fix the cause and run the workflow again: it deletes
drafts with the same tag before creating a new one.

## Verifying a release

```sh
gh release view vx.y.z                                # assets, not a draft
gh api repositories/1439071956/releases/latest --jq .tag_name   # what installed copies see

gh release download vx.y.z --dir check
(cd check && sha256sum --check SHA256SUMS.txt)
bash scripts/check-update-contract.sh assets check x.y.z
```

Then install from the downloaded setup (SmartScreen: More info > Run
anyway), start the application, and check the version it shows. From an
installed older version, check for updates and use the update button: the
application closes, the installer's progress window appears, and the new version
starts by itself.

## The update contract

What every installed copy relies on to find, verify and install a release is frozen
since 0.1.0: the API address by repository id, the tag and version rules, the two
asset names, the checksum file's format, the setup's arguments, the mutex, the
`AppId`, the per-user installation and its layout, and the TLS plugin. The values
and the reason for each are in
[`updates.md`](updates.md#compatibility-contract-frozen-since-010); the file
[`packaging/update-contract.txt`](../packaging/update-contract.txt) holds them, and
[`scripts/check-update-contract.sh`](../scripts/check-update-contract.sh) checks
them:

| Where | What |
| --- | --- |
| CI, "Format check" | The sources still carry every value in their code: comments are removed first (C++, Inno Setup with its preprocessor and Pascal code, PowerShell, the workflows and their scripts, CMake), so a value left only in a comment fails; documentation, such as [`updates.md`](updates.md), is read whole. `scripts/test-update-contract.sh` tests the check itself. |
| Release, validate | The version's grammar; higher than every published release (GitHub's 404 for `releases/latest` is accepted only when nothing but pre-releases exists); the repository id. |
| Release, package | `pmm-app.exe` contains the repository id. The smoke test runs the installer's side of the contract. |
| Release, publish, before the draft | Every asset name and `SHA256SUMS.txt` line against the grammar, LF only, no BOM, every name once, the setup and `SHA256SUMS.txt` present, the setup at most 512 MiB and `SHA256SUMS.txt` at most 64 KiB, the digests; the version again. |

By hand, for a folder of release files: `bash scripts/check-update-contract.sh
assets <folder> <x.y.z>`; for a version: `bash scripts/check-update-contract.sh
published <x.y.z>` (needs `gh` and `GH_REPO`).

Never edit the contract file to make a check pass. A value that has to change can
only be added alongside the old one, see
[`updates.md`](updates.md#compatibility-contract-frozen-since-010).

## Withdrawing a bad release

Roll back by releasing a higher version, never by reusing one. Installed copies
only take a strictly newer version, so users who already updated keep what they
have until a higher version exists, and a version number published once is never
offered to them again. Bump the patch number, fix the problem and release; that
release moves everyone off the bad one, through the updater.

To stop further downloads right away, either of:

```sh
# Keeps the release and its tag visible, but it stops being "latest":
gh release edit vx.y.z --prerelease

# Removes the release; the previous one becomes "latest" again. Keep the tag:
# it blocks the version number from being used again.
gh release delete vx.y.z --yes
```

Prefer the first: the workflow compares a new version with every published
release, pre-releases included, but cannot see a deleted one. Never reuse a version
number for different contents. To retract a version permanently, also say so in
`CHANGELOG.md`.

## Code signing

Not done. The installer and executables are unsigned, so SmartScreen warns
("Windows protected your PC") until the download has built up reputation. The
in-app update is protected by HTTPS and `SHA256SUMS.txt`, which proves integrity against
the release, not who built it.

What signing would take: an Authenticode certificate (a cloud signing service such
as Azure Trusted Signing, or an OV/EV certificate on a hardware token) and its
credentials as repository secrets; then sign `pmm-app.exe` and `pmm-steam-helper.exe`
in the staged tree, and the setup with Inno Setup's `SignTool=` directive, which also
signs the uninstaller. Timestamp every signature.

Signing belongs in the Package job, with the secrets limited to it through a
GitHub environment that the owner must approve.

## Building a package locally

The commands the workflow uses are in [`packaging.md`](packaging.md#building-packages).
[`scripts/changelog-section.sh`](../scripts/changelog-section.sh) `<version>` prints
the notes a version would get.
