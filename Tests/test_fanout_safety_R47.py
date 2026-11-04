#!/usr/bin/env python3
"""Host execution regressions for fanout mutations before the PCM DMA start.

These tests compile the actual C route function with a controlled PCI/codec
fixture.  They verify fail-closed behavior; they are not firmware tests.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_hardware_volume_30 import definition

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.c"

C_PREFIX = r'''
#include <stdint.h>
#include <stddef.h>
#define STATIC static
#define CONST const
#define UINTN size_t
#define UINT8 uint8_t
#define CHAR8 char
#define UINT32 uint32_t
#define BOOLEAN int
#define TRUE 1
#define FALSE 0
#define EFI_STATUS int
#define EFI_SUCCESS 0
#define EFI_UNSUPPORTED 3
#define EFI_DEVICE_ERROR 7
#define EFI_INVALID_PARAMETER 2
#define EFI_ERROR(x) ((x) != EFI_SUCCESS && (x) != EFI_UNSUPPORTED)
#define HDA_PARAM_PIN_CAP 12
#define HDA_VERB_GET_CONFIG_DEFAULT 0xF1C
#define HDA_VERB12(verb,payload) ((verb)<<8 | (payload))
#define HDA_PIN_CAP_HDMI 0x8000
#define HDA_PIN_CAP_DISPLAYPORT 0x4000
#define HdaLog(...) ((void)0)
typedef struct {int unused;} EFI_PCI_IO_PROTOCOL;
static EFI_PCI_IO_PROTOCOL pci;
static struct {BOOLEAN AllowDigitalPins;} mHdaCfg = {FALSE};
static BOOLEAN mHdaVolumeReadbackMismatch = FALSE;
static EFI_STATUS limit_status, failure_stage;
static UINTN mutations;
static EFI_STATUS HdaGetParamChecked(EFI_PCI_IO_PROTOCOL *io, UINT8 codec,
                                     UINT8 nid, UINT8 param, UINT32 *val) {
  (void)io;(void)codec;(void)nid;(void)param;
  if (failure_stage == 1) return EFI_DEVICE_ERROR;
  *val=0;
  return EFI_SUCCESS;
}
static EFI_STATUS HdaImmediateCommand(EFI_PCI_IO_PROTOCOL *io, UINT8 codec,
                                      UINT8 nid, UINT32 verb, UINT32 *val) {
  (void)io;(void)codec;(void)nid;(void)verb;
  if (failure_stage == 2) return EFI_DEVICE_ERROR;
  *val=0x90170110;
  return EFI_SUCCESS;
}
static BOOLEAN HdaPinDefaultIsDigital(UINT32 config) {(void)config;return FALSE;}
static EFI_STATUS HdaSetWidgetPowerD0(EFI_PCI_IO_PROTOCOL *io, UINT8 codec, UINT8 nid) {
  (void)io;(void)codec;(void)nid;
  mutations++;
  return failure_stage == 3 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS HdaProgramAmpSafeUnity(EFI_PCI_IO_PROTOCOL *io,UINT8 codec,
                                        UINT8 afg,UINT8 nid,const char *why) {
  (void)io;(void)codec;(void)afg;(void)nid;(void)why;
  mutations++;
  return failure_stage == 4 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS HdaProgramConnectionPath(EFI_PCI_IO_PROTOCOL *io,UINT8 codec,
                    UINT8 afg,const UINT8 *path,UINTN n,const char *why) {
  (void)io;(void)codec;(void)afg;(void)path;(void)n;(void)why;
  mutations++;
  return failure_stage == 5 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS HdaProgramOutputPin(EFI_PCI_IO_PROTOCOL *io,UINT8 codec,
                                      UINT8 pin,const char *why) {
  (void)io;(void)codec;(void)pin;(void)why;
  mutations++;
  return failure_stage == 6 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS HdaProgramOutputDacStream(EFI_PCI_IO_PROTOCOL *io,UINT8 codec,
                                      UINT8 afg,UINT8 dac,const char *why) {
  (void)io;(void)codec;(void)afg;(void)dac;(void)why;
  mutations++;
  return failure_stage == 7 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS HdaLimitFanoutHardwareVolume(EFI_PCI_IO_PROTOCOL *io, UINT8 codec,
                                UINT8 afg, UINT8 dac, const UINT8 *path,
                                UINTN n,const char *why) {
  (void)io;(void)codec;(void)afg;(void)dac;(void)path;(void)n;(void)why;
  mutations++;
  return limit_status;
}
static EFI_STATUS HdaNormalizeExactSuccess(EFI_STATUS status) { return status; }
'''

C_MAIN = r'''
int main(void) {
  const UINT8 path[] = {0x14, 0x0c, 0x02};
  int stage;
  for (stage=1; stage<=7; ++stage) {
    mHdaVolumeReadbackMismatch=FALSE;
    failure_stage=stage;
    limit_status=EFI_SUCCESS;
    mutations=0;
    EFI_STATUS st=HdaProgramAnalogOutputPath(&pci,0,1,0x14,2,path,3,"test");
    if (st != EFI_DEVICE_ERROR) return 10+stage;
    /* Preflight failures have not touched the route; mutation failures must
       invalidate the entire candidate before the driver starts DMA. */
    if ((int)mHdaVolumeReadbackMismatch != (stage>=3)) return 20+stage;
    if ((stage<=2 && mutations != 0) || (stage>=3 && mutations == 0)) return 30+stage;
  }
  mHdaVolumeReadbackMismatch=FALSE;
  failure_stage=0;
  limit_status=EFI_DEVICE_ERROR;
  mutations=0;
  if (HdaProgramAnalogOutputPath(&pci,0,1,0x14,2,path,3,"test") != EFI_DEVICE_ERROR) return 50;
  if (!mHdaVolumeReadbackMismatch || mutations < 2) return 51;
  /* A codec with no usable amp retains the documented 30% PCM fallback. */
  mHdaVolumeReadbackMismatch=FALSE;
  limit_status=EFI_UNSUPPORTED;
  if (HdaProgramAnalogOutputPath(&pci,0,1,0x14,2,path,3,"test") != EFI_SUCCESS) return 52;
  if (mHdaVolumeReadbackMismatch) return 53;
  mHdaVolumeReadbackMismatch=FALSE;
  limit_status=EFI_SUCCESS;
  if (HdaProgramAnalogOutputPath(&pci,0,1,0x14,2,path,3,"test") != EFI_SUCCESS) return 54;
  if (mHdaVolumeReadbackMismatch) return 55;
  return 0;
}
'''

class FanoutMutationSafetyTests(unittest.TestCase):
    def test_fanout_error_after_mutation_invalidates_playback_candidate(self):
        cc = shutil.which("clang") or shutil.which("gcc")
        if cc is None:
            raise unittest.SkipTest("C compiler unavailable")
        source = SOURCE.read_text(encoding="utf-8")
        fn = definition(source, "HdaProgramAnalogOutputPath")
        with tempfile.TemporaryDirectory(prefix="hdau-fanout-R47-") as d:
            cfile=Path(d)/"fanout.c"
            exe=Path(d)/"fanout"
            cfile.write_text(C_PREFIX+"\n"+fn+"\n"+C_MAIN,encoding="utf-8")
            flags=["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-function", "-Wno-unused-parameter"]
            if os.environ.get("HDAU_C_SANITIZERS"):
                flags.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])
            build=subprocess.run([cc,*flags,str(cfile),"-o",str(exe)],
                                 capture_output=True,text=True,timeout=30)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
            self.assertEqual(run.returncode,0,"exit=%s; %s %s"%(run.returncode,run.stdout,run.stderr))

    def test_selected_path_checks_fanout_error_before_dma_start(self):
        source=SOURCE.read_text(encoding="utf-8")
        for fn in ("HdaUnmutePath", "HdaPostStartCodecReapply"):
            region=definition(source,fn)
            self.assertIn("if (mHdaVolumeReadbackMismatch)",region)
            self.assertIn("EFI_COMPROMISED_DATA",region)
