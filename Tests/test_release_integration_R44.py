#!/usr/bin/env python3
"""Repository integration checks for the R44 metadata and CI dependency fix."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReleaseIntegrationR44Tests(unittest.TestCase):
    def test_funding_includes_all_requested_sponsors(self):
        self.assertEqual(
            (ROOT / ".github" / "FUNDING.yml").read_text(encoding="utf-8"),
            'ko_fi: olarila\n'
            'custom:\n'
            '  - "https://www.paypal.com/donate/?hosted_button_id=AA4QBFJQYW2N2"\n'
            '  - "https://revolut.me/olarila"\n'
            '  - "https://remit.ly/pay/danielm2937"\n'
            '  - "https://mpago.la/2wFPzHP"\n',
        )

    def test_macos_ci_prepares_missing_nasm_without_changing_local_build(self):
        workflow = (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
        self.assertIn('if ! command -v nasm >/dev/null 2>&1; then', workflow)
        self.assertIn('HOMEBREW_NO_AUTO_UPDATE=1 brew install nasm', workflow)
        self.assertLess(workflow.index('brew install nasm'), workflow.index('bash ./Build-HDAUniversalBootChimeDxe.command'))
        self.assertEqual(workflow.count('brew install nasm'), 1)

    def test_macos_ci_prepares_mtoc_converter(self):
        workflow = (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
        self.assertIn('if ! test -x /usr/local/bin/mtoc; then', workflow)
        self.assertIn('HOMEBREW_NO_AUTO_UPDATE=1 brew install ocmtoc', workflow)
        self.assertIn('test -x /usr/local/bin/mtoc', workflow)
        self.assertLess(workflow.index('brew install ocmtoc'), workflow.index('bash ./Build-HDAUniversalBootChimeDxe.command'))

    def test_active_release_metadata_is_consistent(self):
        files = {
            "HDAUniversalBootChimeDxe/HdaUefi.h": '1.0.87-final-audit',
            "HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf": 'VERSION_STRING                 = 1.0.87',
            "HDAUniversalBootChimeDxePkg.dsc": 'PLATFORM_VERSION               = 1.0.87',
            "Build-HDAUniversalBootChimeDxe.command": 'VERSION="1.0.87-final-audit"',
            "HDAUChimeRelease.ini": '1.0.87-final-audit',
            "HDAUChimeDebug.ini": '1.0.87-final-audit',
            "Enable-HDAUCodecDump-NextBoot.command": '1.0.87-final-audit',
        }
        for path, needle in files.items():
            with self.subTest(path=path):
                self.assertIn(needle, (ROOT / path).read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
