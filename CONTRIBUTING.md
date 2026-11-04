# Contributing

HDAUniversalBootChimeDxe is an EDK2/UEFI DXE project. Keep changes focused and reviewable; describe the tested audio controller, codec, firmware, and bootloader when reporting a hardware issue. Do not include machine serial numbers, private keys, or personal identifiers in logs.

## Before proposing code changes

1. Run the source tests from the repository root:

   ```sh
   PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s Tests -v
   bash -n Build-HDAUniversalBootChimeDxe.command Setup-EDK2.command Enable-HDAUCodecDump-NextBoot.command
   ```

2. For firmware changes, build both DEBUG and RELEASE EFI images on a supported macOS/EDK2 environment using `./Build-HDAUniversalBootChimeDxe.command`.
3. Include reproduction steps, relevant configuration and test evidence. Do not claim that host-side tests prove audio playback on physical hardware.
4. Keep the root `README.md` limited to the short English project description. Technical history is stored in `CHANGELOG.md`, `docs/PROJECT-REPORT.txt` and `docs/PROJECT-HISTORY.md`.

See [the testing guide](docs/TESTING.md) and [CI documentation](docs/CI.md) for additional details. Project permissions and licensing remain with the repository owner; no license is implied by the presence of this contribution guide.
