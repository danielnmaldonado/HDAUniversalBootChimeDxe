# Testing HDAUniversalBootChimeDxe

Copyright © 2026 **DNMTechLabs**
Contact: **dnmtechlabs@gmail.com**

## Source/build validation gate

`Build-HDAUniversalBootChimeDxe.command` runs shell/Python syntax checks and five regression/stress rounds before EDK2 setup. Override only when needed with `SOURCE_TEST_ROUNDS=1..100`. A firmware DEBUG/RELEASE build still requires a complete EDK2 workspace; failures remain recorded in the single `Release/Logs/BUILD-COMPILE.log`.

## Normal playback test

1. Disable other pre-boot HDA/chime drivers for the test.
2. Install only `HDAUniversalBootChimeDxe.efi` in the active bootloader driver directory.
3. Install `HDAUChimeRelease.ini` as `EFI/HDAUniversalBootChimeDxe/HDAUChimeRelease.ini`.
4. Keep the speaker or headphone connected before powering on.
5. Boot once and confirm that the chime completes before macOS starts.

The chime PCM is always scaled to 30% amplitude. On each selected analog path and additional validated AppleALC/Realtek fanout route, the driver independently requests approximately 30% of zero-dB gain (−10.5 dB or lower after HDA gain-step rounding) at **one** suitable codec amplifier. The selected amplifier is reapplied after the stream starts. With both controls effective, the digital signal and codec amplifier together contribute approximately 9% of the unattenuated amplitude before downstream gains. If a route offers no programmable amplifier with enough negative range, the driver retains its fixed 30% PCM limit and logs the hardware limitation. The legacy `Volume` INI key cannot override the limit.

**This is not a calibrated 30% acoustic loudness/SPL setting.** External speaker amplifiers, output stages, monitor/HDMI volume and physical speaker sensitivity cannot be universally controlled from an HDA DXE driver. A real hardware test is needed to verify the audible level for each codec and output.


## Universal expansion controls

The shipped profiles enable `FullPathMap`, `RouteFallback`, `PinSensePolicy`, `TransactionalRestore`, `CorbRirbFallback`, `PcmFormatFallback`, `DigitalPipeline` and `ExactProfileInit`. `DigitalInfoFrame` remains disabled. Keep `AllowDigitalPins=0` for ordinary analog boot-chime testing; set it to `1` only when intentionally testing HDMI, DisplayPort or S/PDIF.

When comparing routes, use the log entries `route candidate`, `codec snapshot`, `PCM format selected`, `volume`, `digital pipeline` and `codec restore`. A fallback route is attempted only after a real programming or capability failure. EAPD readback mismatches are retried and remain advisory when SET succeeds. A persistently **confirmed** amplifier-gain mismatch, however, rejects the analog route and blocks playback on a partially programmed fanout; unavailable GET readback is logged as unverified rather than falsely marked confirmed. Firmware cannot determine whether a human actually heard the sound.

For the Lenovo S145, collect the log and codec dump with the exact EFI/config used for the silent boot. The most useful evidence is the full selected PathMap, pin-sense value, EAPD transition/readback, amp capabilities/readback, revision ID and subsystem ID.

## OpenCore

The driver must be present in `EFI/OC/Drivers` and listed under `UEFI -> Drivers` in `config.plist`.

## Clover

Place the driver in `EFI/CLOVER/drivers/UEFI`.

## Diagnostic test

Install `HDAUChimeDebug.ini` beside the release INI and set `ReadDebugConfig=1` in the release INI. Alternatively run `Enable-HDAUCodecDump-NextBoot.command`: it preserves first-run backups, builds the full Debug profile in a sibling staging file, publishes it with one rename, and enables the existing Release override only after that atomic publication completes. Diagnostics are consolidated into:

```text
EFI/HDAUniversalBootChimeDxe/Logs/HDAUChime.log
```

For a full codec dump without relying on the `D` key, set:

```ini
Debug=1
FileLog=1
CodecDumpAlways=1
CodecDumpSkipsPlayback=1
```

After collecting the log, return `CodecDumpAlways=0`. The `D` key remains best-effort because some firmware/bootloaders do not expose keyboard notifications to a DXE driver.

## Evidence to collect when there is no sound

Record the codec/controller identifiers, selected `alcid`, selected pin/DAC/path, converter format and stream tag, EAPD/pinctl readback, DMA progress/fault status, bootloader/version, and the corresponding working macOS or Linux codec dump. No macOS audio preference is modified by this driver.

## Source regression suite

Run from the source root:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s Tests -v
bash -n Build-HDAUniversalBootChimeDxe.command Setup-EDK2.command Enable-HDAUCodecDump-NextBoot.command
python3 - <<'PY'
import ast
from pathlib import Path
for name in ("Tools/generate_applealc_profiles.py", "Tests/test_audit_regressions.py"):
    ast.parse(Path(name).read_text(encoding="utf-8"), filename=name)
PY
```

The regression suite validates exact-success async event/context ownership barriers, critical `CreateEvent()` non-null handles, `CloseEvent()` release barriers, verified PCI MEMORY/BUS_MASTER enable/restore/containment, exact MMIO and PCI-location outputs, exact DMA allocation/map/unmap/free ownership transitions, exact PCI I/O flush semantics, suppression of stale blocking fallback, deferred codec-dump serialization against synchronous HDA ownership, retained CORB/RIRB containment, DMA full-range/wrap validation, 32-bit `GCAP.64OK` boundaries, codec-dump warning/error propagation, bounded `boot-args` capacity, stream-descriptor ownership publication, deterministic helper Release opt-in/temp/first-backup cleanup, release artifact hygiene, controlled malformed-plist handling with preserved optional-resource skip semantics, pre-open AppleALC archive-size limits, exact-profile mask-domain conflicts, valid strict-subset precedence, unsafe/duplicate archive rejection, and all bundled generated-table offsets/counts.

A real `DEBUG` and `RELEASE` EFI build still requires a complete EDK2 workspace and supported X64 toolchain:

```bash
./Build-HDAUniversalBootChimeDxe.command
```

## Gain-readback safety in 1.0.84/R45

When a codec returns two **valid but persistently inconsistent** amplifier-gain readings after the driver writes the 30% hardware target, the candidate analog route must be rejected rather than counted as safe. A mismatch in any additional fanout route prevents playback on the affected selected route until state restoration/candidate retry. A firmware that cannot complete GET_AMP_GAIN_MUTE is different: successful SET commands retain legacy best-effort playback compatibility, and the log explicitly marks the hardware gain as unverified. The 30% PCM transport limit always remains active.

This safeguards observable gain mismatches; it cannot calibrate acoustic output from external amplifiers or make unverified codecs acoustically comparable. Test representative layouts on real machines before releasing.

## R46 local safeguards (1.0.85)

- The HDA gain readback checks left and right channels separately, including
  partial-read cases where one channel succeeds with the wrong attenuation and
  the other returns a transport error. Both the selected route and fanout
  must reject a *confirmed* mismatch, while completely unavailable GET
  readbacks (including UEFI warning statuses) retain the documented
  unverified-SET compatibility behavior.
- The optional Secure Boot signing helper uses Python 3 `os.link()` with
  directory-descriptor-relative, non-following, exclusive destination creation.
  It rejects a directory or symlink-to-directory inserted before publication
  instead of writing the signed image under an unexpected filename.
- Local host regressions are not an EDK II DEBUG/RELEASE build or a hardware
  acoustic/Secure Boot validation. No private key is included in the source.

## R47 local safeguard (1.0.86)

After enabling any analog fanout widget or stream, failures from further route
setup **or** the second hardware attenuation pass invalidate the full playback
candidate (including on post-start reinitialization). This avoids starting DMA
while a discarded extra output may still be active at unity gain. Pure
preflight failures before any mutation remain skippable, and `EFI_UNSUPPORTED`
from the hardware attenuation search preserves the documented 30% PCM-only
fallback for codecs without sufficient attenuator range. The executable host
regression uses the real `HdaProgramAnalogOutputPath()` body with fault
injection at each programming step; firmware/hardware confirmation is pending.

## R48 local transport-status guard (1.0.87)

The Immediate Command / CORB-RIRB boundary accepts only `EFI_SUCCESS` as a
completed codec command. A warning result is normalized to a device error;
for an ambiguous immediate-command warning no fallback replay is attempted,
because a SET verb may have non-idempotent effects. An unsuccessfully completed
read never exposes its provisional response as valid data. The new
`Tests/test_transport_status_R48.py` uses injected EFI_STATUS values against
the actual C function. Existing PCI MMIO wrappers already normalize warnings,
so this exercise is boundary hardening rather than evidence that a particular
codec produced a warning on real firmware.
