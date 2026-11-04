#!/usr/bin/env python3
"""Regression for the EDK2 workspace bootstrap's stdout contract.

When captured by prepare_edk2(), bootstrap_edk2_if_needed() must return
only the checkout directory on stdout. Progress and pin verification must be
sent to stderr, otherwise WORKSPACE becomes a multi-line invalid path.
"""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "Build-HDAUniversalBootChimeDxe.command"


class Edk2BootstrapStdoutTests(unittest.TestCase):
    def test_pinned_bootstrap_returns_only_workspace_path(self):
        source = BUILD.read_text(encoding="utf-8")
        start = source.index("bootstrap_edk2_if_needed() {")
        end = source.index("prepare_edk2() {", start)
        real_functions = source[start:end]

        with tempfile.TemporaryDirectory(prefix="hdau-edk2-stdout-") as directory:
            root = Path(directory)
            checkout = root / "edk2"
            checkout.mkdir()
            (checkout / "MdePkg").mkdir()
            (checkout / "edksetup.sh").write_text("# test fixture\n", encoding="utf-8")
            subprocess.run(["git", "init", "-q", str(checkout)], check=True, capture_output=True)
            subprocess.run(
                ["git", "-C", str(checkout), "-c", "user.name=CI Test",
                 "-c", "user.email=ci@example.invalid", "commit", "--allow-empty", "-qm", "fixture"],
                check=True, capture_output=True,
            )
            commit = subprocess.check_output(["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
            harness = """set -euo pipefail
log() { printf '[HDAUniversalBootChimeDxe] %s\\n' "$*"; }
warn() { printf 'WARNING: %s\\n' "$*" >&2; }
fail() { printf 'ERROR: %s\\n' "$*" >&2; exit 1; }
require_command() { command -v "$1" >/dev/null; }
find_edk2_workspace() { return 1; }
""" + real_functions + """
workspace="$(bootstrap_edk2_if_needed)"
printf '%s' "$workspace"
"""
            result = subprocess.run(
                ["bash", "-c", harness], capture_output=True, text=True,
                timeout=30,
                env={
                    "PATH": __import__("os").environ.get("PATH", ""),
                    "HOME": str(root),
                    "AUTO_FETCH_EDK2": "1",
                    "EDK2_DEFAULT_DIR": str(checkout),
                    "EDK2_REF": "HEAD",
                    "EDK2_EXPECTED_COMMIT": commit,
                },
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, str(checkout), "EDK2 bootstrap polluted captured WORKSPACE")
            self.assertIn("Verified EDK2 reference:", result.stderr)
            self.assertIn("Verified EDK2 expected commit:", result.stderr)


if __name__ == "__main__":
    unittest.main()
