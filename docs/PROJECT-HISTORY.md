# HDAUniversalBootChimeDxe 1.0.82-final-audit

UEFI DXE boot-chime driver for Clover and OpenCore. It discovers HDA controllers/codecs at runtime, validates the live HDA widget graph, honors `alcid=xx` when a matching AppleALC-derived profile exists, negotiates 48/44.1 kHz and stereo/mono PCM capabilities, and restores the selected codec state after playback.

Copyright © 2026 **DNMTechLabs**
Contact: **dnmtechlabs@gmail.com**
Credits: ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)



## 1.0.82 final audit R37 — EDK2/XCODE5 sub-page PE alignment compatibility

R37 fixes the real post-compile failure captured on macOS after R36 successfully built the DEBUG EFI image with the pinned EDK2 XCODE5 toolchain. The firmware/runtime code, HDA routing, DMA ownership, AppleALC data and audio assets are unchanged.

- Reproduces the real validator failure `invalid FileAlignment: 0x20` after EDK2 completed DEBUG compilation and `GenFw` produced `HDAUniversalBootChimeDxe.efi`.
- Corrects the Release PE32+ gate to implement the documented sub-page PE exception: when `SectionAlignment < 0x1000`, `FileAlignment` must equal `SectionAlignment`; page-aligned images still require the normal `FileAlignment >= 0x200` range.
- For sub-page images, additionally requires each section with raw data to use `PointerToRawData == VirtualAddress`, preserving the PE rule that file offsets match RVAs in this layout rather than broadly relaxing validation.
- Adds positive regression coverage for the actual EDK2/XCODE5 `0x20/0x20` geometry and a negative case proving mismatched raw offset/RVA remains rejected.
- Preserves all prior X64, PE32+, executable-image, subsystem, section bounds/overlap, entry-point, Data Directory and `WIN_CERTIFICATE` checks.
- Active release metadata advances from 1.0.81/R36 to 1.0.82/R37 as required.
- Final R37 host validation passes 267/267 normally and 267/267 with `PYTHONMALLOC=debug` plus Python development mode; shell syntax and Python AST validation also pass.
- Credits remain ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com).


## 1.0.81 final audit R36 — macOS source-hygiene race closure

R36 is a focused release-pipeline correction based on the real macOS build log from R35. The firmware/runtime code, HDA routing, DMA ownership, AppleALC data, audio assets and PE validator remain unchanged.

- Reproduces the R35 build failure caused by Finder creating `.DS_Store` after the initial hygiene preflight but before a later regression round.
- Adds a narrow `clean_transient_source_metadata()` step that removes only macOS Finder metadata (`.DS_Store` and AppleDouble `._*`) while pruning the supported local `./edk2` workspace.
- Runs that cleanup before the initial strict hygiene gate and again before every regression/stress round, closing the time-of-check/time-of-use window seen in the user's round 5/5 failure.
- Keeps the hygiene gate strict: `.DS_Store` and `._*` remain forbidden if cleanup does not remove them; no warning, test or diagnostic is disabled.
- A functional reproduction seeds root/nested `.DS_Store` plus AppleDouble metadata, then the real wrapper removes them and completes the full source regression gate before the expected environment-only EDK2 boundary.
- Active release metadata advances from 1.0.80/R35 to 1.0.81/R36 as required.
- Final host validation passes 264/264 normally and 264/264 with `PYTHONMALLOC=debug` plus Python development mode. The real wrapper was functionally exercised with seeded root/nested `.DS_Store` and AppleDouble metadata: two complete rounds pass 528/528, all seeded metadata is removed, and execution then stops only at the intentionally disabled EDK2 bootstrap boundary in this environment.
- A five-round wrapper attempt completed rounds 1 and 2 at 264/264 and entered round 3 before the harness timeout; it is not counted as a completed five-round pass.
- Credits remain ©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com).


## 1.0.80 final audit R35 — fail-closed analog routing and PE image-integrity hardening

R35 re-audits the complete accumulated R34 source. The runtime changes are limited to reproduced fail-open HDA capability reads and digital/analog classification boundaries; release tooling is hardened against additional malformed PE/COFF layouts. Existing AppleALC data, PCM/MP3 assets, routing preferences, configuration semantics and successful-path behavior are preserved.

- Makes fallback route-candidate publication fail closed when live `PIN_CAP` cannot be read, preventing a transient transport failure from persisting an HDMI/DisplayPort endpoint as an analog candidate.
- Makes the input-amplifier route prefilter fail closed when `Audio Widget Capabilities` are unreadable, so an unknown input-amp requirement cannot be treated as absent.
- Enforces `AllowDigitalPins=0` from the selected route's live HDMI/DisplayPort capabilities as well as `ConfigDefault`, while preserving the explicit `ForcePin` selection exception.
- Adds a central pre-mutation guard to analog path programming: a pin identified as HDMI/DisplayPort/digital by live capabilities/configuration is rejected before the first `SET_*` write when digital outputs are disabled. This protects AppleALC fanout, Realtek combo/fanout and direct analog programming through one common barrier.
- Hardens the PE32+ Release validator so a Certificate Table cannot overlap headers/section raw data; section raw ranges cannot overlap headers or each other; COFF must mark an executable image; and image/header/section offsets and sizes must respect declared `SectionAlignment`/`FileAlignment`.
- Active release metadata advances from 1.0.79/R34 to 1.0.80/R35 as required.
- Final host validation passes 261/261 normally and 261/261 with Python debug allocator/development mode. Two complete full-suite rounds in the same process pass 522/522 with FD 4→4, threads 1→1 and no GC-object growth between measured rounds; current RSS increases 1,440 KiB between those rounds. A longer stress attempt exceeded the harness limit and is not counted as a pass.
- The retained Release wrapper passes its complete 261-test source gate and then stops before compiler invocation because no local EDK2 workspace exists and shell DNS cannot resolve `github.com`; no `.efi`, partial package or synthetic signature is claimed.
- This remains a UEFI PE/COFF DXE driver rather than a macOS Mach-O application. macOS ad-hoc `codesign` is not an applicable signing primitive; firmware Secure Boot trust requires deployment keys/certificates accepted by the target firmware.
- Final validation evidence and any build-environment limitation are consolidated in `docs/PROJECT-REPORT.txt` and the single `Release/Logs/BUILD-COMPILE.log`.


## 1.0.79 final audit R34 — transport truthfulness, transactional rollback closure and hardened PE release gate

R34 re-audits the complete accumulated R33 source. Changes are limited to reproduced HDA transport-error ambiguities, transactional snapshot integrity and PE/COFF release validation gaps. Existing routing policy, AppleALC generated data, audio assets, user configuration semantics and normal successful-path behavior are preserved.

- Adds status-bearing `HdaGetParamChecked()` handling where a failed GET_PARAMETER could otherwise masquerade as zero-valued widget/PCM/stream/pin/amp capabilities. DAC selection, PCM negotiation, widget power, selector programming, output-pin programming, digital-path readiness and hardware-volume programming now propagate the real transport failure on state-changing paths.
- Makes AppleALC exact RevisionID/SubsystemID matching require a successfully read identity when a mask is present; failed identity reads can no longer satisfy an exact zero/masked profile by accident.
- Makes fanout selector claims and intermediate-pin validation fail closed when widget capabilities cannot be read, preventing an unknown shared selector from being treated as conflict-free.
- Hardens transactional rollback capture: incomplete node snapshots are not published, pin/EAPD requirements cannot disappear after a transient PinCap failure, input-amp paths with an unreadable/missing selected connection abort snapshotting, and Realtek warm-reset power capture stops on unknown widget capabilities.
- Extends the pre-package PE32+ validator to reject zero-section images, raw section ranges outside the file, virtual section ranges outside `SizeOfImage`, unmapped/zero entry points, impossible `NumberOfRvaAndSizes`, and malformed/alignment-invalid `WIN_CERTIFICATE` chains. The validator distinguishes structural certificate presence from trust; firmware Secure Boot trust still depends on deployment keys.
- Active release metadata advances from 1.0.78/R33 to 1.0.79/R34 as required.
- This remains a UEFI PE/COFF DXE driver rather than a macOS Mach-O application. macOS ad-hoc `codesign` is not an applicable signing primitive; firmware Secure Boot signing requires the target firmware's enrolled trust material.
- Final host validation passes 253/253 normally and 253/253 with Python debug allocator/development mode. An eight-round same-process stress run of the 219 in-process-safe tests passes 1,752/1,752 after warm-up with FD 4→4, threads 1→1, GC-tracked objects +3, and RSS plateauing at 118,176 KiB in the final four measured rounds.
- The final release wrapper passes its complete 253-test source gate, then stops before compiler invocation because this host has no local EDK2 workspace and cannot resolve `github.com` to bootstrap the pinned `edk2-stable202608` tree. No `.efi`, partial package, or synthetic signature is claimed.
- Final validation evidence and any build-environment limitation are consolidated in `docs/PROJECT-REPORT.txt` and the single `Release/Logs/BUILD-COMPILE.log`.


## 1.0.78 final audit R33 — route-read closure, spec-correct codec dump and release PE validation

R33 re-audits the complete accumulated R32 source. The runtime changes are localized to reproduced fail-open ConfigDefault reads and diagnostic HDA verb addressing; release tooling gains a fail-closed PE/COFF structural gate before any generated EFI image can be packaged. Audio assets, generated AppleALC profile data, routing policy and fixed-volume behavior are otherwise preserved.

- Closes the three remaining ConfigDefault-dependent route/fanout paths that could reinterpret a transport failure as `Config=0`: QuickPath non-forced pin selection, alcid safe fanout and Realtek AppleALC combo initialization now skip the affected candidate on read failure. Forced QuickPath pins avoid the now-unnecessary ConfigDefault transaction entirely.
- Corrects codec-dump `GET_SUBSYSTEM_ID (F20h)` addressing from root NID 0 to Function Group NIDs. Mandatory codec Vendor/Revision identity reads now propagate transport failure instead of printing synthetic zeros; Function Group subsystem-ID failures are explicitly reported as unavailable.
- Adds a pre-package EFI validator that rejects malformed/truncated outputs and requires DOS/PE signatures, X64 machine type, PE32+, nonzero image/header sizes and subsystem 11 (`EFI Boot Service Driver`). Any present Authenticode certificate table must also be file-range consistent.
- Adds executable regression coverage using a synthetic valid PE32+ EFI image plus a wrong-subsystem negative case, so the release gate itself is tested rather than only inspected.
- Active release metadata advances from 1.0.77/R32 to 1.0.78/R33 as required.
- Final host validation passes 238/238 normally and 238/238 with Python debug allocator/development mode; six same-process measured rounds pass 1,428/1,428 after warm-up with FD 4→4 and threads 1→1. The build wrapper also passes 238/238 in each of five source gates, then stops before compiler invocation because this host has no EDK2 workspace and cannot resolve `github.com` to fetch the pinned toolchain; no `.efi` is fabricated or claimed.
- This project is a UEFI PE/COFF DXE driver, not a macOS Mach-O app bundle; macOS ad-hoc `codesign` is not an applicable signing mechanism. Firmware Secure Boot signing, where required, depends on the target firmware's enrolled key/certificate policy and is intentionally not fabricated by the source build.
- Final validation evidence is consolidated in `docs/PROJECT-REPORT.txt` and the single `Release/Logs/BUILD-COMPILE.log`.


## 1.0.77 final audit R32 — direct-NID conformance and fail-closed route state

R32 re-audits the complete accumulated R31 source against Intel High Definition Audio Rev. 1.0a and the existing UEFI ownership model. Changes are limited to reproduced parser/addressing, route-classification, diagnostic truthfulness and reduced-state boundary defects; audio assets and the generated AppleALC table remain unchanged.

- Enforces the direct 7-bit HDA NID domain (`0x00..0x7F`) in both command transports, generated ConfigData validation, runtime ConfigData spans, path traversal and selector-claim indexing. Bit 27/indirect node addressing is rejected because this driver does not implement the specification's indirect mechanism.
- Rejects malformed connection lists containing sequential Range entries, as required by the Intel HDA connection-list encoding rules.
- Makes route classification and ConfigDefault-dependent programming fail closed on transport errors, including AppleALC full/fallback paths, route candidates, output PinCtl programming and Realtek fanout/profile paths; the already-classified primary route is reused instead of issuing a redundant ConfigDefault verb.
- Propagates mandatory controller-dump MMIO read failures instead of logging synthetic zero values, and identifies widget type 6h as `Volume Knob`.
- Right-sizes widget-indexed state to 128 entries, with explicit pre-index NID guards so malformed/external data cannot turn the memory reduction into an out-of-bounds access.
- The project remains a UEFI PE/COFF DXE driver, not a macOS Mach-O application bundle; Apple ad-hoc bundle signing and desktop-window/live-resize requirements do not apply. Firmware Secure Boot signing remains target-key dependent.
- Final validation evidence is consolidated in `docs/PROJECT-REPORT.txt` and `Release/Logs/BUILD-COMPILE.log`.


## 1.0.76 final audit R31 — pre-test hygiene gate and exact log-cap boundary

R31 re-audits the complete accumulated R30 source. Runtime HDA/DMA ownership, codec routing, playback timing, AppleALC data and PCM/MP3 assets are preserved except for the single evidence-backed log-size boundary correction below.

- Moves cache/temporary-artifact validation into an explicit release preflight gate that executes before Python imports can create their own test-runner bytecode. The gate fails closed on `__pycache__`, `*.pyc`, `.DS_Store` and `*.tmp.*` instead of relying on interpreter flags to make the invariant observable, while pruning the explicitly supported local `./edk2` toolchain workspace from project-source hygiene.
- Corrects the diagnostic append boundary from `LogSize > HDAU_MAX_LOG_FILE_BYTES` to `LogSize >= HDAU_MAX_LOG_FILE_BYTES`; a file already exactly at the 4 MiB cap is recreated before another append can exceed that session-start limit.
- Adds focused R31 regressions for both reproduced defects, makes `find`/`sort` explicit preflight dependencies for the new hygiene gate, and advances active release metadata to 1.0.76-final-audit / R31.
- The pinned build provenance remains `edk2-stable202608` at Tianocore commit `2970e5699ba6267f3384ffab20f96647578aebc8`.
- This remains a UEFI PE/COFF DXE driver, not a macOS Mach-O application bundle; desktop-window behavior and Apple ad-hoc bundle signing do not apply. Firmware Secure Boot signing, where required, remains a separate certificate/key deployment policy.
- Final validation evidence for this round is consolidated in `docs/PROJECT-REPORT.txt` and `Release/Logs/BUILD-COMPILE.log`.


## 1.0.75 final audit R30 — release-tooling portability and full-source revalidation

R30 re-audits the complete accumulated R29 source against the same UEFI 2.11 and Intel High Definition Audio Rev. 1.0a ownership/conformance boundaries. No new evidence-backed defect was found in the HDA runtime path, so codec routing, DMA sequencing, playback timing, fixed-volume policy, AppleALC data and UI-visible behavior remain unchanged.

- Fixes one verified release-tooling inconsistency: the build preflight validates `zip` and `unzip` through `PATH`, and packaging now invokes those same PATH-resolved commands instead of unrelated hard-coded `/usr/bin` paths. This preserves macOS behavior while avoiding a false post-preflight failure on otherwise valid hosts.
- Adds R30 regressions for that packaging invariant and the mandatory release-version synchronization.
- Keeps the pinned `edk2-stable202608` provenance at Tianocore commit `2970e5699ba6267f3384ffab20f96647578aebc8`.
- The project remains a UEFI PE/COFF DXE driver rather than a macOS Mach-O application bundle; desktop-window geometry/live-resize and macOS ad-hoc bundle-signing requirements are not applicable to this source tree. Secure-Boot signing, when required by a target firmware, uses a certificate/private key and is a separate deployment policy.
- Release validation results for this round are recorded in `docs/PROJECT-REPORT.txt` and the single `Release/Logs/BUILD-COMPILE.log`.


## 1.0.74 final audit R29 — post-playback ownership and final RUN-boundary hardening

R29 is the authoritative source state and contains every prior final-audit round through R28. The complete accumulated driver was re-audited against UEFI Specification 2.11 and Intel High Definition Audio Specification Rev. 1.0a. Changes remain localized to evidence-backed stream ownership/cleanup paths; routing policy, fixed-volume behavior, AppleALC data, PCM/MP3 assets and user configuration semantics are preserved.

- Revalidates the complete prepared Stream Descriptor immediately before asserting `RUN`, after codec programming and PCI attribute transitions. A late foreign mutation of CBL/BDL/FMT/tag/status now aborts before DMA can start.
- Once `RUN` is read back clear, a failure to restore the borrowed Stream Descriptor no longer escalates into controller-wide `CRST`. Intel HDA global reset affects controller-wide state and could destroy state owned by another pre-boot agent; R29 records the exact cleanup failure and releases only the driver's now-idle DMA buffers.
- Adds `PostPlaybackCleanupStatus` to preserve the real EFI error for post-playback descriptor/codec/resource degradation without misusing an unrelated generic UEFI warning code and without replaying the chime solely because rollback failed after successful playback.
- FastBoot now surfaces that cleanup degradation before releasing its asynchronous context; retained cleanup paths keep the same exact error for diagnostics/retry.
- The pinned `edk2-stable202608` provenance was revalidated against Tianocore commit `2970e5699ba6267f3384ffab20f96647578aebc8`.
- Host regression coverage is expanded for the final RUN-boundary revalidation, local-vs-global reset scope, exact cleanup-status retention, no duplicate playback on post-playback degradation, and FastBoot diagnostic preservation.
- Final host validation passes 207/207 normally and 207/207 with `PYTHONMALLOC=debug` + `-X dev`. In-process stress passes 828/828 measured tests after 207 warm-up tests with FD 4→4, threads 1→1 and RSS plateauing at 114,928..115,788 KiB.
- The release wrapper passes 207/207 in each of five source-gate rounds. The actual EFI compiler stage remains externally blocked because no EDK2 workspace is installed and the shell cannot resolve `github.com`; the pinned Tianocore commit was independently resolved, but no `.efi` or synthetic signed artifact is represented as successful. The exact build failure is preserved in the sole `Release/Logs/BUILD-COMPILE.log`.
- This is a UEFI DXE driver, not a graphical desktop application; window geometry, live resize, cards/sidebars/toolbars and GPU frame-pacing requirements do not map to this source tree.


## 1.0.73 final audit R28 — transactional HDA transport/stream ownership hardening

R28 is the authoritative source state and contains every prior final-audit round through R27. The runtime was re-audited against UEFI Specification 2.11 and Intel High Definition Audio Specification Rev. 1.0a, with changes limited to evidence-backed HDA command-transport, ring-DMA and stream-descriptor ownership/conformance paths.

- Immediate Command now fails closed on foreign/ambiguous entry-time `ICB` or `IRV`, preserves another agent's unread result, revalidates CORB/RIRB immediately before command/result acknowledgment, and refuses operation when a pre-existing `CORBSTS.CMEI` requires controller recovery.
- CORB/RIRB fallback now revalidates its complete idle snapshot after allocation/mapping and before the first register mutation; a foreign state change aborts without replaying a stale snapshot. `RsvdP` fields are handled by read-modify-write, temporary programming is read back before DMA starts, and late `RIRBOIS`/`CMEI` faults cannot be erased by cleanup and returned as success.
- On controllers without `GCAP.64OK`, the read-only/reserved upper-base registers (`CORBUBASE`, `RIRBUBASE`, `SDnBDLPU`) are not written. 32-bit DMA range checks remain enforced before DMA can be armed.
- Output Stream Descriptors are borrowed transactionally: idle state is captured/revalidated before mutation, stream tags are reserved across all descriptors and rechecked against concurrent changes, bidirectional descriptors use `DIR=1` for output, critical programming and `RUN` are read back, and teardown performs the required Stream Reset before restoring the prior functional descriptor state. Restoration itself is read back; a failed restoration gets one controller-reset/retry containment attempt after DMA is confirmed stopped.
- A fixed 1 ms preparation stall made redundant by transactional acquisition was removed. Routing policy, fixed 30% volume behavior, PCM/MP3 assets, generated AppleALC tables/generator and configuration semantics remain unchanged.
- Final host regression passes 200/200 normally and 200/200 with `PYTHONMALLOC=debug` + `-X dev`. In-process stress passes 800/800 measured tests after 200 warm-up tests with FD 4→4, threads 1→1 and non-monotonic RSS; a longer stress attempt exceeded the environment execution limit and is not represented as a pass.
- The build wrapper passes all 200 tests in each of five source-gate rounds. Real EFI compilation is not represented as successful because no EDK2 workspace is installed and the environment cannot resolve `github.com`; the exact failure is retained in the sole `Release/Logs/BUILD-COMPILE.log`, with no partial EFI/ZIP represented as successful.
- This is a UEFI DXE driver, not a graphical desktop application; window geometry, live resize, cards/sidebars/toolbars and GPU frame-pacing requirements do not map to this source tree.

## 1.0.72 final audit R27 — Intel HDA reset and transport conformance

R27 is the authoritative source state and contains every prior final-audit round through R26. The runtime was re-audited against UEFI Specification 2.11 and Intel High Definition Audio Specification Rev. 1.0a; only evidence-backed HDA reset/transport paths changed.

- Before asserting controller reset, the driver now verifies CORB/RIRB DMA and every GCAP-advertised stream `RUN` bit are clear. An active foreign/unclean DMA state is rejected rather than forcibly reset.
- Both full reset and the low-latency path that discovers an already-running controller wait 521 us after observing `CRST=1` before codec status/discovery is consumed.
- Immediate Command and CORB/RIRB use are mutually exclusive in both directions. CORB fallback cannot begin while `ICB` is busy, and Immediate Command cannot begin while ring DMA is enabled.
- If an Immediate Command launched by this driver times out or its response polling encounters an MMIO read error, owned `ICB` state is cleared with a bounded wait and any racing `IRV` is acknowledged before reuse. A command already busy before acquisition remains untouched, and cleanup will not interfere with an active CORB/RIRB transport.
- Routing, fixed 30% volume behavior, PCM/MP3 assets, generated AppleALC tables/generator, configuration semantics and normal successful playback are preserved.
- Final host regression passes 181/181 normally and 181/181 with the Python debug allocator/development mode; stress passes 3,620/3,620 measured tests after 2,172 warm-up tests without descriptor/thread growth or monotonic RSS growth.
- The build wrapper passes all 181 tests in each of five source-gate rounds. Real EFI compilation is not represented as successful because no EDK2 workspace is installed and the environment cannot resolve `github.com`; the exact failure is retained in `Release/Logs/BUILD-COMPILE.log`.
- This is a UEFI DXE driver, not a graphical desktop application; window position/resize/frame-pacing requirements do not map to this source tree.

## 1.0.71 final audit R26 — fail-closed verification of the pinned EDK2 workspace

R26 is the authoritative source state and contains every prior final-audit round through R25. The complete runtime, DMA/resource ownership, callback/TPL, shutdown and release-tooling paths were re-audited against UEFI Specification 2.11. No runtime HDA/audio/UI behavior required a source change in this round.

- `Build-HDAUniversalBootChimeDxe.command` now refuses a non-Git EDK2 workspace whenever `EDK2_EXPECTED_COMMIT` is active. This closes the R25 provenance gap where the default pinned `edk2-stable202608` toolchain could be accepted without any way to prove that its files actually came from commit `2970e5699ba6267f3384ffab20f96647578aebc8`.
- Explicit custom `EDK2_REF` use remains backward-compatible: when no expected commit is requested, a non-Git workspace is still allowed with the existing warning that provenance cannot be verified.
- The executable transactional DEBUG+RELEASE build command, codec-dump helper, PCM/MP3 assets, generated AppleALC table, routing policy and UEFI runtime C source remain otherwise unchanged.
- The project is a UEFI DXE driver and has no graphical desktop/window or live-resize subsystem; no unrelated UI architecture was introduced.


## 1.0.70 final audit R25 — key-notify failure containment and reproducible EDK2 pin

R25 is the authoritative source state and contains every prior final-audit round through R24. It re-audits the complete UEFI-facing source against UEFI Specification 2.11 and hardens only two evidence-backed edge cases: undocumented `RegisterKeyNotify()` failure ownership and default EDK2 toolchain reproducibility. The project is a UEFI DXE driver and contains no desktop/window subsystem, so there is no window-opening or live-resize code to alter.

- `RegisterKeyNotify()` now treats only the UEFI-documented `EFI_OUT_OF_RESOURCES` failure as a known no-registration result. Any other non-success status is outside the UEFI 2.11 contract and is contained as unknown callback ownership before the deferred event is torn down.
- The default `edk2-stable202608` build path is pinned to the official Tianocore commit `2970e5699ba6267f3384ffab20f96647578aebc8` in both `Build-HDAUniversalBootChimeDxe.command` and `Setup-EDK2.command`. Explicit custom refs remain supported and are not implicitly forced to the default hash.
- `Build-HDAUniversalBootChimeDxe.command` remains the executable, transactional DEBUG+RELEASE compilation command and preserves the single consolidated `Release/Logs/BUILD-COMPILE.log`.

## 1.0.69 final audit R24 — notification-teardown ownership barrier and validated build command

R24 is the authoritative source state and contains every prior final-audit round through R23. The complete UEFI-facing source was re-audited against UEFI Specification 2.11, with particular attention to notification ownership transitions, callback/TPL reentrancy, event lifetime, DMA containment and release tooling.

- Generalizes the D-key notification ownership barrier so it covers both `RegisterKeyNotify()` and `UnregisterKeyNotify()`. The complete lower/upper registration pair remains behind one barrier until known token ownership is published, and a re-entrant key callback cannot publish deferred work while firmware is adding or removing notification ownership.
- Keeps that barrier active through both unregister operations and optional deferred-event teardown; the deferred worker only performs its existing bounded rearm during a temporary ownership mutation, while terminal quarantine/unknown-registration gates remain terminal.
- Preserves all R23 unknown-registration containment and transactional codec-dump-helper behavior, along with routing policy, PCM assets, AppleALC data, normal playback and Release defaults.
- Includes the executable `Build-HDAUniversalBootChimeDxe.command` as the primary compilation command. It runs the source gates, builds EDK2 DEBUG and RELEASE targets, cleans partial output on failure, and integrity-checks the generated release ZIP.


## 1.0.68 final audit R23 — key-registration containment and diagnostic-helper activation

R23 is the authoritative source state and contains every prior final-audit round through R22. The complete UEFI-facing source was re-audited against UEFI Specification 2.11, including callback ownership, event lifetime, DMA containment, PCI I/O state restoration, bounded polling and build/release hygiene.

- Hardens the boot-screen D-key registration path for a firmware contract violation where `RegisterKeyNotify()` returns `EFI_SUCCESS` but a NULL `NotifyHandle`. UEFI requires a unique handle on success; without one, a callback may be installed but cannot be unregistered safely.
- Makes callbacks side-effect-free while `RegisterKeyNotify()` is still on the stack and makes an already-pending worker defer until notification ownership is known; if broken firmware returns success without a handle, terminal containment then clears the request before teardown.
- Publishes a permanent per-boot containment gate before closing the deferred event. Any untracked callback therefore returns without touching timer, filesystem, PCI or codec state, preventing use of a closed event or repeated diagnostic work.
- If the upper-case registration violates the contract after the lower-case token was acquired normally, the containment gate is published first and the known lower-case registration is then unregistered through the existing exact-status cleanup path.
- Corrects the codec-dump helper for normal Release precedence. Because the shipped Release INI disables `ReadDebugConfig`, the helper now backs up the existing Release and Debug INIs, builds the complete Debug profile in a sibling staging file, publishes it with one rename, and only then commits `ReadDebugConfig=1` to an existing Release INI. This remains all-or-nothing even if Release already had Debug enabled. Repeated runs preserve the original backups; if no Release INI exists, the helper does not invent one and relies on the compiled `ReadDebugConfig=TRUE` default.
- Normal firmware behavior, routing policy, PCM assets, AppleALC data, configuration semantics, playback behavior, UI-visible behavior and release diagnostic defaults are unchanged.


## 1.0.67 final audit R22 — nesting-safe key-notify exclusion

R22 is the authoritative source state and contains every prior final-audit round through R21. The complete UEFI-facing source was re-audited against UEFI Specification 2.11, with particular attention to callback/TPL reentrancy, resource ownership, DMA containment and exact-status handling.

- Replaces the R20/R21 boolean D-key callback guard with a nesting depth counter. UEFI permits `KeyNotificationFunction` at any TPL `<= TPL_CALLBACK`; nested notifications can therefore no longer clear the exclusion barrier while an outer callback is still active.
- The deferred codec-dump timer remains blocked until the outermost key notification unwinds, preserving the existing one-shot D-key behavior without adding polling, wakeups or normal-path latency.
- Adds an overflow fail-safe for the depth counter and regression coverage proving balanced enter/leave ordering and worker exclusion.
- No routing policy, PCM asset, AppleALC data, configuration semantics, playback behavior, UI-visible behavior or release diagnostic defaults were changed.


## 1.0.66 final audit R21 — CORB/RIRB cleanup closure and DMA containment error fidelity

R21 is the authoritative source state and contains every prior final-audit round through R20. The UEFI-facing DMA/resource paths were rechecked against UEFI Specification 2.11 PCI I/O ownership semantics.

- CORB/RIRB fallback now routes the unsupported-common-ring-size case through the common cleanup path after temporary `BUS_MASTER` ownership has been acquired. This closes a state leak where a controller with no mutually supported ring size could return `EFI_UNSUPPORTED` while leaving BME enabled.
- Output-stream failure handling now returns the PCI containment failure when stream stop and controller reset both fail and disabling `BUS_MASTER` also fails. The previous result could mask the safety-critical containment error behind an older stop error.
- Regression coverage explicitly locks both failure paths so every error after fallback-owned BME acquisition reaches cleanup and DMA containment failures retain diagnostic priority.
- No routing policy, PCM asset, UI/config behavior, AppleALC data, normal playback path, or release diagnostic defaults were changed.

## 1.0.65 final audit R20 — TPL callback/entry reentrancy hardening

R20 was the authoritative source state for 1.0.65 and is retained below as history. The UEFI-facing event/TPL paths were rechecked against UEFI Specification 2.11 and the `edk2-stable202608` event dispatcher.

- The periodic FastBoot timer runs at `TPL_CALLBACK`; EDK2 dispatches pending higher-TPL notifications while lowering TPL. R20 therefore blocks `HdaAsyncPlaybackTick()` until the DXE entry path has completed `HdaDebugFinish()`, preventing the asynchronous worker from preempting entry-owned global/log state. The periodic timer naturally retries afterwards, and playback timeout accounting does not advance while the gate is active.
- If the boot-screen D key is received while DXE entry is active, the request is marked pending but its timer is not armed. Existing controller-safe synchronous service points retain the previous behavior; if none consumes the request, entry arms the one-shot timer only after its log/file state is closed.
- UEFI permits a registered key notification to be invoked at any TPL `<= TPL_CALLBACK`. A firmware implementation that calls it below `TPL_CALLBACK` could otherwise let the zero-delay timer dispatch before the key callback stack unwinds. R20 publishes an explicit key-callback-active guard; the timer worker re-arms itself by 1 ms until that callback has returned, then performs the existing heavy dump.
- R20 deliberately does **not** attempt to consume `PciIo->Map()` outputs after a failed call. Official EDK2 consumers likewise treat a failed `Map()` as not transferring a usable mapping token; speculative `Unmap()` of a failed-call `OUT` could dereference undefined firmware output.
- All R19 PCI/BME ownership fixes, R18 bit-scoped attribute rollback, R17 File/Event fixes, R16 handoff/SingleShot, R15 DMA/TPL and R14 terminal ownership corrections remain intact.


## 1.0.64 final audit R19 — retained PCI ownership reconciliation and safe BME activation

R19 was the authoritative source state for 1.0.64 and is retained below as history. The complete UEFI-facing call surface was rechecked against UEFI Specification 2.11 and the pinned `edk2-stable202608` protocol definitions.

- A retained CORB/RIRB cleanup record whose only remaining obligation is a fallback-owned `BUS_MASTER` transition is now reconciled with the outer verified PCI rollback. If the outer transaction explicitly restores and verifies `BUS_MASTER=0`, R19 clears `BusMasterEnabledByFallback`; if no ring/map/buffer ownership remains, the retained record is released instead of falsely blocking later playback or diagnostics.
- Codec-dump rollback now includes `BUS_MASTER` in its managed mask when `BusMasterEnabledByFallback` is the pending obligation, not only when DMA containment/stop verification is pending. This closes the case where the fallback had enabled BME, its local disable failed, but the outer diagnostic transaction previously restored only `MEMORY`.
- Before any code path changes PCI bus mastering from disabled to enabled, R19 walks every HDA input/output/bidirectional stream descriptor and requires every `RUN` bit to be clear. Playback and CORB/RIRB fallback therefore fail closed instead of potentially reactivating a foreign/stale stream whose DMA had only been suppressed by BME.
- All R18 bit-scoped PCI rollback, delayed BME, R17 File/Event, R16 handoff/SingleShot, R15 DMA/TPL and R14 terminal ownership corrections remain intact.


## 1.0.63 final audit R18 — PCI attribute ownership and CORB/RIRB bus-master safety

R18 was the authoritative source state for that round and is retained below as history. The complete UEFI-facing code was rechecked against UEFI Specification 2.11, PI 1.10 where applicable to the DXE environment, and the pinned `edk2-stable202608` protocol definitions.

R18 corrects PCI attribute rollback so the driver no longer uses `EfiPciIoAttributeOperationSet` to replace the controller's complete attribute mask. Playback/diagnostic rollback now changes only bits actually owned by this driver using `Enable`/`Disable`, preserving unrelated attributes that may have changed after the original snapshot. DMA containment disables only `EFI_PCI_IO_ATTRIBUTE_BUS_MASTER` and verifies it by read-back.

The CORB/RIRB fallback now owns a temporary BUS_MASTER transition itself. It first verifies both ring DMA engines are disabled and their pointers are at reset position; only then, when BME was originally clear, does it enable BUS_MASTER for the coherent CommonBuffer transfer. Cleanup restores that exact bit after ring DMA is confirmed stopped, and the restoration obligation remains tracked/retryable if firmware rejects the change. This prevents a diagnostic path from pre-enabling bus mastering before stale ring state has been inspected.

The codec-dump owner therefore remains MMIO-only: it enables only `EFI_PCI_IO_ATTRIBUTE_MEMORY`. If Immediate Command falls back to CORB/RIRB, the fallback performs the narrowly-scoped BME transition itself.

Normal playback now follows the same containment principle when firmware supplied BME clear: controller discovery and codec/path preparation enable only `PCI MEMORY`; `BUS_MASTER` is enabled and read-back verified only immediately before the prepared output stream is placed in `RUN`. This prevents entering the driver from reactivating unrelated residual HDA DMA before the new stream is owned.

## 1.0.62 final audit R17 — File.Open contract and deterministic deferred key worker

R17 was the authoritative source state for that round and is retained below as history. The UEFI-facing code was rechecked against UEFI Specification 2.11 and the pinned `edk2-stable202608` protocol definitions.

- Existing-directory opens now pass `Attributes = 0`. UEFI defines `EFI_FILE_PROTOCOL.Open()` attributes as valid only when `EFI_FILE_MODE_CREATE` is present; `EFI_FILE_DIRECTORY` remains used only by the create retry. This prevents strict firmware from rejecting ordinary directory lookup with `EFI_INVALID_PARAMETER`.
- The interactive codec-dump key path now creates `EVT_TIMER | EVT_NOTIFY_SIGNAL` and arms it with `SetTimer(..., TimerRelative, 0)`. UEFI defines a zero relative trigger as the next timer tick, removing the earlier direct `SignalEvent()` dependency. R20 later adds an explicit callback-active guard because the timer tick alone is not a cross-TPL stack-unwind guarantee on every conforming input implementation.
- The controller-critical-section gate remains authoritative: if the timer fires while synchronous HDA state is owned, the request stays pending and is serviced after that owner exits. A later one-shot timer callback becomes a no-op through the existing pending/done gates.
- After a successful interactive dump, the worker unregisters the key notifications and closes its timer event from inside the notify function; UEFI explicitly permits this. Failed dumps retain the event/registrations so the request remains retryable.
- All R16 `BEFORE_EXIT_BOOT_SERVICES`, fail-closed `SingleShot`, R15 DMA/TPL, R14 terminal resource semantics, exact-success output publication and transactional build/tooling corrections remain intact.


## 1.0.61 final audit R16 — UEFI 2.11 handoff and SingleShot fail-safe

R16 was the authoritative source state for that round and is retained below as history. The complete UEFI-facing call surface was re-audited against UEFI Specification 2.11 and the pinned `edk2-stable202608` interfaces.

- The HDA terminal cleanup is registered with `CreateEventEx()` in `EFI_EVENT_GROUP_BEFORE_EXIT_BOOT_SERVICES`, the UEFI-defined last opportunity to use firmware interfaces in the boot environment. This moves bounded PCI/MMIO DMA stop/containment out of the stricter `EFI_EVENT_GROUP_EXIT_BOOT_SERVICES` phase while keeping the callback synchronous, allocation-free and independent of delayed timer processing.
- The callback still performs no `Unmap`, `FreeBuffer`, `FreePages`, `FreePool`, file I/O, NVRAM writes, event close/registration or key-unregister work; ownership release remains outside the handoff path so the OS-loader memory map is not perturbed.
- `SingleShot` is now fail-closed. Playback proceeds only when at least one independent per-boot guard (volatile Boot Services variable or private protocol marker) is actually established. If both mechanisms fail, the driver skips playback instead of allowing multiple images to program the same HDA/DMA state concurrently.
- All R15 DMA direction/opaque-token/TPL corrections remain intact: playback PCM+BDL use `BusMasterRead`, CORB/RIRB use `AllocateBuffer + CommonBuffer`, successful mappings are tracked independently of opaque token values, and event notifications use `TPL_CALLBACK`.
- Acquisition outputs are still published only after exact `EFI_SUCCESS`; failed calls with undefined outputs are never consumed. Terminal File/Event/Unmap semantics from R14/R15 remain authoritative.

## 1.0.60 final audit R15 — UEFI 2.11 conformance

R15 was the authoritative source state for that round and is retained below as history. The UEFI-facing code was rechecked against UEFI Specification 2.11, with particular attention to event TPL, Boot Services lifetime, protocol output ownership, PCI DMA mapping and terminal teardown semantics.

- Event notification functions now use `TPL_CALLBACK`; `TPL_APPLICATION` is not used as an event notification TPL. The deferred codec-dump worker and FastBoot timer remain allowed to perform their existing bounded file/device work at the UEFI callback level.
- Playback PCM and BDL memory allocated with `gBS->AllocatePages()` is mapped as `EfiPciIoOperationBusMasterRead`, matching the HDA controller's device-read DMA direction. `EfiPciIoOperationBusMasterCommonBuffer` is reserved for CORB/RIRB, whose memory is obtained with `PciIo->AllocateBuffer()` and shared concurrently by CPU/controller.
- Successful `Map()` ownership is tracked by explicit booleans, not by interpreting the opaque `Mapping` token. Cleanup calls `Unmap()` once for every successfully established mapping even if a firmware implementation uses an unusual token value.
- `ExitBootServices` handling is deliberately terminal and minimal. It performs only bounded PCI/MMIO work needed to prove stream/ring DMA stopped or to contain bus mastering if stopping cannot be confirmed. It does not replay codec state, scrub nonessential stream/ring registers, restore cosmetic pre-boot PCI state, free memory, unmap DMA, close events, unregister key notifications, touch files/NVRAM, or use timer services during the handoff callback.
- Acquisition APIs publish handles/pointers/tokens only after exact `EFI_SUCCESS`. Outputs documented as undefined on failure are never consumed by cleanup.
- Terminal-release contracts established in R14 remain authoritative: file `Close/Delete`, `CloseEvent`, and PCI `Unmap` are not retried after the terminal operation; failed `Unmap` commit status is propagated without reusing the mapping token.
- The EDK2 build wrapper stages the package outside the user's workspace through `PACKAGES_PATH`, restores any temporary BaseTools compatibility edit byte-for-byte, removes partial build products on failure and retains one consolidated `Release/Logs/BUILD-COMPILE.log`.

### Accumulated UEFI audit rounds R11–R14

- **1.0.59 / R14:** corrected failed-`OUT` handling and terminal resource semantics after direct specification review. This round supersedes earlier speculative cleanup of outputs returned together with failed `CreateEvent/Open/OpenVolume/RegisterKeyNotify/InstallProtocolInterface` calls and supersedes retry assumptions for terminal file/event/DMA operations.
- **1.0.58 / R13:** re-audited filesystem, DMA, key-notify and event lifecycle contracts, separating genuinely retryable ownership from terminal operations.
- **1.0.57 / R12:** hardened filesystem/log lifecycle and made the EDK2 build flow transactional/non-destructive; added repeated stress, malformed-input and helper-integration validation.
- **1.0.56 / R11:** tightened exact-success barriers, fallback behavior and asynchronous ownership. Where an R11 rule conflicts with R14/R15 UEFI-contract findings, R14/R15 is authoritative.

> Historical round notes below record what each round changed at the time. Later rounds intentionally supersede any earlier ownership assumption that proved inconsistent with the UEFI specification.


## 1.0.55 final audit R10

This round preserves the established routing/PCM/volume behavior and hardens exact-success transaction boundaries throughout PCI/MMIO, DMA ownership and asynchronous UEFI resources:

- PCI `MEMORY`/`BUS_MASTER` enable, restore and containment are now read back before the driver considers the mutation committed; firmware that reports success without applying the requested bits cannot advance into DMA or silently complete rollback.
- Event, timer and keyboard-notify resources transfer or release ownership only on exact `EFI_SUCCESS`. A warning from `CloseEvent()` therefore cannot free callback-reachable context/buffers, and ambiguous `CreateEvent()`/registration outputs are never published.
- MMIO reads/writes and PCI location discovery normalize non-success warning statuses before their outputs are consumed.
- Playback and CORB/RIRB `AllocatePages`/`AllocateBuffer`/`Map` acquisitions require exact success before host addresses or mapping tokens become owned by cleanup.
- `Unmap`, `FreeBuffer` and `FreePages` likewise require exact success before ownership fields are cleared; a warning leaves the resource retained instead of making a later teardown use stale state.
- PCI I/O `Flush()` is centralized through an exact-success barrier, preventing a warning from being treated as proof that descriptor/ring writes are visible to hardware.
- Codec-dump controller warnings are normalized and propagated to the final diagnostic result rather than disappearing behind `EFI_ERROR()` semantics.
- The source regression suite now contains 101 focused tests. Normal route scoring, codec/pin/DAC selection, fixed 30% volume, PCM assets and the generated AppleALC table remain unchanged.

## 1.0.54 final audit R9

This round preserves the established playback/routing/PCM behavior and closes additional diagnostic, filesystem and cleanup edge cases:

- The single consolidated `HDAUChime.log` is bounded at 4 MiB between append sessions; oversized logs are recreated in place instead of accumulating rotated files. File write/flush/delete and config reads require clean `EFI_SUCCESS`, so warning statuses cannot be mistaken for complete I/O.
- Filesystem directory/root acquisition is fail-closed: create retry occurs only after `EFI_NOT_FOUND`, and any handle published by an ambiguous non-success result is closed before being discarded.
- CORB/RIRB cleanup performs a final DMA-enable readback after register restoration and retains buffers/mappings if either engine is still enabled.
- Debug `OpenVolume()` and the private SingleShot protocol marker require clean success plus valid handles; an ambiguously installed marker is rolled back before its handle is cleared.
- Deferred interactive codec dumping now consumes the one-shot only after `HdaDumpAllControllersDynamic()` succeeds. A failed dump leaves notifications installed so it can be retried in the same boot.
- The second `boot-args` read requires exact `EFI_SUCCESS` before ASCII/UTF-16 scanning, complementing the existing returned-size containment.
- AppleALC output generation uses an fsync + atomic replace path and normalizes ZIP member read failures into controlled errors without leaving temporary files.
- The final source regression suite contains 90 tests; repeated stress, malformed-plist fuzz, generator determinism and helper integration are revalidated for this revision. Normal audio routes, fixed 30% volume, PCM samples and the generated AppleALC table remain unchanged.

## 1.0.53 final audit R8

This round keeps established playback/routing behavior and hardens failure-only DMA and diagnostic boundaries:

- CORB/RIRB successful allocations must provide usable host addresses before memory is touched; cleanup failures override earlier transport errors so retained ownership cannot be masked.
- Audio, BDL, CORB and RIRB mappings are validated as complete address intervals. Physical-address wrap is rejected, and 32-bit-only controllers must fit every byte below 4 GiB.
- Codec dumping propagates controller errors, stops immediately when ring cleanup ownership is retained, preserves PCI Bus Master containment during rollback, and suppresses subsequent playback while retained diagnostic DMA ownership exists.
- A failed `STATESTS` read is reported as a real MMIO/controller failure rather than “no codecs”.
- The AppleALC generator rejects archives larger than 512 MiB before ZIP parsing, complementing the existing member-count, expanded-size, plist-size/object/depth and ConfigData limits.
- Regression, stress, fuzz, determinism and helper-integration gates cover these changes. PCM samples, generated AppleALC table, fixed 30% volume, route scoring, pin/DAC selection and normal codec behavior remain unchanged.

## 1.0.52 final audit R7

This round keeps the established audio/route/PCM behavior and hardens failure-only boundaries that can otherwise become crashes, invalid cleanup or uncontrolled tooling exceptions:

- Malformed AppleALC plists are normalized to a controlled generator error. Optional malformed layout/platform fragments retain their previous skip behavior; structural resource-limit failures still abort generation.
- The second UEFI `boot-args` read is bounded by the capacity obtained from the first `GetVariable()` call even if broken firmware reports an oversized successful result.
- A selected HDA stream descriptor is not published as owned until immediately before the first register write that can mutate it. Read/discovery failure therefore cannot cause teardown of a descriptor that was never touched.
- Deferred codec-dump, ExitBootServices-cleanup and FastBoot-timer events require both `EFI_SUCCESS` and a non-null event handle before use.
- The HDA ATAPI Default Configuration connection constant is corrected to `0x3`; it is currently not consumed by routing, so normal selection is unchanged.
- Source regressions now contain 68 tests. Additional host validation in this round completed 3,400/3,400 repeated in-process tests, 4,000 deterministic malformed-plist fuzz calls without an unexpected exception, 500/500 deterministic generator outputs and two repeated helper runs with one original backup and zero orphan rewrite temporaries.
- Release metadata is synchronized at 1.0.52. Copyright/contact remain Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com.


## 1.0.51 final audit R6

This round preserves the established audio behavior and closes two failure-only firmware paths discovered in the final diagnostic/timeout audit:

- Full codec dumping now requires a successful snapshot of the controller's original PCI attributes before MMIO is enabled. If the snapshot fails, that controller is not touched further by the dump path.
- A failure restoring PCI attributes is always surfaced as the final diagnostic error, so a rollback failure cannot be hidden behind an earlier `EFI_NOT_FOUND` or other enumeration status.
- Pre-menu D-key polling drains at most 64 events from each UEFI input interface per cycle. This preserves normal queued-key handling while guaranteeing progress to the configured timeout even if defective firmware reports endless successful reads.
- The release source regression suite contains 61 tests and preserves Release logging defaults, the fixed 30% volume policy, PCM assets, generated AppleALC data, routes and configuration compatibility.
- Release metadata is synchronized at 1.0.51. Copyright/contact remain Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com.


## 1.0.50 final audit R5

This round preserves the established codec/route/PCM/volume behavior and closes ownership ambiguity at UEFI API failure boundaries while bounding additional hostile-input work in the AppleALC generator:

- `EFI_PCI_IO_PROTOCOL.AllocateBuffer()` and `Map()` results are staged in local variables and become CORB/RIRB or playback ownership only after `EFI_SUCCESS`. Failed firmware calls can no longer inject non-owned host pointers or mapping tokens into `FreeBuffer()`/`Unmap()` cleanup.
- A successful but truncated playback mapping is deliberately published before rejection so its legitimate mapping token is still released.
- Failed `LocateHandleBuffer()`, filesystem directory/log/root acquisition, boot-guard protocol installation and pre-menu `SimpleTextInputEx` lookup no longer publish or consume failed OUT values. `ReadKeyStrokeEx` is validated before use.
- AppleALC exact-profile ambiguity checks are grouped by codec/layout, avoiding comparisons between unrelated domains. A single exact domain is safety-bounded before pairwise overlap analysis.
- Oversized `ConfigData` is rejected before materializing its verb tuple, avoiding unnecessary multi-megabyte allocations/CPU work on malformed archives.
- The final source regression suite contains 58 tests. The official pre-EDK2 gate completes five clean rounds before toolchain setup.
- Release metadata is synchronized at 1.0.50. Copyright/contact remain Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com.


## 1.0.49 final audit R4

This round preserves the established codec/route/PCM/volume behavior and closes reentrancy and repeated-run cleanup gaps found in the final lifecycle audit:

- The interactive `D` codec-dump worker cannot walk/reset HDA hardware while `HdaTryController` owns a synchronous stack-local playback/verb context. A request arriving in that critical section remains pending and is serviced immediately after ownership returns.
- The boot-screen `D` notification is installed only after any boot/config-requested synchronous codec dump finishes, preventing diagnostic recursion during initial enumeration.
- `CodecDumpSkipsPlayback=1` is honored after a deferred dump serviced at the controller boundary, so probing does not continue into another controller after the requested diagnostic action.
- `Enable-HDAUCodecDump-NextBoot.command` now cleans interrupted `.tmp.<pid>` files, terminates deterministically on HUP/INT/TERM, and keeps one deterministic `.backup` instead of accumulating timestamped copies, and preserves the first pre-helper configuration across repeated runs.
- Release hygiene regressions reject Python caches, `.DS_Store`, orphan `.tmp.*` files, duplicate auxiliary logs and unmanaged diff/patch/hash artifacts.
- Release metadata is synchronized at 1.0.49. Copyright/contact remain Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com.


## 1.0.48 final audit

This round preserves the established audio behavior and adds fail-safe ownership gates around exceptional firmware cleanup paths:

- Playback/diagnostic HDA access now requires a successfully installed `ExitBootServices` cleanup event before controller mutation begins; if firmware cannot provide the terminal cleanup hook, the driver skips hardware work instead of risking stranded DMA/ring state.
- Controller iteration now stops whenever asynchronous playback ownership or retained CORB/RIRB ownership survives a failed candidate. It will not mutate a second HDA controller while cleanup of the first remains pending.
- The build command runs shell/Python syntax gates and repeats the regression suite five times by default before EDK2 setup (`SOURCE_TEST_ROUNDS` is bounded to 1..100).
- Release metadata is synchronized at 1.0.48 and the single-log/copyright/contact policy is regression-tested.

## 1.0.47 final audit

This cumulative release preserves the v1.0.43 audible behavior and 30% volume policy while closing the boot/DMA, lifecycle and transactional-state issues found in the subsequent audit rounds.

- CORB/RIRB fallback now retains ownership on cleanup failure, contains unconfirmed DMA with PCI bus-master disable, validates 128-byte DMA alignment and `GCAP.64OK`, rejects RIRB overrun, filters unsolicited responses, handshakes `CORBRP`, and uses the largest ring size supported by both engines. ExitBootServices cleanup never re-enables ring DMA.
- Playback BDL uses two descriptors and matching `LVI`, and FastBoot transfers asynchronous ownership only after the ExitBootServices cleanup hook is installed. Terminal cleanup restores PCI attributes even when codec-state restore reports an error.
- Codec programming is transactional for the standard state modified by generic, AppleALC, Realtek/fanout and exact-profile paths. Processing coefficients and their 16-bit Coefficient Index are snapshotted/restored; private mutations without deterministic GET/rollback are skipped instead of being left resident.
- ConfigData write errors are propagated. Exact-profile data is applied only after its state can be captured. Digital paths avoid unnecessary analog post-start programming.
- The AppleALC generator bounds archive expansion, member count, plist size/object count/depth and uses iterative traversal for pathological nesting.
- Boot-argument parsing reuses one workspace instead of allocating/copying repeatedly.
- Build tooling pins the default EDK2 reference to `edk2-stable202608`, remains overrideable through `EDK2_REF`, and writes one consolidated build log at `Release/Logs/BUILD-COMPILE.log`.
- The active source and release documentation use Copyright © 2026 DNMTechLabs / dnmtechlabs@gmail.com.

## 1.0.43 stable minimal volume 30

This revision is based directly on the user-confirmed stable 1.0.39 driver. It preserves all codec discovery, routes, timing, DMA ownership, async cleanup, AppleALC profiles, PCM assets and build behavior. The only runtime change is the global boot-chime volume policy:

- Compiled default and both INIs are fixed at 30%.
- The legacy `Volume` key remains accepted but cannot raise or lower the fixed level.
- The PCM copy path forces the same 30% ceiling in hardware-volume, software fallback and replay paths.
- PCM samples are scaled directly by `30/100`; Q8 `77` is retained only as the nearest internal policy representation.

No parser policy, EDK2 workflow, codec route, hardware verb, stream timing or ownership behavior was changed.

## 1.0.39 async ownership and profile validation

This maintenance release closes two failure-only correctness gaps while preserving the established discovery, routing, volume, PCM and hardware-programming behavior:

- FastBoot timer-arm failure now transfers teardown to the central ownership-safe cleanup path. Blocking fallback and further codec/controller probing are suppressed after DMA/context ownership has moved or been released by that cleanup.
- A failed `CloseEvent()` can no longer be followed by DMA-buffer release. The event, callback context and every buffer reachable by that callback remain owned until event closure succeeds.
- If firmware signals a retained timer after the stream was already stopped, the callback retries teardown without reading a cleared stream descriptor.
- The AppleALC generator now rejects conflicting exact profiles whose IDs differ only in bits excluded by their masks. Strictly more-specific profiles remain valid and are emitted first as before.
- Regression tests cover the async ownership barriers, generated-table bounds, valid strict-subset profiles, semantically equivalent mask conflicts, unsafe resource paths and duplicate archive members.

No codec ID, layout, NID, ACPI name, PCI address, endpoint, board-specific route, timing, PCM asset, volume policy or default configuration behavior was changed.

## 1.0.38 systematic lifecycle hardening

This aggressive failure-path audit preserves the codec graph, route policy, PCM assets, stream timing and absolute 40% volume ceiling while closing remaining ownership gaps:

- Failed codec/PCI restoration or DMA unmap/page release now retains the complete cleanup context instead of letting a stack context disappear with live ownership.
- Blocking, prepared-stream, allocation-failure and post-start failure paths share the same verified free-or-retain policy.
- Async event closure is checked before its callback context is released; failed event closure keeps the context valid for a later retry.
- ExitBootServices restoration disables temporary CORB/RIRB fallback, preventing hidden allocations while the final memory map is being committed.
- DMA containment no longer attempts an impossible codec restore, while the original PCI bus-master state remains intentionally disabled.
- Duplicate PCI-attribute restoration was removed.
- HDMI InfoFrame snapshotting now requires complete DIP state and verified index restoration; partial snapshots are transactionally rolled back before another route is attempted.
- Key-notification unregister and deferred-event close failures retain their handles for retry instead of silently losing ownership.

Normal successful playback behavior, AppleALC/VoodooHDA-compatible amp semantics, config keys and release defaults are unchanged.

## 1.0.37 volume-40 hard cap

The global boot-chime ceiling is now structurally limited to 40% (`Q8 102`), not merely configured to `Volume=40` by default.

- Percent values above 40 and raw Q8 values above 102 are clamped while parsing either INI.
- The PCM/DMA transport ceiling is Q8 102, so an unavailable amp, failed readback, codec reset or post-start verb replay cannot expose the former 50% transport level.
- The PCM copy routine applies the same Q8 102 cap defensively, independent of parser and hardware-volume state.
- For configured values below 40%, HDA amp attenuation rounds toward more attenuation rather than toward the nearest step.

Codec/controller discovery, route ranking, AppleALC data, EAPD, stream timing, PCM assets and compatibility switches are unchanged.

## 1.0.36 transport-50 hardening

This systematic maintenance round preserves the 1.0.35 transport-50 audio behavior and hardens failure-only cleanup/tooling paths:

- Temporary CORB/RIRB fallback DMA is now stopped, flushed and read-back verified before buffers are unmapped or freed. Failed teardown retains the buffers safely and disables further fallback attempts for that boot.
- Every temporary ring-register restore error is propagated instead of being silently discarded.
- Playback DMA cleanup now checks `Unmap()` and `FreePages()` results; mapped pages are retained instead of being freed through a live mapping.
- DMA page ownership tracks the BDL allocation explicitly and rejects a null host pointer returned from physical address zero.
- The Clang-only conditional-uninitialized warning and signed-mask warning are eliminated without changing the successful playback path.
- The AppleALC generator remains Python 3.8 compatible, validates archive/plist/resource-path bounds, emits safe C strings and checks generated route offsets.
- Build and codec-dump helpers pass strict ShellCheck, fail on partial config writes, and integrity-test the generated release ZIP.

`Volume=40`, Q8 transport `128` (50%), PCM assets, codec discovery, route ranking, AppleALC table, EAPD, amp policy, stream timing and default compatibility switches are unchanged.

## 1.0.35 transport-50-test

This test release changes only the DMA PCM transport ceiling requested for hardware validation. The configured final volume remains `Volume=40`.

- `HDAU_PCM_TRANSPORT_GAIN_Q8` changes from Q8 `192` (75%) to Q8 `128` (50%).
- When a usable HDA amplifier is selected, the driver transports PCM at 50% and requests the remaining attenuation needed for a 40% final amplitude at that single amp.
- If no usable amp is selected, the existing software fallback remains Q8 `102` (40%).
- Codec discovery, route selection, Lenovo/desktop compatibility, EAPD, Pin Sense, DMA, watchdogs, PCM assets, AppleALC data and the XCODE5 `memcpy` link fix are unchanged.

## 1.0.34 xcode5-memcpy-link-fix

This maintenance release fixes the real EDK2/XCODE5 link failure reported by the supplied build logs. Clang successfully compiled the driver but lowered three optimized structure copies to the external C runtime symbol `memcpy`; a DXE module does not link libc, so the final Mach-O link failed.

- A local compiler-support `memcpy` symbol now delegates to EDK2 `BaseMemoryLib` `CopyMem`.
- The helper has the native pointer/`UINTN` signature expected on X64 and introduces no separate libc dependency.
- HDA discovery, codec routes, Lenovo compatibility, desktop compatibility, EAPD, volume, PCM, DMA and all ten universal expansion layers are unchanged.
- The source remains build-system independent: the fix resolves compiler-generated copies without guessing which source assignment Clang will inline or lower on a specific optimizer revision.

## 1.0.33 desktop-lenovo-compatibility

This maintenance release preserves all ten universal codec-expansion layers from v1.0.32 and corrects three compatibility regressions exposed by hardware testing: v1.0.32 made the Lenovo S145 audible, but stopped the previously working desktop.

- Analog Pin Sense absence is now diagnostic only. Positive presence detection still improves ranking, internal-speaker preference remains active, and HDMI/DisplayPort still require valid presence/ELD. An unreliable preboot absence value can no longer demote a working desktop line-out or headphone route.
- EAPD keeps the v1.0.32 read/merge/verify path when supported, but falls back to the v1.0.31-compatible direct `0x02` write when GET/readback is unavailable or inconsistent. A successful SET is no longer rejected only because firmware readback is unreliable.
- HDA amp readback remains enabled for diagnostics and receives one bounded rewrite. When both reads remain unavailable or mismatched, the successful volume SET is accepted instead of rejecting the complete route.
- Real write failures remain fatal. Only readback-only incompatibilities were relaxed.
- The 85 ms EAPD stabilization, internal-speaker ranking, transactional restore, CORB/RIRB fallback, PCM format fallback, digital pipeline, route candidates and exact-profile framework remain enabled.
- `Volume=40` and the known-good Q8 192 transport path are unchanged.
- No codec ID, layout, NID, motherboard, GPIO polarity or vendor coefficient was added.

## 1.0.32 universal-codec-expansion

This round implements the ten requested compatibility layers without changing the confirmed desktop primary route or adding model-specific NIDs, GPIO polarities or invented codec coefficients:

1. Complete AppleALC PathMap node chains are supported by the generator and validated edge-by-edge against the live codec graph. The bundled legacy table remains compatible and uses live graph fallback until it is regenerated from an AppleALC source archive.
2. Up to 24 validated route candidates are collected. Candidate zero is always the original selected route; fallbacks are attempted only after programming/readback failure.
3. `Volume=40` keeps the known-good Q8 192 DMA transport and applies the final amplitude at one discovered HDA amp using the advertised zero-dB offset and dB step size. Left/right readback is mandatory.
4. Pin Sense presence, trigger-required handling and ELD validity participate in output ranking without excluding internal speakers.
5. Exact initialization profiles can be generated and matched by codec ID, layout ID, revision mask and subsystem mask. Generic GPIO/coefficient sweeps remain prohibited.
6. The selected route plus every NID targeted by AppleALC ConfigData or an exact profile is snapshotted. AFG/widget power, selectors, pin/default configuration, EAPD, converter state, processing state, amps, GPIO and enabled HDMI state are restored transactionally, with one bounded retry after a transient restore failure.
7. EAPD power-up is ordered before unmute and receives an 85 ms stabilization delay only when transitioning from off to on.
8. Immediate Command remains primary. A temporary CORB/RIRB fallback is used only when both firmware rings are disabled and at reset positions; coherent buffers are allocated through `EFI_PCI_IO_PROTOCOL` and all saved registers are restored.
9. PCM format negotiation prefers 48 kHz/16-bit, falls back to 44.1 kHz/16-bit, and converts the embedded stereo source to mono only when the selected DAC is mono.
10. Analog, S/PDIF and display-audio programming are separated. HDMI/DisplayPort require live presence and ELD; channel mapping is programmed only for display routes, and an HDMI InfoFrame is never written to a DisplayPort pin.

Route discovery does not apply AppleALC ConfigData or exact initialization profiles; those verbs run only after the transactional snapshot. Pin Sense may issue the specification-required transient trigger on pins that advertise it. Digital outputs remain opt-in through `AllowDigitalPins=1`; `DigitalInfoFrame=0` remains the conservative default.

## 1.0.31 volume-40-hardware-attenuation

This round rolls the functional driver baseline back to the last user-confirmed audible source, `1.0.27-output-resolution-hardening`, and changes only the volume path. `Volume=40` remains the default in the compiled fallback, Release INI, Debug INI and codec-dump helper.

For volumes below the known-good 75% PCM transport level, the driver now keeps the DMA stream at that confirmed signal level and discovers one usable amplifier on the validated live HDA route. It applies the requested reduction at that single amplifier, preferring the selected DAC output amp, then another route output amp, then the selected indexed input amp. All other route amps remain at safe unity, so attenuation is not compounded. The selected hardware control is reapplied after post-start codec verbs. When no usable amp exists, the original software PCM scaling remains the fallback.

No codec ID, layout, board, controller, NID, GPIO sequence, AppleALC profile, audio asset, DMA register sequence, route ranking or playback timing was changed.


## 1.0.26 playback-progress-hardening

This maintenance round preserves the dynamic codec graph, exact AppleALC data, route programming and chime timing while hardening DMA progress evidence. Blocking and FastBoot playback now treat the first valid LPIB read only as a baseline, require a later position change before accepting the stop threshold, reject LPIB values outside the programmed CBL, and apply the asynchronous no-progress watchdog to stalls that occur after DMA has already started. This prevents stale or invalid MMIO state from being reported as a successful chime and prevents a partially moving stream from remaining owned until the longer hard timeout.

No codec, controller, board, NID, GPIO polarity or layout-specific default was added.


## 1.0.25 dynamic-graph-hardening

This maintenance round keeps the chime pipeline intact while removing fixed codec/NID/mobile/desktop guesses from generic selection. Runtime now evaluates every Audio Function Group, accepts the complete NID `0x00–0xFF` space, expands connection lists atomically, preserves all compatible AppleALC fanout paths through shared-selector claim tracking, supports dedicated and bidirectional output descriptors, validates DMA addressing against controller capabilities, and treats malformed configuration as invalid rather than silently changing behavior. Build and dump helpers now reject ambiguous/stale inputs instead of selecting the first filesystem match.

Exact codec/layout data remains generated from AppleALC and every chosen route is still revalidated against the live HDA graph. No board-specific GPIO polarity, vendor coefficient, fixed pin or controller-specific audible fix was added to the default path.

## 1.0.24 spec-lifetime

This maintenance round preserves the boot-chime pipeline and adds specification-driven safety fixes: full 8-bit HDA NID traversal, live PCM-capability filtering for converters, zero-dB amplifier initialization from AmpCaps, power-state completion polling, malformed connection-list rejection, programmable input-amp route validation, retained ownership after an unconfirmed blocking DMA stop, and ExitBootServices cleanup without log/file activity. The AppleALC generator now rejects boolean numeric fields, validates plist container types, accepts the full NID range, and detects duplicate archive members in linear time.

## 1.0.23 runtime-safety

This audit round hardens the HDA register widths, full 7-bit connection-list handling, dynamic route discovery, amplifier capability inheritance, PCI/DMA ownership, asynchronous teardown, deferred codec dumps, deterministic AppleALC generation and macOS/Mavericks build helpers. The audible chime asset, `alcid` priority and normal boot-chime purpose are unchanged.

## Safety policy

The default profile is dynamic and conservative. It does not run generic GPIO polarity sweeps, invented Realtek coefficient writes, broad analog pin sweeps, or board/controller-specific H81 logic. Optional legacy compatibility switches remain available but are disabled by default and should only be enabled with exact evidence from a working codec/layout dump.

## Source layout

- `HDAUniversalBootChimeDxe/`: driver, HDA definitions, embedded 48 kHz and 44.1 kHz PCM sources, and generated AppleALC profile table.
- `Tools/generate_applealc_profiles.py`: deterministic generator for exact codec/layout path hints and ConfigData verb streams from an AppleALC source zip.
- `assets/BootChime.mp3`: original chime asset; firmware playback uses the compiled PCM array.
- `HDAUChimeRelease.ini`: silent normal profile.
- `HDAUChimeDebug.ini`: debug/log profile.
- `docs/PROJECT-REPORT.txt`: current audit plus consolidated historical notes. It is the only `.txt` documentation file.

## Build

Python 3.8 or newer is required. On macOS, run the build command directly; if no valid EDK2 `WORKSPACE` is available, the build automatically bootstraps the pinned EDK2 revision into `~/edk2`:

```bash
chmod +x Setup-EDK2.command Build-HDAUniversalBootChimeDxe.command
./Build-HDAUniversalBootChimeDxe.command
```

`AUTO_FETCH_EDK2` is on by default so a clean source tree can compile without a separate setup step. The pinned default revision is `EDK2_REF=edk2-stable202608`; set `EDK2_REF` explicitly only when intentionally validating another EDK2 revision. Existing `WORKSPACE`/`~/edk2` installations are reused. To forbid network bootstrap and require a preinstalled EDK2 workspace, explicitly use `AUTO_FETCH_EDK2=0`. `Setup-EDK2.command` remains available for manual preparation.

The build command creates DEBUG and RELEASE EFI files, places the corresponding INI beside each output, and creates an EFI test pack under `Release/`. It accepts the exact requested target/toolchain output, verifies duplicate EDK2 staging copies byte-for-byte, selects the canonical architecture-level EFI deterministically, and rejects missing or divergent candidates.

## Installation

Place `HDAUniversalBootChimeDxe.efi` in the bootloader driver directory:

- Clover: `EFI/CLOVER/drivers/UEFI/`
- OpenCore: `EFI/OC/Drivers/`, then add the driver under `UEFI -> Drivers` in `config.plist`.

Place the selected INI at:

```text
EFI/HDAUniversalBootChimeDxe/HDAUChimeRelease.ini
```

For diagnostics, place `HDAUChimeDebug.ini` in the same directory. The release profile is read first; the debug profile overrides it only when `ReadDebugConfig=1`.

## Codec dump and logging

All diagnostics are consolidated into one file:

```text
EFI/HDAUniversalBootChimeDxe/Logs/HDAUChime.log
```

A dump can be requested through `CodecDumpAlways=1`, `DumpCodec=1`, a supported dump boot-arg, or the `D` key when the firmware exposes `SimpleTextInputEx`. The key callback only schedules a deferred application-level event; it does not walk PCI/filesystems at callback TPL. `Enable-HDAUCodecDump-NextBoot.command` discovers the mounted Clover/OpenCore EFI dynamically and refuses to guess when more than one candidate is mounted; `EFI_VOL=/Volumes/<name>` selects one explicitly. It preserves first-run backups of existing Release/Debug INIs, prepares the debug profile, and enables the Release `ReadDebugConfig` override only after preparation completes. It enables the dump without a blocking keyboard poll. The setting remains enabled until the backups are restored or the dump keys and `ReadDebugConfig` are reset.

## AppleALC table regeneration

```bash
python3 Tools/generate_applealc_profiles.py /path/to/AppleALC.zip \
  HDAUniversalBootChimeDxe/HdaAppleAlcLayouts.h
```

The generator rejects ambiguous archives, conflicting platform mappings, duplicate ZIP members, overlapping exact predicates and profiles that exceed compiled limits. It preserves complete PathMap node chains, emits compact shared route/verb arrays, separates generic ConfigData from revision/subsystem-specific overrides, and produces deterministic output. Runtime validates every node-to-node edge and every programmable input-amp index against the live codec graph before using a generated route. Layout `0` is preserved when AppleALC defines it explicitly.

## Validation boundary

The package includes static/source validation results in `docs/PROJECT-REPORT.txt`. A real EDK2/XCODE5 build and hardware boot test must still be performed in the target macOS/firmware environment.
