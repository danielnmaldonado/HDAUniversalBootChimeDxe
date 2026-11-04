# Security policy

HDAUniversalBootChimeDxe executes in UEFI firmware before the operating system starts. Treat the driver and any EFI artifacts as privileged code; validate binaries before deployment and keep working EFI backups.

Please report a suspected vulnerability privately to **dnmtechlabs@gmail.com**. Include the affected version, a concise reproduction procedure, and any relevant redacted logs. Do not attach private keys, firmware passwords, system serial numbers, or other sensitive information.

GitHub Actions produces test/build artifacts only; successful CI does not imply Secure Boot trust or a signature accepted by the destination firmware. Secure Boot enrollment and signing must be handled separately by the system owner.
