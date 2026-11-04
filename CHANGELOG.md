# Changelog

## 1.0.87 — R48 HDA transport exact-success safety (local candidate)

- Reproduced a transport boundary false-success result with the R47 `HdaImmediateCommand()` C function: a positive UEFI warning was accepted as completion; a warning from CORB/RIRB fallback could be misreported as `EFI_SUCCESS`. The fault-injection test models non-clean statuses; the current PCI/MMIO access wrappers already normalize their own warnings, so the test validates an additional transport-boundary defense rather than proving that a real controller produced the warning.
- Enforce **exact `EFI_SUCCESS`** at the immediate-command and fallback boundary. A warning from the first transport fails closed **without replaying the same verb**, which could be non-idempotent; a warning from fallback cannot be reported as success. Clear any provisional response on terminal failure.
- Add executable C fault-injection regressions (`Tests/test_transport_status_R48.py`) for warnings, errors, fallback, and response ownership. Preserve the fixed PCM 30% and hardware amp controls, snapshots, generated codec layouts, audio assets, signed-EFI helper, README credits and Revolut funding unchanged.
- Update only active version/build metadata and host test expectations to **1.0.87**; keep historic test and audit evidence as provenance.
- DEBUG/RELEASE EFI firmware compilation, signature with firmware-trusted Secure Boot keys, device-boot verification, acoustic measurements and sustained hardware profiling remain pending.

## 1.0.86 — R47 local analog fanout fail-closed correction (candidate, not yet released)

- Reproduced a safety defect in the R46 analog fanout path using the actual C
  implementation and a fault-injecting host harness: a secondary pin/DAC route
  could be partially enabled before a later codec mutation or 30% hardware
  attenuation command failed. The void fanout caller could then skip that
  candidate while still allowing the selected DMA playback to start.
- Once analog fanout route mutation begins, fail closed on any non-success
  result, including unsuccessful setup, path programming, DAC stream setup or
  amplifier limit programming. Propagate the existing fanout safety barrier
  through pre-start and post-start playback checks, so a partially programmed
  output cannot be treated as harmless. Preserve pre-mutation rejection and
  the existing PCM 30% fallback when the codec genuinely has no capable amp.
- Add executable tests for preflight failures, all post-mutation stages and
  the normal/unsupported attenuation paths, with a failing R46 reproduction
  before the change. Preserve the original waveform, AppleALC layouts, README,
  Revolut funding, signing helper and previous codec compatibility settings.
- Synchronize active release metadata and build-time version checks to
  1.0.86, leaving historical reports, checked-in logs and previous R46 notes
  intact. This source candidate is **not** a firmware-compiled or signed EFI;
  actual boot/Secure Boot tests require trusted keys and real hardware.

## 1.0.85 — R46 local HDA readback and Secure Boot publish fixes (not yet released)

- Reproduced and fixed partial stereo amplifier readback: a successful gain
  read on either channel must be validated independently, even when the other
  GET fails. A confirmed contradiction returns `EFI_COMPROMISED_DATA` after
  the bounded retry on both selected and fanout analog routes. Completely
  unavailable reads remain explicitly **unverified**, never certified safe.
- Reproduced and fixed the optional signing helper's destination-directory
  race: use Python 3's directory-FD-relative `os.link` for exclusive publish,
  so a concurrent directory or symlink-to-directory cannot redirect the output.
  Require an operator-controlled output directory. No key material is bundled.
- Require exact `EFI_SUCCESS` for gain-readback verification; a UEFI warning
  is not proof of the programmed value (confirmed in a failing-then-passing
  executable host regression).
- Fixed a build-gating regression: the build wrapper overwrites its tracked
  historical master log before running tests, while two R45/R46 tests required
  the old 1.0.82 header. Tests now recognize the historic committed log and
  the exact 1.0.85 current-run header, verified with a real wrapper smoke run.
- Added executable regression cases demonstrating the failure on R45 before
  the fix and passing afterward. Existing 30% PCM/HDA attenuation, AppleALC
  layouts, audio assets, Revolut funding, README and bootloader policies are
  unchanged.
- Updated active firmware, build scripts, INIs and release metadata to 1.0.85.
  Historical R44/R45 reports and build logs are retained as evidence.
- This local revision is **not** a compiled/tested EFI Release; signing needs
  operator-supplied UEFI Secure Boot credentials and acceptance on target
  hardware, not macOS ad-hoc `codesign`.

## 1.0.84 — R45 local safety and CI review (not yet released)

- Reject a selected analog path when both amp-gain readbacks succeed but continue reporting the wrong value after a retry; do not silently accept an observed gain that contradicts the 30% HDA attenuation policy.
- Make an irreconcilable fanout gain mismatch reject the parent route before DMA starts (or stop playback during the post-start check). Keep legacy compatibility when the preboot read transport itself is unavailable; log that the gain is **unverified**, not proven.
- Validate X64 DEBUG and RELEASE firmware builds during pull requests. Keep distributable build artifacts restricted to push/manual runs, with read-only workflow permissions. Parse every current Python test and tool during the syntax gate.
- Mark the older checked-in build log and the R37 report as historical in CI documentation. Retain historical bytes for provenance; do not misrepresent them as R45 release results.
- Synchronize active version/build metadata to 1.0.84. Preserve original PCM, AppleALC layout assets, README, default options, donation link and supported bootloaders.
- Added an opt-in, fail-safe UEFI Authenticode signing helper using user-provided trusted keys, with `sbverify` verification and no embedded credentials. macOS ad-hoc `codesign` cannot sign PE/COFF images.
- No claims of successful R45 EFI build, trusted Secure Boot signing, hardware playback, or acoustic calibration without executing those validations.

## 1.0.83 — R44 release maintenance and private funding update

- Verified the full 1.0.82/R43 source against GitHub commit `ed10995` before changing any files.
- Replaced the requested funding destination with the Revolut custom link in `.github/FUNDING.yml`. No PayPal reference was present in the R43 source, so no pre-existing payment component was removed.
- Corrected verified macOS GitHub Actions build environment failures (`nasm: command not found` and `/usr/local/bin/mtoc: No such file or directory`) by installing NASM and the compatible Acidanthera `ocmtoc` converter in CI only when absent. The project's toolchain pin and local build defaults remain unchanged.
- Advanced active firmware, package, build, INI and diagnostic-helper version metadata to 1.0.83 and updated corresponding active regression expectations; retained historic R37/R43 evidence unchanged.
- Preserved the R43 independent 30% PCM and codec amplifier gain policy, the driver behavior, codec layouts, audio assets, bootloader compatibility and README credits without modification.
- Build success, hardware playback and acoustic calibration remain subject to actual EFI validation and device testing; host regression tests do not prove those conditions.

## Unreleased — independent codec-amplifier 30% limit

- Preserve the existing 30% PCM ceiling while independently attenuating a programmable HDA amplifier to approximately 30% linear amplitude (≤−10.5 dB relative to unity) on each selected analog route and additional validated fanout route.
- Reapply the selected codec gain after post-start route initialization. Use live HDA amplifier capabilities and quantization to avoid assuming one gain index fits every layout; preserve the 30% digital fallback where a codec cannot provide sufficient hardware attenuation.
- Add executable regression tests for DAC, pin, indexed input-mixer and insufficient-attenuation cases. Physical acoustic loudness still requires device-specific calibration.
- Do not change the source's 1.0.82 version metadata, default INI options, waveform assets or bootloader behavior.

## 1.0.82 — final audit R37 EDK2/XCODE5 sub-page PE alignment compatibility

- Fixed the real macOS post-compile failure where pinned EDK2/XCODE5 successfully generated `HDAUniversalBootChimeDxe.efi` with `SectionAlignment=FileAlignment=0x20`, but the R35/R36 validator rejected it as `invalid FileAlignment: 0x20`.
- The PE gate now implements the documented sub-page exception: `FileAlignment` may be below 0x200 only when `SectionAlignment < 0x1000` and both values are equal; page-aligned images retain the normal 0x200 minimum.
- Sub-page sections with raw data must additionally have `PointerToRawData == VirtualAddress`, so the fix accepts the valid EDK2 layout without weakening malformed-image detection.
- Added executable positive/negative regression tests modeling the real XCODE5 geometry and preserving all previous PE/COFF hardening.
- Firmware/runtime behavior, HDA routing, DMA ownership, AppleALC data, assets and configuration semantics are unchanged.
- Final R37 host validation passes 267/267 normally and 267/267 with `PYTHONMALLOC=debug` plus Python development mode; shell syntax and Python AST validation also pass.
- Version/build advanced from 1.0.81/R36 to 1.0.82/R37 as required.
- Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)

## 1.0.81 — final audit R36 macOS source-hygiene race closure

- Reproduces the supplied macOS R35 failure where `.DS_Store` appeared after the one-time preflight and was detected only during regression/stress round 5/5.
- Adds `clean_transient_source_metadata()` to remove only transient Finder metadata (`.DS_Store` and AppleDouble `._*`) while pruning `./edk2`; it runs before the strict source-hygiene gate and before every regression round.
- Keeps the source-hygiene gate fail-closed and extends it to reject AppleDouble `._*` files as well as `.DS_Store`, `__pycache__`, `*.pyc` and `*.tmp.*`.
- Adds focused R36 regressions proving cleanup placement, continued strict rejection and synchronized 1.0.81/R36 metadata while preserving the R35 history.
- Firmware/runtime, HDA/DMA behavior, AppleALC data, audio assets, configuration semantics and PE/COFF validation are unchanged in this focused correction.
- Final host validation passes 264/264 normally and 264/264 with `PYTHONMALLOC=debug`/Python development mode. A real two-round wrapper run with seeded `.DS_Store`/AppleDouble metadata passes 528/528, removes all seeded metadata, then reaches only the expected no-EDK2 boundary.
- A five-round wrapper attempt completed the first two 264/264 rounds and entered round 3 before the external harness timeout; it is not represented as a completed five-round pass.
- Version/build advanced from 1.0.80/R35 to 1.0.81/R36 as required.
- Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)

## 1.0.80 — final audit R35 fail-closed analog routing and PE integrity

- Re-audits the complete R34 source and preserves all unrelated HDA policy, generated AppleALC data, PCM/MP3 assets, configuration behavior and prior ownership/lifecycle fixes.
- Rejects fallback route candidates when live `PIN_CAP` cannot be read, preventing a transport failure from persisting a digital endpoint as an analog candidate.
- Makes input-amp prefiltering fail closed when `Audio Widget Capabilities` are unreadable.
- Applies the `AllowDigitalPins` policy using live HDMI/DisplayPort `PIN_CAP` state in the common selected-route classifier, while preserving the explicit `ForcePin` selection exception.
- Adds a common pre-mutation guard to analog path programming so live digital endpoints are rejected before any power/amp/selector/pin/DAC write when digital outputs are disabled.
- Hardens the pre-package PE32+ gate against Certificate Table overlap with mapped raw data, section/header and section/section raw overlap, missing executable-image characteristics, invalid File/Section alignment relationships, and misaligned image/header/section offsets or sizes.
- Adds focused R35 regressions for every reproduced defect above and revalidates the complete accumulated regression suite.
- Final host validation passes 261/261 normally and 261/261 with Python debug allocator/development mode; two same-process complete-suite rounds pass 522/522 with FD/thread counts unchanged and no GC-object growth between measured rounds.
- The retained Release wrapper passes 261/261 before toolchain setup, then stops before compiler invocation because EDK2 is not installed locally and this host cannot resolve `github.com`; no `.efi` or signature is fabricated.
- Version/build advanced from 1.0.79/R34 to 1.0.80/R35 as required.
- Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)

## 1.0.79 — final audit R34 transport truthfulness, rollback closure and hardened PE gate

- Added status-bearing GET_PARAMETER handling on all reproduced decision/mutation paths where a transport error could previously be reinterpreted as zero capabilities, including DAC/PCM selection, power, selectors, pins, digital readiness and hardware amplifier programming.
- Exact AppleALC RevisionID/SubsystemID masks now require successfully read identity fields; unavailable identity can no longer match an exact profile as synthetic zero.
- Fanout selector safety now treats unreadable widget capabilities conservatively, invalidates selector-claim state on failed reads and rejects paths whose intermediate widget type cannot be established.
- Transactional snapshot publication is delayed until capture completes. Pin/EAPD validation and input-amp connection capture fail closed, and Realtek warm-reset power snapshots abort if widget capabilities cannot be read, preventing later mutation without rollback state.
- Hardened the Release PE32+ gate with nonzero section-count enforcement, section raw/virtual bounds, mapped entry-point validation, Optional Header/Data Directory capacity checks and aligned `WIN_CERTIFICATE` chain walking with `dwLength >= 8`.
- Added regression coverage for every R34 defect above plus executable positive/negative PE certificate-table cases.
- Preserved normal successful-path routing, AppleALC generated profiles, PCM/MP3 assets, fixed-volume semantics and all prior R33 behavior outside the reproduced defects.
- Version/build advanced from 1.0.78/R33 to 1.0.79/R34 as required.
- Final host validation passes 253/253 normally and 253/253 with `PYTHONMALLOC=debug`/Python development mode. Eight measured same-process rounds of the 219 tests that do not spawn subprocesses pass 1,752/1,752 after warm-up with FD 4→4, threads 1→1, GC-tracked objects +3, and RSS plateauing at 118,176 KiB across the final four rounds.
- The final release wrapper passes 253/253 in its source gate, then is blocked before compiler invocation because no local EDK2 workspace exists and shell DNS cannot resolve `github.com` to fetch the pinned `edk2-stable202608` tree. The exact failure remains in the single `Release/Logs/BUILD-COMPILE.log`; no `.efi`, partial package, or fabricated signature is represented as successful.
- macOS ad-hoc signing is not applicable to this UEFI PE/COFF DXE image. Firmware Secure Boot signing/trust requires deployment-specific enrolled keys/certificates and cannot be synthesized without the destination trust material.
- Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)

## 1.0.78 — final audit R33 route-read closure, spec-correct codec dump and PE release gate

- Fixed the three remaining runtime paths that consumed zero-initialized `ConfigDefault` after a failed HDA transport: non-forced QuickPath pin enumeration, `HdaAlcIdLayoutSafeFanout`, and `HdaRealtekAppleAlcComboInit`. They now fail closed per candidate; forced QuickPath pins no longer issue the unused ConfigDefault read.
- Corrected diagnostic `GET_SUBSYSTEM_ID (F20h)` from codec root NID 0 to Function Group NIDs, consistent with Intel HDA Function Group semantics. Mandatory codec Vendor/Revision reads now return their EFI transport error to the controller dump instead of emitting false zero identity.
- Added fail-closed structural validation of each generated `.efi` before packaging: `MZ`, in-range `PE\0\0`, X64 (`0x8664`), PE32+ (`0x20b`), EFI Boot Service Driver subsystem (`11`), sane image/header sizes and bounds checking for any Authenticode certificate table.
- Added regression tests for all three fail-open route reads, Function Group subsystem-ID addressing, mandatory dump identity errors, validator ordering and executable positive/negative PE validation. The dynamic validator test also caught and fixed an escaped-PE-signature defect during this audit before release.
- Preserved existing HDA/DMA ownership, cleanup/containment, AppleALC generated tables, PCM/MP3 assets, configuration semantics and normal playback policy outside the verified defects above.
- Version/build advanced from 1.0.77/R32 to 1.0.78/R33 as required.
- Final host validation passes 238/238 normally and 238/238 with Python debug allocator/development mode. Six measured same-process rounds pass 1,428/1,428 after 238 warm-up tests with FD 4→4, threads 1→1 and no progressive RSS/object growth. The release wrapper passes 238/238 in each of five gates before EDK2 bootstrap is blocked by unavailable local EDK2 plus shell DNS failure for `github.com`; it leaves no partial `.efi` output and preserves the exact failure in the single build log.
- macOS ad-hoc signing remains inapplicable because this is a UEFI PE/COFF DXE driver rather than a Mach-O bundle. Secure Boot signing requires deployment-specific enrolled keys/certificates and is not synthesized by the build.
- Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)

## 1.0.77 — final audit R32 direct-NID conformance and fail-closed route state

- Re-audits the complete accumulated R31 source and limits changes to reproduced Intel HDA/addressing, error-path and state-boundary defects.
- Enforces the supported direct 7-bit NID domain (`0x00..0x7F`) in Immediate Command, CORB/RIRB, ConfigData generation/runtime validation, path traversal and selector-claim tables; malformed indirect-NID data fails closed before composition or indexing.
- Rejects sequential Range entries in connection lists and adds explicit bounds before all reduced 128-entry NID-indexed traversal/selector/ConfigData snapshot arrays.
- Makes ConfigDefault-dependent route selection/programming fail closed when the verb transport fails, including AppleALC exact/fallback routing, selected-route classification, fallback candidate insertion, output PinCtl programming and Realtek profile/fanout paths. The primary route reuses its already-validated digital classification, removing one redundant HDA verb.
- Makes mandatory controller-dump MMIO reads error-propagating and names Intel widget type 6h `Volume Knob`.
- Right-sizes widget/path state from the unused 256-entry indirect-address space to the 128 direct NIDs actually supported by the driver; the embedded AppleALC corpus stays within that domain.
- Version/build advanced from 1.0.76/R31 to 1.0.77/R32 as required.
- Final host validation passes 229/229 normally and 229/229 with `PYTHONMALLOC=debug`/`PYTHONDEVMODE=1`/`-X dev`. A clean 12-round same-process stress run passes 2,748/2,748 measured tests after 229 warm-up tests with FD 4→4, threads 1→1 and current RSS within 115,888..117,944 KiB; an extended diagnostic run keeps the Python GC-tracked object count fixed at 147,269 and later drops RSS, providing no evidence of progressive harness leakage.
- The release wrapper passes 229/229 in each of five source-gate rounds. Real EFI compilation remains blocked before compiler invocation because no local EDK2 workspace exists and this host cannot resolve `github.com`; the exact failure is retained in the sole `Release/Logs/BUILD-COMPILE.log`, with no `.efi`, partial ZIP or synthetic signature represented as successful.
- Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)

## 1.0.76 — final audit R31 pre-test hygiene and exact log-cap boundary

- Re-audits the complete accumulated R30 source and preserves HDA/DMA/routing/playback behavior where no new runtime defect was reproduced.
- Adds an explicit source-tree hygiene preflight before test imports, failing closed on `__pycache__`, `*.pyc`, `.DS_Store` and `*.tmp.*` artifacts rather than depending on Python bytecode-suppression flags for correctness.
- Corrects the append-mode log boundary so a diagnostic file already exactly at the 4 MiB cap is recreated before further append, instead of only recreating files strictly larger than the cap.
- Adds R31 regression coverage for the pre-test hygiene ordering, exact log-size boundary and synchronized 1.0.76/R31 release metadata; `find` and `sort`, which the new hygiene gate actually executes, are now explicit preflight dependencies so the gate cannot silently degrade when either tool is absent. The explicitly supported local `./edk2` workspace is pruned from project-source hygiene scans, and the historical tree-hygiene test follows the same boundary, avoiding false failures on toolchain-owned caches.
- Final host validation passes 212/212 normally and 212/212 with `PYTHONMALLOC=debug`/`PYTHONDEVMODE=1`/`-X dev`. Same-process stress passes 2,544/2,544 measured tests after 212 warm-up tests with FD 4→4, threads 1→1 and current RSS within 115,032..116,944 KiB across the 12 measured rounds.
- The release wrapper passes 212/212 in each of five source-gate rounds. Real EFI compilation remains blocked before compiler invocation because no local EDK2 workspace exists and the host cannot resolve `github.com`; the exact failure is retained in the sole `Release/Logs/BUILD-COMPILE.log`, with no `.efi`, partial release ZIP or synthetic signature represented as successful.
- Keeps the pinned `edk2-stable202608` provenance at Tianocore commit `2970e5699ba6267f3384ffab20f96647578aebc8`, independently revalidated against the official Tianocore tag.
- Version/build advanced from 1.0.75/R30 to 1.0.76/R31 as required.
- Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)


## 1.0.75 — final audit R30 release-tooling portability and full-source revalidation

- Re-audits the complete accumulated R29 source against UEFI Specification 2.11 and Intel High Definition Audio Specification Rev. 1.0a, preserving the verified HDA runtime because no new evidence-backed runtime defect was found.
- Fixes the release wrapper so the `zip`/`unzip` commands proven available by preflight are the same PATH-resolved commands used for package creation/integrity checking, eliminating a hard-coded `/usr/bin` mismatch without changing package contents.
- Adds focused R30 regressions for packaging-tool resolution and synchronized 1.0.75/R30 metadata.
- Final host validation passes 209/209 normally and 209/209 with `PYTHONMALLOC=debug`/`-X dev`; extended same-process stress passes 2,508/2,508 measured tests after 209 warm-up tests with FD 4→4 and threads 1→1; after three settling rounds, current RSS remains within 116,276..117,140 KiB and the high-water mark within 116,092..116,860 KiB across nine further rounds.
- The release wrapper passes 209/209 in each of five source-gate rounds. Real EFI compilation is externally blocked before compiler invocation because no local EDK2 workspace exists and this host cannot resolve `github.com` to fetch the pinned `edk2-stable202608` workspace; the exact failure is preserved in the sole `Release/Logs/BUILD-COMPILE.log`, and no `.efi`, partial release ZIP or synthetic signature is represented as successful.
- Version/build advanced from 1.0.74/R29 to 1.0.75/R30 as required.
- Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)

## 1.0.74 — final audit R29 post-playback ownership/RUN-boundary hardening

- Re-audits the complete accumulated R28 source against UEFI Specification 2.11 and Intel High Definition Audio Specification Rev. 1.0a, preserving unrelated routing, volume policy, generated AppleALC data, PCM/MP3 assets and configuration semantics.
- Revalidates the entire prepared output Stream Descriptor immediately before `RUN`, after codec programming, stream-tag scanning and PCI attribute transitions, so late CBL/BDL/FMT/tag/status mutation cannot launch DMA against state no longer owned by this driver.
- Removes the R28 controller-wide `CRST` escalation used solely after a post-stop Stream Descriptor restore/read-back failure. Once `RUN` is proven clear, R29 records the local restore degradation and avoids resetting foreign controller-wide transport/stream state. Safety-critical CRST/PCI bus-master containment remains unchanged for the separate case where stream stop itself cannot be proven.
- Preserves the real post-playback cleanup EFI status in `PostPlaybackCleanupStatus` instead of mapping MMIO/rollback failures onto an unrelated generic UEFI warning code. Successful playback is not replayed on another codec/controller solely because a later rollback failed; the degradation is explicitly logged and exported through `LastStatus`.
- FastBoot publishes Stream Descriptor cleanup degradation before its asynchronous context is released, and retained cleanup paths preserve the same concrete error status for diagnostics/retry.
- Revalidates the pinned `edk2-stable202608` root as Tianocore commit `2970e5699ba6267f3384ffab20f96647578aebc8`.
- Adds focused R29 regression coverage for the final RUN ownership gate, global-reset scope, exact cleanup-status retention, duplicate-playback suppression and FastBoot cleanup diagnostics.
- Final host validation passes 207/207 normally and 207/207 under `PYTHONMALLOC=debug`/`-X dev`; same-process stress passes 828/828 measured tests after 207 warm-up tests with FD 4→4, threads 1→1 and RSS plateauing within 114,928..115,788 KiB.
- The release wrapper passes 207/207 in each of five source-gate rounds. Real EFI compilation remains externally blocked because no local EDK2 workspace exists and the shell cannot resolve `github.com`; no `.efi`, partial release ZIP or synthetic signature is represented as successful, and the exact failure remains in the sole `Release/Logs/BUILD-COMPILE.log`.
- Version/build advanced from 1.0.73/R28 to 1.0.74/R29 as required.
- Credits: ©DNMTechLabs / dnmtechlabs@gmail.com

## 1.0.73 — final audit R28 transactional HDA ownership/conformance hardening

- Re-audits the complete accumulated R27 source against UEFI Specification 2.11 and Intel High Definition Audio Specification Rev. 1.0a, preserving all unrelated audio policy, AppleALC data, PCM/MP3 assets and configuration behavior.
- Immediate Command no longer clears a foreign/pre-existing `IRV` or waits through ambiguous entry-time `ICB`; it fails closed, revalidates transport ownership immediately before command/result transitions, retires only a result owned by this driver, and surfaces pre-existing `CORBSTS.CMEI` instead of hiding a controller fault.
- CORB/RIRB fallback revalidates the complete idle ring snapshot after memory allocation/mapping and before first register mutation; applies current-value read/modify/write semantics to `RsvdP` fields; verifies programmed base/size/pointers and DMA-enable readback; detects late `RIRBOIS`/`CMEI`; and requires controller reset after owned CMEI before returning transport to service.
- Corrects 32-bit-only HDA compatibility: when `GCAP.64OK=0`, reserved/read-only-zero `CORBUBASE`, `RIRBUBASE` and Stream Descriptor `BDLPU` are no longer written. Existing 32-bit address/range validation remains fail-closed.
- Makes Stream Descriptor use transactional: captures/revalidates idle state before mutation, treats all non-zero tags across descriptors as reserved, detects concurrent CTL/CTL2 changes during tag scans, sets bidirectional `DIR=1` for output, read-backs critical descriptor programming and `RUN`, performs Stream Reset before post-run CBL restoration, restores the prior functional descriptor state, and verifies restoration before ownership is released. A restoration failure after DMA stop receives one controller-reset/retry containment attempt.
- Removes a redundant fixed 1 ms stream-preparation stall after the acquisition path became transactionally verified; no timing shortcut was added around required hardware state transitions.
- Adds focused R28 regression coverage for transport ownership, severe ring errors, late overrun/error status, snapshot races, `RsvdP` handling, 32-bit-only upper-base behavior, Stream Descriptor borrowing/restoration, bidirectional direction, tag concurrency and read-back gates.
- Final host validation passes 200/200 regressions normally and 200/200 with `PYTHONMALLOC=debug`/`-X dev`; reduced in-process stress passes 800/800 measured tests after 200 warm-up tests with FD 4→4, threads 1→1 and non-monotonic RSS (111,116 KiB post-warm-up baseline, 114,484 KiB final; measured range 113,668..114,484 KiB). A longer stress attempt hit the environment execution timeout and is not counted as a pass.
- The build wrapper passes 200/200 in each of five source-gate rounds, then the EFI compiler/bootstrap stage is externally blocked because no EDK2 workspace is installed and DNS cannot resolve `github.com`. The sole `Release/Logs/BUILD-COMPILE.log` preserves that exact result; no `.efi`, partial release ZIP or synthetic signed artifact is represented as successful.
- Version/build advanced from 1.0.72/R27 to 1.0.73/R28 as required.
- Credits: ©DNMTechLabs / dnmtechlabs@gmail.com

## 1.0.72 — final audit R27 Intel HDA reset/transport conformance

- Re-audits the complete accumulated R26 source against UEFI Specification 2.11 and Intel High Definition Audio Specification Rev. 1.0a, preserving existing routing, fixed-volume policy, PCM assets, generated AppleALC data, configuration behavior and release diagnostics.
- Hardens controller reset so `CRST` is not asserted while CORB/RIRB DMA or any GCAP-advertised stream descriptor still reports `RUN`; an unclean/foreign active DMA state is refused instead of being destroyed by reset.
- Applies the Intel HDA codec-discovery settle requirement consistently: both the full reset path and the low-latency path that finds an already-running controller wait `HDAU_CODEC_POST_RESET_MIN_US` (521 us) after `CRST=1` is observed before codec status/discovery is consumed.
- Makes Immediate Command and CORB/RIRB transport use mutually exclusive in both directions, preventing fallback from starting CORB/RIRB while `ICB` is busy and preventing Immediate Command use while ring DMA is enabled.
- Repairs recovery for an Immediate Command owned by this driver after either a response-poll MMIO read failure or timeout: clear `ICB`, bounded-poll until `ICB=0`, and clear a racing `IRV` before allowing transport reuse/fallback. A foreign pre-existing busy command remains untouched because its ownership is not known, and abort refuses to interfere if CORB/RIRB becomes active.
- Adds focused R27 regressions for reset quiescence, both 521 us settle paths, owned Immediate Command recovery, Immediate/CORB mutual exclusion and synchronized release metadata. Final host regression passes 181/181 normally and 181/181 under Python debug allocator/development mode.
- In-process stress passes 3,620/3,620 measured tests after 2,172 warm-up tests; file descriptors remain 4 -> 4, threads 1 -> 1, and measured RSS is non-monotonic/stable (114,432 -> 113,100 KiB; range 111,564..114,760 KiB).
- The primary build command passes all 181 tests in each of five source-gate rounds. Real EFI compilation remains externally blocked because no EDK2 workspace is installed and this environment cannot resolve `github.com`; the exact bootstrap failure is preserved in the sole `Release/Logs/BUILD-COMPILE.log`, with no partial EFI/ZIP represented as successful.
- R26/R27 comparison confirms the PCM headers, MP3 asset, generated AppleALC table/generator and testing documentation listed in the release report are byte-for-byte unchanged.
- Version/build advanced from 1.0.71/R26 to 1.0.72/R27 as required.
- Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)

## 1.0.71 — final audit R26 pinned-workspace provenance closure

- Re-audits the complete accumulated R25 source against UEFI Specification 2.11, including SimpleTextInputEx callback ownership/TPL, PCI I/O DMA/map lifetimes, CORB/RIRB containment, stream teardown, error propagation, bounded polling, ExitBootServices behavior, release tooling and artifact hygiene.
- Fixes a reproducibility gap in `Build-HDAUniversalBootChimeDxe.command`: when `EDK2_EXPECTED_COMMIT` is active (including the default `edk2-stable202608` pin), a non-Git EDK2 workspace is now rejected because its root commit cannot be proven. R25 previously emitted only a warning and continued, bypassing the promised commit pin.
- Preserves the prior compatibility path for explicitly custom, unpinned `EDK2_REF` workspaces: if no expected commit is requested, a non-Git workspace remains allowed with a provenance warning. `Setup-EDK2.command` already required Git provenance and is unchanged.
- Leaves the complete HDA runtime C source, PCM/MP3 assets, generated AppleALC data, codec routing policy, configuration behavior and UI-visible behavior unchanged after the renewed runtime audit found no additional evidence-backed correction. The project has no graphical window/live-resize subsystem.
- Adds focused R26 regressions locking pinned non-Git rejection, explicit unpinned compatibility and synchronized release metadata.
- Final host validation passes 174/174 focused regressions in normal mode and 174/174 with `PYTHONMALLOC=debug`/`-X dev`; in-process stress passes 3,480/3,480 measured tests after 522 warmup tests with file descriptors 4 -> 4, threads 10 -> 10, and no monotonic RSS growth (112,060 KiB measured baseline, 112,264 KiB final; measured range 111,316..112,580 KiB). Functional release-tool tests prove pinned non-Git workspaces fail before build with no partial EFI/ZIP while explicitly unpinned custom refs retain compatibility. The build wrapper is required to pass 174/174 in each of five source-gate rounds; the real EFI compiler remains environment-dependent and its exact result is preserved in the single `Release/Logs/BUILD-COMPILE.log`. Shell/Python/C structural checks pass, and all R10-R25 corrections/history remain accumulated.

## 1.0.70 — final audit R25 key-notify failure containment and reproducible EDK2 pin

- Re-audits the complete accumulated R24 source against UEFI Specification 2.11, with emphasis on `EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL`, callback/TPL ownership, event lifetime, DMA teardown, exact-status semantics and release tooling.
- Fixes ambiguous callback ownership after an undocumented `RegisterKeyNotify()` result. UEFI 2.11 documents only `EFI_SUCCESS` and `EFI_OUT_OF_RESOURCES` for registration; R25 therefore keeps `EFI_OUT_OF_RESOURCES` as the retry-safe known failure and permanently contains any other non-success status before closing the deferred event.
- Pins the default `edk2-stable202608` build to the official Tianocore commit `2970e5699ba6267f3384ffab20f96647578aebc8` in both build/setup `.command` scripts, while preserving explicit custom-ref overrides. This prevents a moved or locally shadowed default tag from silently changing the release toolchain.
- Preserves `Build-HDAUniversalBootChimeDxe.command` as the executable transactional DEBUG+RELEASE compiler entry point and retains exactly one consolidated `Release/Logs/BUILD-COMPILE.log`.
- Adds focused R25 regressions for undocumented key-notify status containment, documented resource-failure behavior, immutable default EDK2 provenance and synchronized release metadata.
- The project contains no graphical window/live-resize subsystem; no unrelated UI/window code was added or changed.
- Final host validation passes 171/171 focused regressions in normal mode and 171/171 with `PYTHONMALLOC=debug`/`-X dev`; in-process stress passes 3,420/3,420 measured tests after 513 warmup tests with file descriptors 4 -> 4, threads 10 -> 10, and no monotonic RSS growth (112,684 KiB measured baseline, 113,000 KiB final; measured range 110,108..113,376 KiB). The build wrapper passes 171/171 in each of five source-gate rounds; the real DEBUG/RELEASE EFI compiler stage remains externally blocked because no EDK2 workspace is installed and the container cannot resolve `github.com`, with the exact failure preserved in the single `Release/Logs/BUILD-COMPILE.log`. Shell/Python/C structural checks pass, and all R10-R24 corrections/history remain accumulated.

## 1.0.69 — final audit R24 notification-teardown ownership barrier and build-command validation

- Re-audits the complete accumulated R23 source against UEFI Specification 2.11 and the pinned `edk2-stable202608` interfaces, with renewed review of SimpleTextInputEx notification ownership, event/TPL reentrancy, PCI I/O DMA containment, teardown ordering, release tooling and package hygiene.
- Closes the inverse ownership-transition window around `UnregisterKeyNotify()`: the D-key callback and deferred codec-dump worker are now side-effect-free while notification tokens are being removed, so a callback that preempts lower-TPL firmware teardown cannot publish or rearm work against partially dismantled state.
- Generalizes the R23 registration-only barrier into one notification-ownership mutation barrier covering both `RegisterKeyNotify()` and `UnregisterKeyNotify()`. The barrier spans the complete lower/upper registration transaction and is released only after the resulting token ownership has been classified and published, eliminating both post-return and between-registration gaps. Terminal quarantine/unknown-registration/ExitBootServices gates retain priority, while temporary ownership mutation only performs the existing bounded 1 ms worker rearm.
- Keeps the mutation barrier asserted through both unregister attempts and the optional terminal deferred-event close, publishing completion only after token/event ownership is fully settled. Normal D-key behavior, one-shot semantics and retry behavior are unchanged.
- Retains `Build-HDAUniversalBootChimeDxe.command` as the single primary compilation command requested for the release. Its pinned EDK2 reference, five source-gate rounds, DEBUG+RELEASE build flow, transactional cleanup, staging outside WORKSPACE and ZIP integrity gate were re-audited and functionally exercised with a controlled EDK2-compatible workspace, including injected RELEASE failure and divergent-artifact rejection.
- Adds focused R24 regressions for unregister mutation ordering, callback/worker exclusion, build-command presence/executable/transactional invariants, and synchronized release metadata.
- Final host validation passes 168/168 focused regressions in normal mode and 168/168 with `PYTHONMALLOC=debug`/`-X dev`; in-process stress passes 3,360/3,360 measured tests after 504 warmup tests with file descriptors 4 -> 4, threads 1 -> 1, and no monotonic RSS growth (111,228 KiB measured baseline, 110,792 KiB final; measured range 110,752..112,212 KiB).
- The pinned `edk2-stable202608` build wrapper passes 168/168 in each of five source-gate rounds. The real DEBUG/RELEASE EFI compiler stage remains externally blocked because no EDK2 workspace is installed and the container cannot resolve `github.com`; the exact failure is preserved in the single `Release/Logs/BUILD-COMPILE.log` and is not represented as a successful firmware build.
- Shell syntax, Python AST parsing and C/header lexical structure checks pass; unchanged PCM 48 kHz/44.1 kHz data, MP3 asset, generated AppleALC table/generator and setup/test-pack helpers were verified byte-for-byte against R23.
- Active release metadata is synchronized to 1.0.69-final-audit while preserving the complete R10-R23 changelog history.

## 1.0.68 — final audit R23 key-registration containment and diagnostic-helper activation

- Re-audits the complete accumulated R22 source against UEFI Specification 2.11, with renewed review of `SimpleTextInputEx` callback ownership, event lifetime, PCI I/O DMA ownership, bounded polling, shutdown containment and release tooling.
- Fixes a firmware-contract edge case in `HdaInstallBootScreenDNotify()`: `RegisterKeyNotify()` returning `EFI_SUCCESS` with a NULL `NotifyHandle` can mean a callback was installed without a usable unregister token. R23 records that ownership as unknown and permanently contains the callback for the remainder of the boot.
- Publishes the unknown-registration gate before event teardown, so an untracked lower- or upper-case D callback returns before touching the deferred timer, filesystem, PCI or codec state. If a normal lower-case token exists before an upper-case contract violation, the existing exact-status cleanup still unregisters that known token safely.
- Closes the remaining re-entrant registration window at its source: callbacks are side-effect-free while `RegisterKeyNotify()` is on the stack, and an already-pending worker only rearms until notification ownership is known. If broken firmware then returns `EFI_SUCCESS` with a NULL handle, R23 publishes permanent containment and clears any pending request before teardown.
- Adds focused R23 regressions for unknown registration containment, re-entrant pending-request discard, worker-side terminal containment ordering, known-token cleanup, and synchronized release metadata.
- Fixes `Enable-HDAUCodecDump-NextBoot.command` for the shipped Release precedence: the helper preserves both INIs, builds the entire Debug profile in a sibling staging file, publishes it with one same-directory rename, then atomically enables `ReadDebugConfig=1` in an existing `HDAUChimeRelease.ini`. Even when Release already opts into Debug, an interrupted transformation cannot expose a partially edited Debug file. Repeated runs preserve the first rollback backups, and no synthetic Release profile is created when one is absent.
- Adds functional OpenCore/Clover helper regressions proving Release opt-in, first-backup preservation, duplicate-key normalization, whole-file Debug publication, injected mid-transform failure rollback, temporary/staging-file cleanup and the no-Release compiled-default path.
- Final host validation passes 163/163 focused regressions in normal mode and 163/163 with `PYTHONMALLOC=debug`/`-X dev`; in-process stress passes 3,260/3,260 measured tests after 489 warmup tests with file descriptors 5 -> 5, threads 10 -> 10, and no monotonic RSS growth (112,000 KiB baseline, 112,100 KiB final; measured range 110,820..112,472 KiB).
- The pinned `edk2-stable202608` build wrapper passes 163/163 in each of five source-gate rounds. The real DEBUG/RELEASE EFI compiler stage remains externally blocked because no EDK2 workspace is installed and `github.com` DNS resolution fails; the exact failure is preserved in the single `Release/Logs/BUILD-COMPILE.log` and is not represented as a successful firmware build.
- Preserves every R22 nesting/TPL fix, R21 DMA containment fix, routing policy, PCM data, generated AppleALC tables, configuration behavior, UI-visible behavior and release diagnostic defaults.
- Synchronizes active metadata to 1.0.68-final-audit while retaining the complete R10-R22 history.

## 1.0.67 — final audit R22 nesting-safe key-notify exclusion

- Re-audits the complete accumulated R21 source against UEFI Specification 2.11, including event/TPL semantics, PCI I/O DMA ownership, terminal resource cleanup, exact-status handling and boot-handoff paths.
- Fixes a reentrancy-state hole in the boot-screen D-key notification barrier: the previous boolean active flag could be cleared by an inner/nested notification while an outer key-notify stack frame was still active. R22 tracks notification depth so the deferred `TPL_CALLBACK` worker remains excluded until the outermost callback returns.
- Adds a `MAX_UINTN` overflow fail-safe before entering the nesting counter, avoiding wraparound to zero under pathological firmware recursion.
- Adds focused R22 regressions for nesting-safe enter/leave ordering, worker exclusion and synchronized release metadata.
- Final host validation passes 153/153 focused regressions; the corrected in-process stress run passes 2,295/2,295 measured tests after 459 warmup tests with file descriptors 5 -> 5, threads 1 -> 1 and RSS fixed at 124,968 KiB across the final five measurements.
- The pinned `edk2-stable202608` build wrapper passes 153/153 in each of five source-gate rounds. The real DEBUG/RELEASE EFI compiler stage remains externally blocked in this environment because no EDK2 workspace is installed and `github.com` DNS resolution fails; the exact failure is preserved in the single `Release/Logs/BUILD-COMPILE.log`. The official Tianocore tag resolves to commit `2970e5699ba6267f3384ffab20f96647578aebc8`.
- Preserves all R21 CORB/RIRB cleanup and DMA containment fixes, routing policy, PCM data, generated AppleALC tables, configuration behavior, UI-visible behavior and release diagnostic defaults.
- Synchronizes active metadata to 1.0.67-final-audit while retaining the complete R10-R21 history.


## 1.0.66 — final audit R21 CORB/RIRB cleanup closure and DMA containment error fidelity

- Re-audits the accumulated R20 resource/PCI paths against UEFI Specification 2.11 PCI I/O ownership and cleanup semantics.
- Fixes a post-acquisition cleanup bypass in `HdaCorbRirbCommand()`: when temporary fallback `BUS_MASTER` had been enabled but CORB/RIRB exposed no mutually supported ring size, the function returned `EFI_UNSUPPORTED` directly. R21 routes that case through `Done` so fallback-owned BME is disabled and verified before return.
- Fixes stream-stop containment error propagation: if stream stop and controller reset both fail and disabling PCI bus mastering also fails, the safety-critical containment error is returned instead of being masked by the earlier stop error.
- Adds targeted regressions proving there is no direct unsupported-ring return after fallback BME acquisition and that containment failure has diagnostic priority.
- Final host validation passes 151/151 focused regressions, 4,530/4,530 measured stress tests after 755 warmup tests, shell/Python/C lexical gates, stable file-descriptor/thread counts and a stable final RSS plateau.
- The pinned `edk2-stable202608` build wrapper passes 151/151 in each of five source-gate rounds. The real DEBUG/RELEASE EFI compiler stage remains externally blocked in this environment because no EDK2 workspace is installed and `github.com` DNS resolution fails; the exact failure is preserved in the single `Release/Logs/BUILD-COMPILE.log`.
- Preserves all routing policy, PCM data, generated AppleALC tables, configuration behavior, UI-visible behavior and release diagnostic defaults.
- Synchronizes active metadata to 1.0.66-final-audit while retaining the complete R10-R20 history.


## 1.0.65 — final audit R20 TPL callback/entry reentrancy hardening

- Re-audits the accumulated R19 event/timer paths against UEFI 2.11 TPL semantics and the `edk2-stable202608` dispatcher.
- Prevents the FastBoot `TPL_CALLBACK` timer from preempting the still-active DXE entry path while it owns the global log/file state; the periodic event remains armed and naturally retries after entry completion.
- Keeps boot-screen D requests synchronous while entry is active: the key callback records the request without arming its asynchronous timer, existing controller-safe service points can consume it, and a still-pending request is armed only after `HdaDebugFinish()`.
- Adds an explicit key-callback-active barrier for firmware that invokes `RegisterKeyNotify` callbacks below `TPL_CALLBACK`; a prematurely dispatched timer re-arms by 1 ms instead of entering filesystem/PCI diagnostics re-entrantly.
- Retains the conservative failed-`PciIo->Map()` ownership rule after comparison with official EDK2 consumers: no failed-call mapping output is dereferenced or unmapped speculatively.
- Adds regression coverage for FastBoot entry exclusion, D-key entry deferral, key-callback timer rearm, and R20 metadata/history.
- Synchronizes active metadata to 1.0.65-final-audit while retaining the full R10-R19 history.


## 1.0.64 — final audit R19 retained PCI ownership reconciliation and safe BME activation

- Re-audits the accumulated R18 source against UEFI 2.11 PCI I/O attribute/ownership rules and the pinned EDK2 interfaces.
- Reconciles fallback-owned CORB/RIRB `BUS_MASTER` state after a verified outer PCI rollback; a successful explicit restore to BME=0 now discharges `BusMasterEnabledByFallback` and clears the retained record when no other resource ownership remains.
- Includes fallback-owned `BUS_MASTER` in codec-dump rollback even when ring DMA itself is already proven stopped, preventing BME from remaining enabled after a failed local fallback restore.
- Adds a bounded all-stream HDA `RUN` scan before any transition from BME=0 to BME=1. Playback and CORB/RIRB fallback refuse to enable bus mastering while another stream descriptor is running, preventing stale/foreign DMA from being reactivated.
- Extends regression coverage for both ownership reconciliation and the pre-BME stream-quiescence barrier.
- Synchronizes active metadata to 1.0.64-final-audit while retaining the full R10-R18 history.

## 1.0.63 — final audit R18 PCI attribute ownership and CORB/RIRB bus-master safety

- Re-audits the accumulated R17 source against UEFI Specification 2.11, current UEFI Forum specification listings, and `edk2-stable202608` protocol contracts.
- Replaces full-mask `EfiPciIoAttributeOperationSet` rollback with bit-scoped `Enable`/`Disable` deltas so unrelated PCI attributes are never reverted from an old snapshot.
- Restores only MEMORY/BUS_MASTER bits that this driver actually changed; DMA containment deliberately owns only BUS_MASTER and keeps it disabled.
- Moves temporary BUS_MASTER ownership for CORB/RIRB into the fallback itself, after ring DMA-disable/reset-position validation and before AllocateBuffer/Map.
- Tracks `BusMasterEnabledByFallback` as explicit cleanup ownership and restores it only after ring DMA is confirmed stopped; failures remain retryable without retaining dead buffers.
- Keeps codec-dump setup MMIO-only (`PCI MEMORY`); CORB/RIRB fallback enables BME transactionally only when it is genuinely required.
- Keeps normal controller discovery MMIO-only when firmware supplied BME clear; playback enables and verifies BUS_MASTER only at the final pre-RUN boundary after buffers/BDL/stream setup, avoiding premature reactivation of residual DMA.
- Expands regression coverage for PCI attribute ownership, absence of full-mask Set rollback, and fallback BME ordering/lifecycle.
- Synchronizes active metadata to 1.0.63-final-audit while retaining every historical R10-R17 round.

## 1.0.62 — final audit R17 File.Open contract and deferred-key ordering

- Corrects `HdaOpenOrCreateDir()` so an existing directory is opened with `Attributes = 0`; UEFI permits `EFI_FILE_DIRECTORY` only when `EFI_FILE_MODE_CREATE` is included.
- Converts the boot-screen codec-dump handoff from a plain signaled event to `EVT_TIMER | EVT_NOTIFY_SIGNAL` plus `SetTimer(TimerRelative, 0)`, which UEFI defines to signal on the next timer tick and therefore avoids relying on unspecified `SignalEvent()` callback ordering.
- Preserves the controller ownership gate and retry semantics when the one-shot timer fires during synchronous HDA work.
- Closes the now-dead timer event after a successful interactive dump; `CloseEvent()` is explicitly safe inside its own UEFI notify function. Failure paths retain it for retry.
- Re-audits the direct Boot/Runtime Services, File, SimpleTextInputEx and PCI I/O call surface without changing normal audio routing, PCM data, AppleALC profiles, volume policy or user-visible configuration.
- Synchronizes active metadata to 1.0.62-final-audit while retaining every historical R10-R16 round.

## 1.0.61 — final audit R16 UEFI 2.11 handoff and SingleShot fail-safe
- Regression runner now disables local bytecode generation internally so direct test execution cannot contaminate the release tree with __pycache__/pyc artifacts.

- Moves terminal HDA DMA stop/containment from an `EVT_SIGNAL_EXIT_BOOT_SERVICES` event to `CreateEventEx(..., gEfiEventBeforeExitBootServicesGuid, ...)`, using the UEFI-defined last opportunity to call boot-environment firmware interfaces while avoiding the stricter EXIT handler external-service restrictions.
- Keeps the handoff callback synchronous and bounded, with no delayed timer dependence and no allocation/free, DMA unmap/free, filesystem, NVRAM, event-close/registration or key-unregister work.
- Makes `SingleShot` fail-closed: if neither the volatile per-boot variable nor the private protocol marker can be established, playback is skipped rather than allowing unguarded concurrent HDA/DMA programming.
- Adds the standard `Guid/EventGroup.h` dependency and declares `gEfiEventBeforeExitBootServicesGuid` in the INF; no private GUID or compatibility shim is introduced.
- Re-audits the complete UEFI call surface and retains exact-success publication, terminal File/Event/Unmap semantics, R15 DMA direction/opaque-map ownership, `TPL_CALLBACK`, transactional build staging and release-log hygiene.
- Synchronizes active metadata to 1.0.61-final-audit with Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com.

## 1.0.60 — final audit R15 UEFI 2.11 conformance

- Moves every event notification callback to `TPL_CALLBACK`, eliminating invalid/unsupported `TPL_APPLICATION` notification levels while preserving bounded file/device work permitted at callback TPL.
- Corrects playback DMA direction: `AllocatePages()`-backed PCM/BDL buffers use `EfiPciIoOperationBusMasterRead`; CORB/RIRB remain `AllocateBuffer()` + `EfiPciIoOperationBusMasterCommonBuffer`.
- Tracks successful playback `Map()` ownership independently of the opaque mapping token and guarantees one `Unmap()` per successful mapping.
- Minimizes `ExitBootServices` hardware activity to stopping/verifying DMA or containing bus mastering. Codec replay, PCI attribute rollback and nonessential descriptor/ring register restoration are intentionally excluded from the terminal callback.
- Retains R14's exact failed-output and terminal-release semantics and expands UEFI 2.11 regression coverage for TPL, DMA operation selection, opaque mappings and terminal handoff behavior.
- Synchronizes active metadata to 1.0.60-final-audit while preserving Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com.

## 1.0.59 — final audit R14 UEFI output/terminal semantics

- Treats outputs from failed `CreateEvent`, file `Open/OpenVolume`, `RegisterKeyNotify` and protocol installation as undefined/unowned and never attempts speculative cleanup through those outputs.
- Treats `EFI_FILE_PROTOCOL.Close/Delete`, `CloseEvent` and PCI `Unmap` as terminal operations according to their contracts, avoiding double-close/double-unmap/use-after-free retry patterns.
- Handles `UnregisterKeyNotify(EFI_INVALID_PARAMETER)` as a terminal invalid token and quarantines undocumented registration-state ambiguity without dereferencing stale registration state.
- Uses exact success for keyboard reads and `GetTime()` data consumption and preserves deterministic failure propagation.

## 1.0.58 — final audit R13 lifecycle contract re-audit

- Reclassified resource release paths by their individual UEFI contracts instead of applying a generic "retry on every non-success" policy.
- Hardened filesystem handle, DMA mapping/buffer/page and key-notification ownership state against stale-resource reuse.
- Prepared the code/tests for the R14 direct specification correction of failed-output semantics.

## 1.0.57 — final audit R12 transactional tooling/resource cleanup

- Hardened log/open/seek failure paths so unusable log handles are not published for subsequent writes.
- Made the EDK2 build package staging non-destructive to an external workspace and BaseTools; temporary compatibility edits are restored transactionally and partial outputs are removed on failure.
- Added repeated regression/stress, malformed plist and helper-integration validation without changing normal audio policy.

## 1.0.56 — final audit R11 exact-status/async hardening

- Added exact-status barriers to event, protocol, keyboard, filesystem and fallback paths and expanded regression coverage for partial-firmware behavior.
- Improved asynchronous cleanup bookkeeping and fallback control. Later R14/R15 rules supersede any R11 assumption that attempted to consume an output after its producing call failed or retried a terminal UEFI release.


## 1.0.55 — final audit R10 exact-success PCI/MMIO, DMA and async ownership barriers

- Adds verified PCI attribute transactions: `MEMORY`/`BUS_MASTER` enable, restore and DMA containment require exact success plus read-back of the mutated bits before hardware ownership advances or is cleared.
- Makes critical `CreateEvent`, `SetTimer`, `CloseEvent`, `SignalEvent`, `RegisterKeyNotify` and `UnregisterKeyNotify` boundaries exact-success ownership gates; callback-reachable context is retained when event close is ambiguous.
- Normalizes PCI MMIO read/write and `GetLocation` warning statuses before consuming output data.
- Requires exact success before publishing playback or CORB/RIRB `AllocatePages`/`AllocateBuffer`/`Map` outputs.
- Requires exact success from `Unmap`, `FreeBuffer` and `FreePages` before clearing mapping/page ownership, preventing warning results from becoming silent leaks or stale cleanup state.
- Routes PCI I/O flushes through a single exact-success helper so hardware progress is not inferred from an ambiguous warning.
- Normalizes and propagates non-success codec-dump controller results through the final status instead of allowing an EFI warning to vanish through `EFI_ERROR()` checks.
- Expands regression coverage to 101 tests while preserving normal routing, PCM assets, generated AppleALC data, fixed 30% volume and the single consolidated build-log policy.
- Synchronizes active release metadata to 1.0.55 with Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com.

## 1.0.54 — final audit R9 filesystem, diagnostic retry and post-restore DMA verification

- Bounds the single append-mode diagnostic log at 4 MiB between sessions and recreates the same file rather than accumulating rotated logs.
- Requires exact `EFI_SUCCESS` for log write/flush/delete and config-file read operations; ambiguous warning results are rejected instead of being treated as complete I/O.
- Restricts directory creation retry to `EFI_NOT_FOUND` and closes handles published by ambiguous filesystem results before discarding them.
- Revalidates CORB/RIRB DMA-enable bits after register restoration and before releasing mappings/buffers; resources remain retained if either DMA engine is still enabled.
- Requires clean `OpenVolume()` and SingleShot private-protocol installation results plus valid output handles; ambiguous marker installation is explicitly rolled back.
- Keeps deferred interactive codec dumping retryable when dynamic enumeration/dump fails, rather than marking the request done and unregistering the trigger.
- Requires clean success for the second `boot-args` `GetVariable()` before text parsing.
- Makes AppleALC output replacement atomic with `fsync()` + `os.replace()` and normalizes ZIP-member read failures while cleaning temporary output on error.
- Adds focused regressions for every R9 invariant; the final source suite contains 90 tests while preserving routing, PCM assets, generated AppleALC data and the fixed 30% volume policy.
- Synchronizes active release metadata to 1.0.54 with Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com and the single consolidated build-log policy.

## 1.0.53 — final audit R8 DMA-range, retained-ring containment and diagnostic propagation hardening

- Rejects `EFI_SUCCESS` + null host addresses from CORB/RIRB `AllocateBuffer()` before any memory access while retaining deterministic `FreeBuffer()` ownership for the successful allocation call.
- Makes CORB/RIRB cleanup failure the authoritative fallback result, so retained DMA/mapping ownership is never hidden behind an earlier verb timeout or transport error.
- Propagates per-controller codec-dump failures, stops enumeration when CORB/RIRB ownership is retained, and prevents a synchronous requested dump from entering playback while retained ring cleanup is pending.
- Preserves PCI Bus Mastering containment across codec-dump attribute rollback when ring DMA stop cannot be confirmed; the diagnostic path can no longer re-enable bus mastering against retained ring buffers.
- Validates complete DMA address intervals for audio, BDL, CORB and RIRB against `EFI_PHYSICAL_ADDRESS` wrap and, on controllers without `GCAP.64OK`, against the entire 32-bit addressable range rather than only each base address.
- Treats a failed critical `STATESTS` MMIO read as a controller diagnostic error instead of misclassifying it as “no codecs”.
- Adds a 512 MiB pre-open input-file cap to the AppleALC archive generator so pathologically large ZIP inputs are rejected before central-directory/I/O work.
- Adds focused regressions for each new invariant. The final source suite contains 80 tests while preserving PCM assets, generated AppleALC data, normal route/codec selection and the fixed 30% volume policy.
- Synchronizes active release metadata to 1.0.53 with Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com and the single consolidated build-log policy.

## 1.0.52 — final audit R7 parser, ownership publication and event-handle hardening

- Normalizes malformed AppleALC XML/binary plist parser failures into a controlled generator error instead of allowing `ValueError`, `AttributeError`, `ExpatError` or related parser exceptions to escape with a traceback.
- Preserves the previous skip semantics for malformed optional layout/platform resources by distinguishing parser corruption from structural safety-limit failures.
- Hardens the two-call `boot-args` `GetVariable()` path against a broken firmware returning `EFI_SUCCESS` with an output size larger than the allocation capacity, preventing out-of-bounds token scanning.
- Publishes HDA stream-descriptor ownership only immediately before the first descriptor write; discovery/read failures no longer make cleanup reset a stream that this attempt never mutated.
- Requires non-null handles after successful critical `CreateEvent()` calls for deferred codec dump, ExitBootServices cleanup and FastBoot timer ownership, converting impossible success/null results into a controlled device error.
- Corrects the unused HDA Default Configuration ATAPI connection-type constant from `0xF` (Other) to the specification value `0x3`; current routing behavior is unchanged because this constant is not used by route selection.
- Adds regression coverage for malformed plist handling, preserved optional-resource behavior, `boot-args` capacity containment, stream ownership ordering, critical event handles and the ATAPI encoding.
- Synchronizes active release metadata to 1.0.52 while preserving Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com, fixed 30% volume, PCM assets, AppleALC table and the single consolidated build-log policy.

## 1.0.51 — final audit R6 diagnostic rollback and bounded input polling

- Makes full codec-dump controller access fail closed when the original PCI attribute state cannot be snapshotted, so diagnostics never enable MMIO without a deterministic attribute rollback target.
- Elevates PCI attribute restore failure to the final codec-dump status even when enumeration also failed, preventing a rollback failure from being masked by `EFI_NOT_FOUND` or another earlier diagnostic result.
- Bounds both `SimpleTextInputEx` and console key-drain loops to 64 events per polling cycle, preventing defective firmware that returns endless `EFI_SUCCESS` key reads from freezing the pre-menu boot path.
- Preserves the existing D/d codec-dump trigger, configured poll timeout, route/codec policy, PCM assets, AppleALC tables and fixed 30% volume behavior.
- Adds focused regressions for fail-closed diagnostic ownership, restore-error propagation and finite input draining; the suite now contains 61 tests.
- Synchronizes active release metadata to 1.0.51 while preserving Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com and the single consolidated build-log policy.

## 1.0.50 — final audit R5 UEFI OUT-parameter ownership and generator load bounds

- Treats EFI/PCI I/O OUT values as owned only after `EFI_SUCCESS`, preventing failed `AllocateBuffer()`, `Map()`, `LocateHandleBuffer()`, filesystem/protocol acquisition and related calls from publishing unowned handles/tokens into cleanup paths.
- Isolates playback and CORB/RIRB DMA mapping outputs in locals until mapping succeeds; successful truncated mappings are still published before teardown so legitimate mapping resources are deterministically unmapped.
- Hardens pre-menu `SimpleTextInputEx` discovery against failed protocol calls that leave non-null garbage outputs and validates `ReadKeyStrokeEx` before dispatch.
- Clears failed directory/log/root/protocol outputs so later retries and global cleanup cannot consume stale firmware values.
- Optimizes AppleALC exact-profile ambiguity validation by grouping profiles by codec/layout before overlap comparisons, eliminating quadratic comparisons across unrelated domains.
- Rejects oversized `ConfigData` before converting it to a large integer tuple and caps a single codec/layout exact-profile domain before its inherently pairwise ambiguity check.
- Adds regression coverage for every new ownership boundary and generator resource guard while preserving audio routing, PCM assets, generated AppleALC tables and fixed 30% volume policy.
- Synchronizes active release metadata to 1.0.50 while preserving Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com and the single consolidated build-log policy.

## 1.0.49 — final audit R4 reentrancy and artifact lifecycle

- Serializes the boot-screen `D` codec-dump worker against synchronous `HdaTryController` hardware ownership and services requests only after the controller critical section has ended.
- Installs the interactive dump notification after any boot/config-requested synchronous dump, preventing recursive HDA enumeration/reset during initial diagnostics.
- Stops further playback/probing when a deferred `D` dump completes under `CodecDumpSkipsPlayback=1`.
- Makes the codec-dump helper clean temporary files on normal exit and signals, preserve signal termination codes, use one deterministic backup instead of timestamped backup accumulation, and preserve the first rollback copy across repeated runs.
- Adds regressions for the new critical-section ordering, deferred-dump semantics, helper cleanup/backup policy, cache/temp-file hygiene and 1.0.49 metadata.
- Preserves existing PCM assets, generated AppleALC table, normal route/codec policy, fixed 30% volume behavior, Copyright © 2026 DNMTechLabs and dnmtechlabs@gmail.com.

## 1.0.48 — final audit ownership gates

- Requires the terminal `ExitBootServices` cleanup hook before general HDA playback/diagnostic hardware access, not only before FastBoot ownership transfer.
- Stops controller iteration when any asynchronous playback context or CORB/RIRB state remains retained, preventing recovery state from being compounded across multiple controllers.
- Adds explicit regressions for both exceptional ownership paths.
- Adds pre-EDK2 shell/Python syntax gates and repeated source regression/stress rounds to the build command.
- Synchronizes release metadata to 1.0.48 while preserving Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com and the one consolidated build log policy.

## 1.0.47 — final audit

- Preserves the v1.0.43 30% boot-chime policy and all prior compatibility paths.
- Completes CORB/RIRB DMA ownership, containment, alignment/64-bit validation, largest-ring selection, overrun handling, unsolicited filtering and ExitBootServices-safe restore.
- Uses a two-entry HDA BDL and prevents FastBoot ownership transfer without deterministic ExitBootServices cleanup.
- Makes codec mutations transactional across standard profile/route state, including 16-bit Coefficient Index and Processing Coefficient rollback.
- Skips only profile mutation sequences that cannot be read back and restored deterministically.
- Propagates ConfigData write failures and protects exact-profile application behind successful snapshotting.
- Bounds AppleALC archive/plist resource usage and removes recursive traversal from hostile/pathological plist walking.
- Reduces temporary allocation/copy work in boot-argument parsing.
- Pins reproducible build tooling to `edk2-stable202608` by default and consolidates build output in `Release/Logs/BUILD-COMPILE.log`.
- Makes the pinned EDK2 bootstrap automatic when no valid workspace is present; `AUTO_FETCH_EDK2=0` remains the explicit offline/preinstalled-workspace opt-out.
- Consolidates current audit documentation and release metadata under Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com.

## 1.0.43 — stable minimal volume 30

- Returned to the known-good 1.0.39 functional driver baseline.
- Preserved all existing async ownership and AppleALC profile-validation fixes.
- Fixed the global chime policy at 30% in defaults, INIs, PCM copy and every fallback path.
- Kept the original parser, EDK2 setup, build layout, routes, timing, codec verbs and PCM assets unchanged.

## 1.0.39 — async ownership and profile validation

- Fixed a FastBoot failure path that could free an event callback context after `SetTimer()` failed even when firmware refused `CloseEvent()`.
- Made successful event closure a mandatory ownership barrier before async DMA buffers or callback context can be released.
- Added a cleanup-only retry path for a retained timer callback after the stream descriptor has already been stopped and cleared.
- Suppressed blocking playback fallback and additional probing after asynchronous ownership transfer has been consumed or retained by cleanup.
- Fixed AppleALC exact-profile validation so conflicting profiles with semantically equivalent masked match domains are rejected instead of becoming ordering-dependent.
- Added regression tests for async lifecycle ownership, AppleALC mask-domain conflicts, strict-subset profile precedence, archive/path validation and bundled table bounds.

Successful playback policy, hardware discovery, routes, PCM assets, volume ceiling, configuration keys and defaults are unchanged.

Earlier release history remains documented in `README.md`; `docs/PROJECT-REPORT.txt` is intentionally consolidated to the current audited release.
