#!/usr/bin/env python3
"""Local R45 build-gate and provenance regressions."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReleaseIntegrationR45Tests(unittest.TestCase):
    def test_pr_runs_real_efi_build_and_does_not_publish_release_artifact(self):
        wf = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        firmware = wf.split("  firmware-build:\n", 1)[1]
        self.assertNotIn("if: github.event_name != 'pull_request'", firmware)
        self.assertIn("needs: source-tests", firmware)
        self.assertIn("runs-on: macos-15-intel", firmware)
        self.assertIn("bash ./Build-HDAUniversalBootChimeDxe.command", firmware)
        self.assertIn("if: success() && github.event_name != 'pull_request'", firmware)
        self.assertIn("permissions:\n  contents: read", wf)

    def test_ci_syntax_gate_includes_new_python_tests(self):
        wf = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        self.assertIn("sorted(Path('Tests').glob('test_*.py'))", wf)
        self.assertIn("sorted(Path('Tools').glob('*.py'))", wf)
        self.assertIn("ast.parse(filename.read_text", wf)

    def test_version_release_and_funding_integrity(self):
        names = (
            "HDAUniversalBootChimeDxe/HdaUefi.h",
            "HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf",
            "HDAUniversalBootChimeDxePkg.dsc",
            "Build-HDAUniversalBootChimeDxe.command",
            "Enable-HDAUCodecDump-NextBoot.command",
            "HDAUChimeRelease.ini",
            "HDAUChimeDebug.ini",
        )
        for name in names:
            with self.subTest(name=name):
                self.assertIn("1.0.87", (ROOT / name).read_text(encoding="utf-8"))
        self.assertEqual((ROOT / '.github/FUNDING.yml').read_text(encoding='utf-8'),
                         'ko_fi: olarila\n'
                         'custom:\n'
                         '  - "https://www.paypal.com/donate/?hosted_button_id=AA4QBFJQYW2N2"\n'
                         '  - "https://revolut.me/olarila"\n'
                         '  - "https://remit.ly/pay/danielm2937"\n'
                         '  - "https://mpago.la/2wFPzHP"\n')

    def test_historical_build_logs_are_preserved_but_not_current(self):
        doc = (ROOT / "docs/CI.md").read_text(encoding="utf-8")
        self.assertIn("Historical diagnostics", doc)
        self.assertIn("not** the build log", doc)
        # The tracked file starts with the original audit log. During a real
        # build, the wrapper deliberately replaces it with the current run's
        # master log BEFORE running this same test suite. Both are valid;
        # an unrelated stale version is not.
        first_line = (ROOT / 'Release/Logs/BUILD-COMPILE.log').read_text(
            encoding='utf-8').splitlines()[0]
        self.assertIn(first_line, {
            '[HDAUniversalBootChimeDxe] ===== HDAUniversalBootChimeDxe 1.0.82-final-audit build =====',
            '[HDAUniversalBootChimeDxe] ===== HDAUniversalBootChimeDxe 1.0.87-final-audit build =====',
        })


if __name__ == "__main__":
    unittest.main()
