#!/usr/bin/env python3
"""R48 regression: UEFI warnings cannot masquerade as successful HDA verbs.

This executes the unmodified HdaImmediateCommand function extracted from the
active driver with injected transport EFI_STATUS values. It does not emulate
MMIO timing or prove EFI hardware support.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_hardware_volume_30 import definition

SOURCE = Path(__file__).resolve().parents[1] / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.c"

C_PREFIX = r'''
#include <stdint.h>
#include <stdio.h>
#define STATIC static
#define UINT8 uint8_t
#define UINT32 uint32_t
#define EFI_STATUS uint64_t
#define EFI_SUCCESS ((EFI_STATUS)0)
#define EFI_WARN_UNKNOWN ((EFI_STATUS)1)
#define EFI_DEVICE_ERROR ((EFI_STATUS)(UINT64_C(1)<<63 | 7))
#define EFI_ACCESS_DENIED ((EFI_STATUS)(UINT64_C(1)<<63 | 15))
#define EFI_ERROR(s) (((s) & (UINT64_C(1)<<63)) != 0)
#define EFI_PCI_IO_PROTOCOL int
#define HdaLog(...) ((void)0)
static struct {int CorbRirbFallback; int Debug;} mHdaCfg;
static int mHdaCorbRirbFallbackDisabled;
static EFI_STATUS immediate_status, fallback_status;
static int immediate_calls, fallback_calls;
static EFI_STATUS HdaImmediateCommandRaw(EFI_PCI_IO_PROTOCOL *i,UINT8 c,UINT8 n,UINT32 v,UINT32 *p){
(void)i;(void)c;(void)n;(void)v; if(p) *p=0xDEADBEEF; immediate_calls++; return immediate_status;}
static EFI_STATUS HdaCorbRirbCommand(EFI_PCI_IO_PROTOCOL *i,UINT8 c,UINT8 n,UINT32 v,UINT32 *p){
(void)i;(void)c;(void)n;(void)v; if(p) *p=0xFACECAFE; fallback_calls++; return fallback_status;}
'''

C_MAIN = r'''
static int check(int use_fb, EFI_STATUS immediate, EFI_STATUS fallback,
                 EFI_STATUS expected, int expected_fb, UINT32 expected_response) {
  EFI_PCI_IO_PROTOCOL pci = 0;
  UINT32 response = 0xFFFFFFFF;
  mHdaCfg.CorbRirbFallback=use_fb;
  immediate_status=immediate; fallback_status=fallback;
  immediate_calls=0; fallback_calls=0;
  EFI_STATUS got = HdaImmediateCommand(&pci,0,2,0x12345,&response);
  if(got!=expected || immediate_calls!=1 || fallback_calls!=expected_fb || response!=expected_response) {
    fprintf(stderr,"unexpected status=%#llx expected=%#llx fallback=%d wanted=%d response=%#x wanted=%#x\n",
            (unsigned long long)got,(unsigned long long)expected,
            fallback_calls,expected_fb,response,expected_response);
    return 1;
  }
  return 0;
}
int main(void) {
  /* A warning is ambiguous completion, so retrying a SET is unsafe. */
  if(check(0,EFI_WARN_UNKNOWN,EFI_SUCCESS,EFI_DEVICE_ERROR,0,0)) return 11;
  if(check(1,EFI_WARN_UNKNOWN,EFI_SUCCESS,EFI_DEVICE_ERROR,0,0)) return 12;
  /* A fallback warning must never be reported as clean success. */
  if(check(1,EFI_DEVICE_ERROR,EFI_WARN_UNKNOWN,EFI_DEVICE_ERROR,1,0)) return 13;
  /* A normal clean command is unaffected. */
  if(check(1,EFI_SUCCESS,EFI_WARN_UNKNOWN,EFI_SUCCESS,0,0xDEADBEEF)) return 14;
  /* An ordinary device error can still use an explicitly successful fallback. */
  if(check(1,EFI_DEVICE_ERROR,EFI_SUCCESS,EFI_SUCCESS,1,0xFACECAFE)) return 15;
  /* Terminal failure must not leak partial read data. */
  if(check(0,EFI_ACCESS_DENIED,EFI_SUCCESS,EFI_ACCESS_DENIED,0,0)) return 16;
  /* If fallback was disabled after ownership failure, preserve its real error. */
  mHdaCorbRirbFallbackDisabled=1;
  if(check(1,EFI_DEVICE_ERROR,EFI_ACCESS_DENIED,EFI_ACCESS_DENIED,1,0)) return 17;
  return 0;
}
'''


class TransportWarningR48Tests(unittest.TestCase):
    def test_immediate_verb_distinguishes_exact_success_warning_and_error(self):
        compiler = shutil.which("clang") or shutil.which("gcc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        source = SOURCE.read_text(encoding="utf-8")
        function = definition(source, "HdaImmediateCommand")
        with tempfile.TemporaryDirectory(prefix="hdau-r48-transport-") as d:
            c_path = Path(d) / "transport.c"
            exe = Path(d) / "transport"
            c_path.write_text(C_PREFIX + "\n" + function + "\n" + C_MAIN, encoding="utf-8")
            args = [compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-Wno-unused-parameter"]
            if os.getenv("HDAU_C_SANITIZERS"):
                args += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            built = subprocess.run(args + [str(c_path), "-o", str(exe)],
                                   capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            self.assertEqual(ran.returncode, 0, f"exit={ran.returncode}: {ran.stdout} {ran.stderr}")

    def test_pci_mmio_wrappers_still_normalize_warning_statuses(self):
        source = SOURCE.read_text(encoding="utf-8")
        for method in ("HdaMemRead8", "HdaMemRead16", "HdaMemRead32",
                       "HdaMemWrite8", "HdaMemWrite16", "HdaMemWrite32"):
            with self.subTest(method=method):
                self.assertIn("HdaNormalizeExactSuccess(Status)", definition(source, method))


if __name__ == "__main__":
    unittest.main()
