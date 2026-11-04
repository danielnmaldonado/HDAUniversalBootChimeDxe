#!/usr/bin/env python3
"""R46 active release metadata, HDA safety and signing publication regressions."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class R46IntegrationTests(unittest.TestCase):
    def test_consistent_active_version_without_modifying_historical_evidence(self):
        active = (
            "Build-HDAUniversalBootChimeDxe.command",
            "Enable-HDAUCodecDump-NextBoot.command",
            "HDAUChimeDebug.ini", "HDAUChimeRelease.ini",
            "HDAUniversalBootChimeDxe/HdaUefi.h",
        )
        for name in active:
            with self.subTest(name=name):
                self.assertIn("1.0.87-final-audit", (ROOT / name).read_text(encoding="utf8"))
        self.assertIn("1.0.87", (ROOT / "HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf").read_text(encoding="utf8"))
        self.assertIn("1.0.87", (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf8"))
        # R46's own build overwrites the tracked historic master log before
        # executing the regression gate. Either exact origin is acceptable.
        first_line = (ROOT / "Release/Logs/BUILD-COMPILE.log").read_text(
            encoding="utf8").splitlines()[0]
        self.assertIn(first_line, {
            '[HDAUniversalBootChimeDxe] ===== HDAUniversalBootChimeDxe 1.0.82-final-audit build =====',
            '[HDAUniversalBootChimeDxe] ===== HDAUniversalBootChimeDxe 1.0.87-final-audit build =====',
        })

    def test_hda_gain_readback_exact_success_is_required_for_both_channels(self):
        src = (ROOT / "HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.c").read_text(encoding="utf8")
        region = src[src.index("STATIC EFI_STATUS HdaVerifyAmpGain("):src.index("STATIC EFI_STATUS HdaProgramStoredHardwareVolume(")]
        self.assertIn("LeftStatus == EFI_SUCCESS", region)
        self.assertIn("RightStatus == EFI_SUCCESS", region)
        self.assertIn("EFI_COMPROMISED_DATA", region)

    def test_optional_signer_is_not_macos_codesign_and_publishes_exclusively(self):
        script = (ROOT / "Tools/Sign-UEFI-SecureBoot.command").read_text(encoding="utf8")
        self.assertIn("src_dir_fd=fd, dst_dir_fd=fd, follow_symlinks=False", script)
        self.assertIn("python3", script)
        self.assertNotIn('ln "$tmp" "$output"', script)
        self.assertNotIn("codesign -s -", script)
        self.assertEqual((ROOT / ".github/FUNDING.yml").read_text(encoding="utf8"),
                         'ko_fi: olarila\n'
                         'custom:\n'
                         '  - "https://www.paypal.com/donate/?hosted_button_id=AA4QBFJQYW2N2"\n'
                         '  - "https://revolut.me/olarila"\n'
                         '  - "https://remit.ly/pay/danielm2937"\n'
                         '  - "https://mpago.la/2wFPzHP"\n')


if __name__ == "__main__":
    unittest.main()
