# No-shell EFI test

Copyright © 2026 **DNMTechLabs**
Contact: **dnmtechlabs@gmail.com**

The driver can be tested directly from the EFI partition without UEFI Shell.

## Clover

Copy:

- `HDAUniversalBootChimeDxe.efi` to `EFI/CLOVER/drivers/UEFI/HDAUniversalBootChimeDxe.efi`
- `HDAUChimeRelease.ini` to `EFI/HDAUniversalBootChimeDxe/HDAUChimeRelease.ini`
- optionally `HDAUChimeDebug.ini` to the same configuration directory

## OpenCore

Copy:

- `HDAUniversalBootChimeDxe.efi` to `EFI/OC/Drivers/HDAUniversalBootChimeDxe.efi`
- the INI files to `EFI/HDAUniversalBootChimeDxe/`

Then add the driver under `config.plist -> UEFI -> Drivers`.


## Expansion defaults

The provided INIs keep analog playback as the safe default (`AllowDigitalPins=0`) while enabling full-route validation, route fallback, Pin Sense policy, transactional restore, CORB/RIRB fallback, PCM format fallback and exact-profile matching. HDMI/DisplayPort/S/PDIF tests must be opted into explicitly with `AllowDigitalPins=1`; HDMI InfoFrame writes remain separately gated by `DigitalInfoFrame=1`.

## Safe first diagnostic boot

The shipped debug profile does not force probe-only mode or a codec dump. To collect a non-playback codec dump, explicitly set in `HDAUChimeDebug.ini`:

```ini
Debug=1
FileLog=1
CodecDumpAlways=1
CodecDumpSkipsPlayback=1
ProbeOnly=0
NoPlayback=0
```

The driver writes the dump and ordinary diagnostics to the same file:

```text
EFI/HDAUniversalBootChimeDxe/Logs/HDAUChime.log
```

After collection, set `CodecDumpAlways=0`. To test playback normally, keep `ProbeOnly=0`, `NoPlayback=0`, and `DumpCodec=0`.

## Configuration precedence

`HDAUChimeRelease.ini` is loaded first. `HDAUChimeDebug.ini` overrides it only when `ReadDebugConfig=1`. This prevents a forgotten debug file from changing release behavior unless the release configuration explicitly allows the override. `Enable-HDAUCodecDump-NextBoot.command` handles this precedence automatically: it preserves first-run backups of existing Release/Debug INIs, builds the complete Debug profile in a sibling staging file, publishes that profile with one same-directory rename, and changes an existing Release INI to `ReadDebugConfig=1` only as the final commit. If a transform fails, the live Debug/Release files are left unchanged and staging files are removed. If no Release INI exists, the helper does not create one; the driver's compiled default permits the prepared Debug INI to be read.
