# Releasing

How a version gets from `main` to GitHub Releases. The work is done by the
[`Release` workflow](../.github/workflows/release.yml); this page is the
procedure for the repository owner. The workflow refuses to run a release for
anyone else, and it publishes nothing for pull requests.

**A dry run has passed on GitHub (2026-10-30), but the publish step has not run**,
and no update between two published releases has happened yet. A pre-release is not offered by the update button, so
the first real test of the button needs two normal releases. Treat the first
release as the test of this page.

What a release contains:

| Asset | For |
| --- | --- |
| `ParadoxModManager-<version>-windows-x86_64-setup.exe` | Installer, also what the application's update button downloads |
| `ParadoxModManager-<version>-windows-x86_64.zip` | Portable package |
| `SHA256SUMS.txt` | `<lowercase hex>  <file name>` per line, one for every other asset; the application verifies the installer against it |

The tag is `v<major>.<minor>.<patch>`. The version comes from
`project(VERSION)` in the root `CMakeLists.txt`, and the workflow fails if the
two differ.

## Procedure

1. **Bump the version** in `CMakeLists.txt` (`project(ParadoxModManager VERSION
   x.y.z ...)`).
2. **Update `CHANGELOG.md`**: move the entries under `## [Unreleased]` to a new
   section `## [x.y.z] - YYYY-MM-DD`. That section becomes the release notes.
   The `## [0.1.0] - planned` section is a to-do list, not notes: rewrite it
   before releasing 0.1.0. A version without a section still releases, with
   GitHub's generated notes and a workflow warning.
3. **Commit to `main`** and wait for CI to pass.
4. **Dry run**: GitHub > Actions > Release > Run workflow, branch `main`, the
   version, *dry run* ticked. See below for what it does.
5. **Release**: the same with *dry run* off. Tick *prerelease* for a test
   release; it does not become the "latest" release, so the update button does
   not offer it.
6. **Verify** the result (see below).

Pushing a tag `vx.y.z` that points at a commit on `main` does the same as step 5
without the dialog. Prefer the dialog: it also creates the tag.

## What the workflow does

| Job | What it does |
| --- | --- |
| Validate release request | The version is `x.y.z` and equals `project(VERSION)`; the run was started by the owner; the commit is on `main`; no release with that tag exists; for a dispatched run, the tag does not exist yet. Warns if `CHANGELOG.md` has no section. |
| CI | The whole of [`ci.yml`](../.github/workflows/ci.yml): build and test with MSVC, ASan, and clang-cl warnings and UBSan on the Qt-free libraries, format. |
| Package (Windows) | `release` preset without tests, `cpack`, the installer, followed by [`smoke-test-installer.ps1`](../packaging/windows/smoke-test-installer.ps1) (silent installs to a temp folder: expected files, uninstall entry and version, another program's folders left alone, failed upgrades that leave the previous version working, a working upgrade, uninstall leaves nothing). Uploads the packages as workflow artifacts. |
| Publish GitHub release | Needs all of the above. Generates and verifies `SHA256SUMS.txt`, creates a **draft**, uploads every asset, compares names and sizes, and only then flips the draft to published. The tag is created by that flip. |

The publish job is the only one with `contents: write`. Because the release stays
a draft until it is complete, the `releases/latest` API that the update check
reads can never see a release with missing assets.

The notes always end with a fixed note: the Windows installer is unsigned, so
SmartScreen may warn.

**Dry run**: runs everything except the publish job. The packages are artifacts
of the workflow run (30 days): download them from the run's summary page and try
them. Nothing appears under Releases and no tag is created.

**Pull requests** that change `packaging/**`, `cmake/Packaging.cmake` or
`release.yml` run the package job and the smoke test only; CI runs for them
through its own workflow as usual.

If a run fails after creating the draft, the draft stays (invisible to the
public, without a tag). Fix the cause and run the workflow again: it deletes
drafts with the same tag before creating a new one.

## Verifying a release

```sh
gh release view vx.y.z                                # assets, not a draft
gh api repos/Giorgi-Apriashvili/pdx-mod-manager/releases/latest --jq .tag_name

gh release download vx.y.z --dir check
(cd check && sha256sum --check SHA256SUMS.txt)
```

Then install from the downloaded setup (SmartScreen: More info > Run
anyway), start the application, and check the version it shows. From an
installed older version, check for updates and use the update button: the
application closes, the installer's progress window appears, and the new version
starts by itself.

## Withdrawing a bad release

Users who already updated keep what they have, so prefer publishing a fixed
version: bump the patch number and release again. That is also what moves them
off the bad version, through the update button.

To stop further downloads right away, either of:

```sh
# Keeps the release and its tag visible, but it stops being "latest":
gh release edit vx.y.z --prerelease

# Removes it; the previous release becomes "latest" again:
gh release delete vx.y.z --cleanup-tag --yes
```

Never reuse a version number for different contents: release the next patch
number instead. To retract a version permanently, also say so in `CHANGELOG.md`.

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
