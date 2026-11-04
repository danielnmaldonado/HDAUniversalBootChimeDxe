#!/usr/bin/env python3
"""Execution tests for the opt-in UEFI signing helper with mock tools.

Mocks prove atomic publication/failure behavior, not real cryptographic
signatures or firmware trust. Actual signing requires real Secure Boot keys.
"""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIGNER = ROOT / "Tools" / "Sign-UEFI-SecureBoot.command"


class SecureBootSigningHelperTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="hdau-sign-fixture-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.key = self.root / 'db private key.pem'
        self.cert = self.root / 'db certificate.pem'
        self.original = self.root / 'input driver.efi'
        self.signed = self.root / 'signed driver.efi'
        self.key.write_text('mock secret - never printed', encoding='utf-8')
        self.cert.write_text('mock certificate', encoding='utf-8')
        self.original.write_bytes(b'MZ MOCK EFI\n')
        self.tool('sbsign', '''#!/bin/sh
[ "$1" = "--key" ] && [ -f "$2" ] && [ "$3" = "--cert" ] && [ -f "$4" ] &&
[ "$5" = "--output" ] && [ -s "$7" ] || exit 9
cp "$7" "$6"
printf '\\nMOCK SIGNED\\n' >> "$6"
''')
        self.tool('sbverify', '''#!/bin/sh
[ "$1" = "--cert" ] && [ -f "$2" ] && [ -s "$3" ] || exit 9
grep -q 'MOCK SIGNED' "$3" || exit 8
''')
        self.env = {**os.environ, 'PATH': str(self.bin) + os.pathsep + os.environ['PATH']}

    def tool(self, name, body):
        path = self.bin / name
        path.write_text(body, encoding='utf8')
        path.chmod(0o755)

    def invoke(self, *names):
        args = list(names) or [self.key, self.cert, self.original, self.signed]
        return subprocess.run(['bash', str(SIGNER), *(str(p) for p in args)],
                              text=True, capture_output=True, env=self.env, timeout=20)

    def test_sign_and_verify_publishes_after_verification(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.signed.read_bytes(), b'MZ MOCK EFI\n\nMOCK SIGNED\n')
        self.assertEqual(self.original.read_bytes(), b'MZ MOCK EFI\n')
        self.assertNotIn('mock secret', result.stdout + result.stderr)
        self.assertEqual(list(self.root.glob('.hdau-sign.*')), [])

    def test_verifier_failure_leaves_no_partially_signed_output(self):
        self.tool('sbverify', '#!/bin/sh\nexit 42\n')
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.signed.exists())
        self.assertEqual(list(self.root.glob('.hdau-sign.*')), [])

    def test_refuses_existing_or_symlink_output_and_missing_arguments(self):
        self.assertEqual(self.invoke(self.key, self.cert, self.original).returncode, 64)
        self.signed.write_bytes(b'IMPORTANT EXISTING DATA')
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertEqual(self.signed.read_bytes(), b'IMPORTANT EXISTING DATA')
        self.signed.unlink()
        self.signed.symlink_to(self.original)
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertEqual(self.original.read_bytes(), b'MZ MOCK EFI\n')

    def test_concurrent_output_creation_is_never_overwritten(self):
        self.tool('sbverify', '''#!/bin/sh
[ "$1" = "--cert" ] || exit 9
printf 'CONCURRENT FILE' > "$HDAU_FAKE_CONCURRENT_DEST"
''')
        self.env['HDAU_FAKE_CONCURRENT_DEST'] = str(self.signed)
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.signed.read_bytes(), b'CONCURRENT FILE')
        self.assertEqual(list(self.root.glob('.hdau-sign.*')), [])

    def test_symlink_to_directory_race_fails_without_misplacing_signed_image(self):
        outside = self.root / 'unexpected output directory'
        outside.mkdir()
        self.tool('sbverify', '''#!/bin/sh
[ "$1" = "--cert" ] && [ -s "$3" ] || exit 9
ln -s "$HDAU_FAKE_UNEXPECTED_DIR" "$HDAU_FAKE_CONCURRENT_DEST"
''')
        self.env['HDAU_FAKE_CONCURRENT_DEST'] = str(self.signed)
        self.env['HDAU_FAKE_UNEXPECTED_DIR'] = str(outside)
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(self.signed.is_symlink())
        self.assertEqual(list(outside.iterdir()), [], "signed image escaped into attacker directory")
        self.assertEqual(list(self.root.glob('.hdau-sign.*')), [])

    def test_directory_appearing_at_output_does_not_receive_signed_file(self):
        self.tool('sbverify', '''#!/bin/sh
[ "$1" = "--cert" ] && [ -s "$3" ] || exit 9
mkdir "$HDAU_FAKE_CONCURRENT_DEST"
''')
        self.env['HDAU_FAKE_CONCURRENT_DEST'] = str(self.signed)
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(self.signed.is_dir())
        self.assertEqual(list(self.signed.iterdir()), [], "signed image escaped into directory")
        self.assertEqual(list(self.root.glob('.hdau-sign.*')), [])

    def test_refuses_to_replace_original_driver(self):
        result = self.invoke(self.key, self.cert, self.original, self.original)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.original.read_bytes(), b'MZ MOCK EFI\n')


if __name__ == '__main__':
    unittest.main()
