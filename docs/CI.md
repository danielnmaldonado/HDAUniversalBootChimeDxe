# Continuous integration and build artifacts

This project uses the [UEFI Driver CI](../.github/workflows/ci.yml) GitHub Actions workflow.

## Triggers

- Commits pushed to `main` and `v*` tags: run host regressions, then build X64 DEBUG and RELEASE EFI images.
- Pull requests targeting `main`: run host regressions **and** build X64 DEBUG/RELEASE EFI. Pull requests do not upload a distributable build artifact.
- **Actions → UEFI Driver CI → Run workflow**: run the full validation and build manually.

## Validation

The Linux job checks the existing shell scripts, parses the project Python tools/tests and runs the complete `unittest` suite. The regression tests include host-side C compilation where supported. Historical audit expectations use `docs/PROJECT-HISTORY.md`, allowing the root README to remain a brief project overview.

The firmware job installs/verifies the NASM assembler (`brew install nasm` when absent) and the Mach-O-to-PE converter (`brew install ocmtoc` if `/usr/local/bin/mtoc` is absent). Both are required by the XCODE5/EDK II build on the hosted macOS Intel runner and are installed only in CI; the local build wrapper is unchanged.

The firmware job uses GitHub's `macos-15-intel` runner, Python 3.11 and the unmodified project build wrapper (`Build-HDAUniversalBootChimeDxe.command`). That wrapper fetches the **pinned** EDK2 `edk2-stable202608` revision, runs its own validation gate, builds both UEFI targets using `XCODE5` and validates each PE/COFF image. If any stage fails, the job fails rather than uploading unverified firmware.

## Artifacts

A successful macOS build on `push` or `workflow_dispatch` uploads a private, 14-day-retained GitHub Actions artifact containing the `Release/` directory, including both EFI images, configuration files, the consolidated build log, an installation test pack, the combined release ZIP and `SHA256SUMS.txt`. On failure, the workflow attempts to upload `Release/Logs/BUILD-COMPILE.log` for diagnosis.

These are temporary Actions artifacts, **not** GitHub Releases. This workflow does not sign EFI binaries, change firmware, install any EFI driver, publish a release or make the repository public. An Actions build is not a physical-device playback test; test on actual UEFI hardware using the existing instructions in `docs/TESTING.md`.

## Requirements and limitations

GitHub Actions must be enabled for the private repository, with sufficient macOS runner minutes. The build requires GitHub-hosted internet access to clone the pinned EDK2 source and its submodules. The root build script and its EDK2 pin remain authoritative; CI does not change toolchain defaults used for local builds. macOS Intel runner availability and hosted-image contents are maintained by GitHub and can change. Do not treat a passing source-test job as proof that a firmware build succeeded; check the **DEBUG and RELEASE EFI** job status.

The original tracked `Release/Logs/BUILD-COMPILE.log` remains in Git history. `.gitignore` only prevents newly untracked generated files from being added automatically, and does not untrack existing files. During a real build the wrapper intentionally replaces the checked-out master log **before** running the host regression gate, so the tests must recognize both the committed historic log and the current-build header; this is not a build-time source regression.

## Historical diagnostics

The committed `Release/Logs/BUILD-COMPILE.log` belongs to the older 1.0.82/R37-R43 development history and is **not** the build log of the latest 1.0.86/R47 source. Similarly, `docs/PROJECT-REPORT.txt` is a retained R37 audit, not a current release report. For the actual tested build, consult the Actions run associated with the commit and download its build log. Do not delete or rewrite the historical evidence.
