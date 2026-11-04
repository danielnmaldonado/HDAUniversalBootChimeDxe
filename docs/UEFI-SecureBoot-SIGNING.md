# UEFI Secure Boot signing (PE/COFF, not macOS ad-hoc)

`HDAUniversalBootChimeDxe.efi` is an **x86-64 PE/COFF EFI Boot Service Driver**. macOS `codesign -s -` creates an ad-hoc signature for Mach-O applications and is **not** an EFI Authenticode signature. A test EFI can be unsigned when Secure Boot is disabled or under a permitted trust policy; EFI images loaded under enforcing Secure Boot must be accepted by the platform's configured trust database and boot manager.

The project does not include an EFI signing key or issue a self-signed certificate implicitly. A newly generated signature is **not trusted** merely because `sbverify` reports cryptographic validity. The signing certificate (or its chain, subject to platform policy) must be trusted in the target firmware Secure Boot `db`. Firmware/vendor policy and third-party bootloader verification may impose additional checks.

## Signing after building (operator supplies trusted credentials)

Use a controlled system with Python 3 and `sbsign` and `sbverify` from `sbsigntool` installed. Supply your **own db signing key** and corresponding certificate, kept outside the source repository and GitHub artifacts:

```bash
./Tools/Sign-UEFI-SecureBoot.command db.key db.crt \
  Release/Release/HDAUniversalBootChimeDxe.efi \
  Release/Release/HDAUniversalBootChimeDxe-signed.efi
```

The helper refuses to overwrite any output or follow an existing output symlink. It publishes via an exclusive directory-relative hard-link, refusing a concurrently created file, directory or symlink-to-directory; run it in an output directory controlled by the operator because no tool can prevent a separate writer from replacing the file after publication. It writes to a temporary file and publishes the new image only after `sbverify --cert` succeeds. It does not enroll keys in the firmware, create a key, sign any binary automatically, or assert acceptance by a physical Secure Boot system. Use a **distinct output path**; the reproducible unsigned CI artifact remains unchanged.

On a secured test system, verify that the signed image is accepted by the intended bootloader/firmware with Secure Boot actually enabled. The GitHub Actions DEBUG/RELEASE build currently validates PE structure and checksums but does not hold signing credentials or produce firmware-trusted signed releases.
