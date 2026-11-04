#!/usr/bin/env python3
"""Executable regression coverage for independent 30% PCM and codec attenuation.

Host mocks exercise the actual C volume functions, not a second implementation
of the gain calculations or the path selection logic.  Firmware HDA playback
still requires testing on hardware with codec and amplifier measurements.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.c"
HEADER = SOURCE.with_name("HdaUefi.h")


def definition(source, name):
    pattern = r"(?m)^STATIC\s+[^;\n]*?\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{"
    match = re.search(pattern, source, re.S)
    if not match:
        raise AssertionError(f"missing firmware function: {name}")
    brace = source.index("{", match.start(), match.end())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        if source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start(): index + 1]
    raise AssertionError(f"unclosed firmware function: {name}")


PREFIX = r"""
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define CONST const
#define UINT8 uint8_t
#define CHAR8 char
#define UINT16 uint16_t
#define UINT32 uint32_t
#define UINT64 uint64_t
#define UINTN size_t
#define BOOLEAN int
#define TRUE 1
#define FALSE 0
#define EFI_STATUS int
#define EFI_SUCCESS 0
#define EFI_UNSUPPORTED 3
#define EFI_INVALID_PARAMETER 2
#define EFI_DEVICE_ERROR 7
#define EFI_COMPROMISED_DATA 33
#define EFI_WARN_UNKNOWN_GLYPH 1
#define EFI_ERROR(x) ((x) >= 2) /* Simulate UEFI warning statuses */
#define BIT31 (1U << 31)
#define HDAU_BOOT_CHIME_VOLUME_PERCENT 30U
#define HDAU_BOOT_CHIME_GAIN_Q8 ((HDAU_BOOT_CHIME_VOLUME_PERCENT * 256U + 50U) / 100U)
#define HDAU_PCM_TRANSPORT_GAIN_Q8 HDAU_BOOT_CHIME_GAIN_Q8
#define HDA_AWCAP_OUT_AMP_PRESENT 1U
#define HDA_AWCAP_IN_AMP_PRESENT 2U
#define HDA_PARAM_AUDIO_WIDGET_CAP 9U
#define HDA_PARAM_AMP_OUT_CAP 10U
#define HDA_PARAM_AMP_IN_CAP 11U
#define HDA_VERB_GET_AMP_GAIN_MUTE 0xBU
#define HDA_VERB_SET_AMP_GAIN_MUTE 0x3U
#define HDA_VERB4(verb,payload) (((verb) << 16) | ((payload) & 0xFFFFU))
#define HDA_AMP_INDEX_MAX 15U
#define HDAU_MAX_CONNECTIONS 128U
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define HdaLog(...) ((void)0)
typedef struct { int id; } EFI_PCI_IO_PROTOCOL;
static struct { UINTN VolumeQ8; } mHdaCfg = { HDAU_BOOT_CHIME_GAIN_Q8 };
static EFI_PCI_IO_PROTOCOL io = { 0 };
static BOOLEAN mHdaVolumeReadbackMismatch = FALSE;
static UINT32 widget_caps[128], output_caps[128], input_caps[128];
static UINT8 edges[128][16];
static UINTN edge_counts[128];
static UINT8 recorded_nid;
static UINT16 recorded_payload;
static unsigned int gain_writes;
static int gain_readback_mode;
static int refuse_caps_for_nid = -1;
static void reset_mock(void) {
  memset(widget_caps, 0, sizeof(widget_caps));
  memset(output_caps, 0, sizeof(output_caps));
  memset(input_caps, 0, sizeof(input_caps));
  memset(edges, 0, sizeof(edges));
  memset(edge_counts, 0, sizeof(edge_counts));
  recorded_nid = 0;
  recorded_payload = 0;
  gain_writes = 0;
  gain_readback_mode = 0;
  mHdaVolumeReadbackMismatch = FALSE;
  refuse_caps_for_nid = -1;
  mHdaCfg.VolumeQ8 = HDAU_BOOT_CHIME_GAIN_Q8;
}
static EFI_STATUS HdaGetParamChecked(EFI_PCI_IO_PROTOCOL *pci, UINT8 codec, UINT8 nid,
                                     UINT32 param, UINT32 *value) {
  (void)pci; (void)codec;
  if (nid == refuse_caps_for_nid) return EFI_DEVICE_ERROR;
  if (param != HDA_PARAM_AUDIO_WIDGET_CAP || value == NULL) return EFI_DEVICE_ERROR;
  *value = widget_caps[nid];
  return EFI_SUCCESS;
}
static EFI_STATUS HdaGetEffectiveAmpCapsChecked(EFI_PCI_IO_PROTOCOL *pci, UINT8 codec,
         UINT8 afg, UINT8 nid, UINT32 awcaps, UINT32 param, UINT32 *result) {
  (void)pci; (void)codec; (void)afg; (void)awcaps;
  if (nid == refuse_caps_for_nid || result == NULL) return EFI_DEVICE_ERROR;
  *result = (param == HDA_PARAM_AMP_IN_CAP) ? input_caps[nid] : output_caps[nid];
  return EFI_SUCCESS;
}
static EFI_STATUS HdaSetVerb(EFI_PCI_IO_PROTOCOL *pci, UINT8 codec, UINT8 nid, UINT32 payload) {
  (void)pci; (void)codec;
  if (((payload >> 16) & 0xFU) != HDA_VERB_SET_AMP_GAIN_MUTE) return EFI_DEVICE_ERROR;
  recorded_nid = nid;
  recorded_payload = (UINT16)payload;
  gain_writes++;
  return EFI_SUCCESS;
}
static EFI_STATUS HdaImmediateCommand(EFI_PCI_IO_PROTOCOL *pci, UINT8 codec, UINT8 nid,
                                      UINT32 cmd, UINT32 *reply) {
  (void)pci; (void)codec;
  if (reply == NULL || nid != recorded_nid ||
      ((cmd >> 16) & 0xFU) != HDA_VERB_GET_AMP_GAIN_MUTE) return EFI_DEVICE_ERROR;
  BOOLEAN is_left = ((cmd & 0x2000U) != 0);
  if (gain_readback_mode == 11) {
    *reply = recorded_payload & 0xFFU;
    return EFI_WARN_UNKNOWN_GLYPH; /* GET did not return clean success */
  }
  if (gain_readback_mode == 2 ||
      (gain_readback_mode == 4 && gain_writes > 1) ||
      ((gain_readback_mode == 9 || gain_readback_mode == 10) && gain_writes > 1) ||
      ((gain_readback_mode == 5 || gain_readback_mode == 7 || gain_readback_mode == 9) && !is_left) ||
      ((gain_readback_mode == 6 || gain_readback_mode == 8 || gain_readback_mode == 10) && is_left))
    return EFI_DEVICE_ERROR; /* GET unavailable on specified channel */
  if (gain_readback_mode == 1 || gain_readback_mode == 5 || gain_readback_mode == 6 ||
      gain_readback_mode == 9 || gain_readback_mode == 10 ||
      ((gain_readback_mode == 3 || gain_readback_mode == 4) && gain_writes == 1)) {
    *reply = 0x3fU; /* GET succeeds, but returns an unsafe gain */
  } else {
    *reply = recorded_payload & 0xFFU;
  }
  return EFI_SUCCESS;
}
static UINTN HdaGetConnections(EFI_PCI_IO_PROTOCOL *pci, UINT8 codec, UINT8 nid,
                                UINT8 *connections, UINTN capacity) {
  (void)pci; (void)codec;
  if (edge_counts[nid] > capacity) return 0;
  for (UINTN i=0; i<edge_counts[nid]; i++) connections[i] = edges[nid][i];
  return edge_counts[nid];
}
"""


class HardwareThirtyPercentExecutableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("cc") or shutil.which("clang")
        if cls.compiler is None:
            raise unittest.SkipTest("host C compiler required to execute HDA volume tests")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.header = HEADER.read_text(encoding="utf-8")

    def compile_run(self, body, *function_names, prelude=""):
        begin = self.source.index("STATIC CONST UINT16 mHdaPerceptualAttenuationQuarterDb[101] = {")
        end = self.source.index("};", begin) + 2
        functions = "\n\n".join(definition(self.source, f) for f in function_names)
        with tempfile.TemporaryDirectory(prefix="hdau-hw30-") as folder:
            source_file = Path(folder) / "volume.c"
            executable = Path(folder) / "volume"
            source_file.write_text(
                PREFIX + "\n" + self.source[begin:end] + "\n" + prelude + "\n" + functions + "\n" + body,
                encoding="utf-8",
            )
            proc = subprocess.run(
                [self.compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-unused-variable",
                 str(source_file), "-o", str(executable)],
                text=True, capture_output=True, timeout=40,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            proc = subprocess.run([str(executable)], text=True, capture_output=True, timeout=20)
            self.assertEqual(proc.returncode, 0, proc.stdout + "\n" + proc.stderr)

    def test_full_30_percent_hardware_at_multiple_quantizations(self):
        self.assertIn("#define HDAU_BOOT_CHIME_VOLUME_PERCENT 30U", self.header)
        self.compile_run(r"""
int main(void) {
  UINTN gain = 0;
  BOOLEAN mute = TRUE;
  UINT32 step075 = (63U << 8) | 28U | (2U << 16);
  if (!HdaAmpVolumeGainFromCaps(step075, &gain, &mute)) return 10;
  if (gain != 14U || mute) return 11; /* 28 zero-dB offset - 14 x 0.75dB */
  UINT32 step050 = (63U << 8) | 30U | (1U << 16);
  if (!HdaAmpVolumeGainFromCaps(step050, &gain, &mute)) return 12;
  if (gain != 9U || mute) return 13; /* 30 zero-dB offset - 21 x 0.5dB */
  UINT32 step100 = (63U << 8) | 31U | (3U << 16);
  if (!HdaAmpVolumeGainFromCaps(step100, &gain, &mute)) return 14;
  if (gain != 20U || mute) return 15; /* ceiling(10.5dB / 1dB) = 11 */
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps")

    def test_every_hda_amp_caps_quantization_respects_the_attenuation_ceiling(self):
        # Every possible step size, number of steps and zero-dB offset in the
        # three 7-bit HDA amplifier capability fields (128**3 cases).
        self.compile_run(r"""
int main(void) {
  for (unsigned int step=0; step<128; step++) {
    for (unsigned int num=0; num<128; num++) {
      for (unsigned int zero=0; zero<128; zero++) {
        UINT32 caps = (step << 16) | (num << 8) | zero;
        UINTN gain = 0;
        BOOLEAN mute = TRUE;
        BOOLEAN supported = HdaAmpVolumeGainFromCaps(caps, &gain, &mute);
        if (supported) {
          unsigned int unity = zero <= num ? zero : num;
          if (mute || gain > unity) return 10;
          if ((unity - gain) * (step + 1U) < 42U) return 11;
          if (gain > num) return 12;
        }
      }
    }
  }
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps")

    def test_codec_without_negative_range_does_not_claim_hardware_30_percent(self):
        self.compile_run(r"""
int main(void) {
  UINTN gain = 999;
  BOOLEAN mute = TRUE;
  UINT32 insufficient = (63U << 8) | 10U | (2U << 16);
  if (HdaAmpVolumeGainFromCaps(insufficient, &gain, &mute)) return 10;
  UINT32 noAttenuation = (31U << 8) | 0U | (3U << 16);
  if (HdaAmpVolumeGainFromCaps(noAttenuation, &gain, &mute)) return 11;
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps")

    def test_dac_and_pin_amps_in_any_route_are_attenuated(self):
        self.compile_run(r"""
int main(void) {
  UINT8 path[] = {0x14, 0x0c, 0x02};
  reset_mock();
  widget_caps[0x02] = HDA_AWCAP_OUT_AMP_PRESENT;
  output_caps[0x02] = (63U << 8) | 28U | (2U << 16);
  if (HdaLimitFanoutHardwareVolume(&io, 0, 1, 0x02, path, 3, "test") != EFI_SUCCESS) return 10;
  if (gain_writes != 1 || recorded_nid != 0x02 || recorded_payload != 0xB00EU) return 11;

  reset_mock();
  widget_caps[0x14] = HDA_AWCAP_OUT_AMP_PRESENT;
  output_caps[0x14] = (63U << 8) | 28U | (2U << 16);
  if (HdaLimitFanoutHardwareVolume(&io, 0, 1, 0x02, path, 3, "test") != EFI_SUCCESS) return 12;
  if (gain_writes != 1 || recorded_nid != 0x14 || recorded_payload != 0xB00EU) return 13;
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
          "HdaVerifyAmpGain", "HdaTryHardwareVolumeOnRouteNode", "HdaLimitFanoutHardwareVolume")

    def test_input_mixer_fallback_uses_selected_connection_index(self):
        self.compile_run(r"""
int main(void) {
  UINT8 path[] = {0x14, 0x0c, 0x02};
  reset_mock();
  widget_caps[0x0c] = HDA_AWCAP_IN_AMP_PRESENT;
  input_caps[0x0c] = (63U << 8) | 28U | (2U << 16);
  edge_counts[0x0c] = 2;
  edges[0x0c][0] = 0x03;
  edges[0x0c][1] = 0x02;
  if (HdaLimitFanoutHardwareVolume(&io, 0, 1, 0x02, path, 3, "test") != EFI_SUCCESS) return 10;
  if (gain_writes != 1 || recorded_nid != 0x0c || recorded_payload != 0x710EU) return 11;
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
          "HdaVerifyAmpGain", "HdaTryHardwareVolumeOnRouteNode", "HdaLimitFanoutHardwareVolume")

    def test_route_rejects_unknown_amp_capabilities(self):
        self.compile_run(r"""
int main(void) {
  UINT8 path[] = {0x14, 0x0c, 0x02};
  reset_mock();
  refuse_caps_for_nid = 0x02;
  EFI_STATUS s = HdaLimitFanoutHardwareVolume(&io, 0, 1, 0x02, path, 3, "test");
  if (s != EFI_DEVICE_ERROR || gain_writes != 0) return 10;
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
          "HdaVerifyAmpGain", "HdaTryHardwareVolumeOnRouteNode", "HdaLimitFanoutHardwareVolume")

    def test_all_fanout_paths_reach_common_limiter_after_DAC_setup(self):
        source = self.source
        route = definition(source, "HdaProgramAnalogOutputPath")
        self.assertIn("HdaLimitFanoutHardwareVolume(", route)
        self.assertLess(route.index("HdaProgramOutputDacStream"), route.index("HdaLimitFanoutHardwareVolume"))
        for function in ("HdaAlcIdLayoutSafeFanout", "HdaEnableLegacyRealtekFanout",
                         "HdaRealtekProgramPinDacProfile"):
            text = definition(source, function)
            self.assertIn("HdaProgramAnalogOutputPath(", text, function)

    def test_selected_amp_is_attenuated_and_reapplied_after_stream_reinit(self):
        volume = definition(self.source, "HdaProgramPlaybackVolume")
        self.assertNotIn("mHdaCfg.VolumeQ8 >= HDAU_PCM_TRANSPORT_GAIN_Q8", volume)
        self.assertIn("HdaTryHardwareOutputVolume(Context, Context->Dac", volume)
        self.assertIn("HdaTryHardwareInputVolume", volume)
        path = definition(self.source, "HdaUnmutePath")
        self.assertGreater(path.index("HdaProgramPlaybackVolume(Context, FALSE"),
                           path.index("HdaAlcIdLayoutSafeFanout(Context)"))
        post = definition(self.source, "HdaPostStartCodecReapply")
        self.assertGreater(post.index("HdaProgramPlaybackVolume(Context, TRUE"),
                           post.index("HdaAlcIdLayoutSafeFanout(Context)"))
        self.assertGreater(post.index("HdaProgramPlaybackVolume(Context, TRUE"),
                           post.index("HdaProgramOutputDacStream(Context->PciIo"))

    def test_selected_path_does_not_skip_codec_amp_at_pcm_30_percent(self):
        source = self.source
        actual = definition(source, "HdaProgramPlaybackVolume")
        # Minimal mock context and callbacks; the real selected-path function
        # must call the DAC amp even when VolumeQ8 equals the PCM ceiling.
        definitions = r"""
typedef struct {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Dac;
  UINT8 Path[8];
  UINTN PathLen;
  BOOLEAN HardwareVolumeApplied;
  BOOLEAN HardwareVolumeInputAmp;
  UINT8 HardwareVolumeNid;
  UINT8 HardwareVolumeInputIndex;
} HDAU_CONTEXT;
static unsigned int selected_volume_calls;
static EFI_STATUS HdaProgramStoredHardwareVolume(HDAU_CONTEXT *ctx, const char *reason) {
  (void)ctx; (void)reason;
  return EFI_SUCCESS;
}
static EFI_STATUS HdaTryHardwareOutputVolume(HDAU_CONTEXT *ctx, UINT8 nid, const char *reason) {
  (void)ctx; (void)nid; (void)reason;
  selected_volume_calls++;
  return EFI_SUCCESS;
}
static EFI_STATUS HdaTryHardwareInputVolume(HDAU_CONTEXT *ctx, UINT8 nid, UINT8 next, const char *reason) {
  (void)ctx; (void)nid; (void)next; (void)reason;
  return EFI_UNSUPPORTED;
}
"""
        body = r"""
int main(void) {
  HDAU_CONTEXT ctx = {0};
  ctx.PciIo = &io;
  ctx.Dac = 2;
  ctx.Path[0] = 0x14;
  ctx.Path[1] = 2;
  ctx.PathLen = 2;
  mHdaCfg.VolumeQ8 = HDAU_PCM_TRANSPORT_GAIN_Q8;
  if (HdaProgramPlaybackVolume(&ctx, FALSE, "selected") != EFI_SUCCESS) return 10;
  if (selected_volume_calls != 1) return 11;
  mHdaCfg.VolumeQ8 = HDAU_PCM_TRANSPORT_GAIN_Q8 + 1;
  if (HdaProgramPlaybackVolume(&ctx, FALSE, "selected") != EFI_UNSUPPORTED) return 12;
  if (selected_volume_calls != 1) return 13;
  return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="hdau-selected30-") as folder:
            path = Path(folder) / "selected.c"
            exe = Path(folder) / "selected"
            path.write_text(PREFIX + "\n" + definitions + "\n" + actual + "\n" + body, encoding="utf8")
            compile_result = subprocess.run(
                [self.compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-unused-variable",
                 str(path), "-o", str(exe)], capture_output=True, text=True, timeout=30
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stderr)


    def test_selected_route_rejects_persistent_confirmed_gain_mismatch(self):
        # Both L/R GET reads complete, but report a value above the 30% target
        # even after a second SET. Do not publish a successful route.
        ctx = r"""
typedef struct {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  UINT8 Afg;
  BOOLEAN HardwareVolumeApplied;
  BOOLEAN HardwareVolumeInputAmp;
  UINT8 HardwareVolumeNid;
  UINT8 HardwareVolumeInputIndex;
} HDAU_CONTEXT;
"""
        self.compile_run(r"""
int main(void) {
  HDAU_CONTEXT ctx = {0};
  reset_mock();
  ctx.PciIo = &io;
  ctx.Codec = 0;
  ctx.Afg = 1;
  ctx.HardwareVolumeApplied = TRUE;
  ctx.HardwareVolumeNid = 2;
  widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
  output_caps[2] = (63U << 8) | 28U | (2U << 16);
  for (int mode = 1; mode <= 4; mode += 3) {
    reset_mock();
    widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
    output_caps[2] = (63U << 8) | 28U | (2U << 16);
    gain_readback_mode = mode;
    if (HdaProgramStoredHardwareVolume(&ctx, "test") != EFI_COMPROMISED_DATA) return 10;
    if (gain_writes != 2 || recorded_payload != 0xB00EU) return 11;
  }
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
             "HdaVerifyAmpGain", "HdaProgramStoredHardwareVolume", prelude=ctx)

    def test_fanout_mismatch_rejects_parent_route_without_claiming_success(self):
        self.compile_run(r"""
int main(void) {
  reset_mock();
  widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
  output_caps[2] = (63U << 8) | 28U | (2U << 16);
  gain_readback_mode = 1;
  UINT8 path[] = {0x14, 0x0c, 0x02};
  EFI_STATUS s = HdaLimitFanoutHardwareVolume(&io, 0, 1, 2, path, 3, "test");
  if (s != EFI_COMPROMISED_DATA || gain_writes != 2) return 10;
  if (!mHdaVolumeReadbackMismatch) return 11;
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
             "HdaVerifyAmpGain", "HdaTryHardwareVolumeOnRouteNode", "HdaLimitFanoutHardwareVolume")

    def test_first_confirmed_mismatch_then_unavailable_read_still_rejects_route(self):
        self.compile_run(r"""
int main(void) {
  reset_mock();
  widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
  output_caps[2] = (63U << 8) | 28U | (2U << 16);
  gain_readback_mode = 4;
  EFI_STATUS s = HdaTryHardwareVolumeOnRouteNode(&io, 0, 1, 2, FALSE, 0, "test");
  if (s != EFI_COMPROMISED_DATA || gain_writes != 2) return 10;
  if (!mHdaVolumeReadbackMismatch) return 11;
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
             "HdaVerifyAmpGain", "HdaTryHardwareVolumeOnRouteNode")

    def test_transient_mismatch_is_recovered_by_second_write(self):
        self.compile_run(r"""
int main(void) {
  reset_mock();
  widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
  output_caps[2] = (63U << 8) | 28U | (2U << 16);
  gain_readback_mode = 3;
  EFI_STATUS s = HdaTryHardwareVolumeOnRouteNode(&io, 0, 1, 2, FALSE, 0, "test");
  if (s != EFI_SUCCESS || gain_writes != 2) return 10;
  if (mHdaVolumeReadbackMismatch) return 11;
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
             "HdaVerifyAmpGain", "HdaTryHardwareVolumeOnRouteNode")

    def test_unavailable_hardware_readback_still_preserves_pcm_fallback(self):
        # A GET transport error is not a confirmed contradictory value.
        self.compile_run(r"""
int main(void) {
  reset_mock();
  widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
  output_caps[2] = (63U << 8) | 28U | (2U << 16);
  gain_readback_mode = 2;
  EFI_STATUS s = HdaTryHardwareVolumeOnRouteNode(&io, 0, 1, 2, FALSE, 0, "test");
  if (s != EFI_SUCCESS || gain_writes != 2) return 10;
  if (mHdaVolumeReadbackMismatch) return 11;
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
             "HdaVerifyAmpGain", "HdaTryHardwareVolumeOnRouteNode")

    def test_fanout_mismatch_fails_closed_before_playback_and_after_stream_start(self):
        initial = definition(self.source, "HdaUnmutePath")
        post = definition(self.source, "HdaPostStartCodecReapply")
        for body in (initial, post):
            self.assertIn("mHdaVolumeReadbackMismatch = FALSE;", body)
            check = body.index("if (mHdaVolumeReadbackMismatch)")
            self.assertGreater(check, body.index("HdaAlcIdLayoutSafeFanout(Context)"))
            self.assertIn("return EFI_COMPROMISED_DATA;", body[check:])
        self.assertLess(initial.index("if (mHdaVolumeReadbackMismatch)"),
                        initial.index('HdaProgramPlaybackVolume(Context, FALSE'))
        self.assertLess(post.index("if (mHdaVolumeReadbackMismatch)"),
                        post.index('HdaProgramPlaybackVolume(Context, TRUE'))
        self.assertIn("return EFI_COMPROMISED_DATA;", definition(self.source, "HdaVerifyAmpGain"))

    def test_partial_stereo_readback_detects_confirmed_mismatch_on_either_channel(self):
        # One GET transport error must never erase a contradictory gain read
        # returned successfully by the other channel.
        context = r"""
typedef struct {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  UINT8 Afg;
  BOOLEAN HardwareVolumeApplied;
  BOOLEAN HardwareVolumeInputAmp;
  UINT8 HardwareVolumeNid;
  UINT8 HardwareVolumeInputIndex;
} HDAU_CONTEXT;
"""
        self.compile_run(r"""
int main(void) {
  HDAU_CONTEXT ctx = {0};
  ctx.PciIo = &io;
  ctx.HardwareVolumeApplied = TRUE;
  ctx.HardwareVolumeNid = 2;
  ctx.Afg = 1;
  UINT8 path[] = {0x14, 0x0c, 0x02};
  for (int mode = 5; mode <= 10; mode++) {
    reset_mock();
    widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
    output_caps[2] = (63U << 8) | 28U | (2U << 16);
    gain_readback_mode = mode;
    EFI_STATUS st = HdaProgramStoredHardwareVolume(&ctx, "selected");
    int should_reject = mode == 5 || mode == 6 || mode == 9 || mode == 10;
    if (st != (should_reject ? EFI_COMPROMISED_DATA : EFI_SUCCESS)) return 10 + mode;
    if (gain_writes != 2) return 30 + mode;

    reset_mock();
    widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
    output_caps[2] = (63U << 8) | 28U | (2U << 16);
    gain_readback_mode = mode;
    st = HdaLimitFanoutHardwareVolume(&io, 0, 1, 2, path, 3, "fanout");
    if (st != (should_reject ? EFI_COMPROMISED_DATA : EFI_SUCCESS)) return 50 + mode;
    if ((int)mHdaVolumeReadbackMismatch != should_reject) return 70 + mode;
  }
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
             "HdaVerifyAmpGain", "HdaProgramStoredHardwareVolume",
             "HdaTryHardwareVolumeOnRouteNode", "HdaLimitFanoutHardwareVolume", prelude=context)

    def test_warning_from_amp_get_is_unverified_and_requires_retry(self):
        # UEFI warnings are not EFI_ERROR, but they are NOT valid gain reads.
        # Both selected and fanout retry the SET once and log unverified state.
        prelude = r"""
typedef struct {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  UINT8 Afg;
  BOOLEAN HardwareVolumeApplied;
  BOOLEAN HardwareVolumeInputAmp;
  UINT8 HardwareVolumeNid;
  UINT8 HardwareVolumeInputIndex;
} HDAU_CONTEXT;
"""
        self.compile_run(r"""
int main(void) {
  HDAU_CONTEXT ctx = {0};
  ctx.PciIo = &io;
  ctx.Afg = 1;
  ctx.HardwareVolumeApplied = TRUE;
  ctx.HardwareVolumeNid = 2;
  reset_mock();
  widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
  output_caps[2] = (63U << 8) | 28U | (2U << 16);
  gain_readback_mode = 11;
  if (HdaProgramStoredHardwareVolume(&ctx, "selected") != EFI_SUCCESS || gain_writes != 2)
    return 10;
  reset_mock();
  widget_caps[2] = HDA_AWCAP_OUT_AMP_PRESENT;
  output_caps[2] = (63U << 8) | 28U | (2U << 16);
  gain_readback_mode = 11;
  if (HdaTryHardwareVolumeOnRouteNode(&io, 0, 1, 2, FALSE, 0, "fanout") != EFI_SUCCESS || gain_writes != 2)
    return 11;
  return 0;
}
""", "HdaAmpUnityGainFromCaps", "HdaAmpVolumeGainFromCaps",
             "HdaVerifyAmpGain", "HdaProgramStoredHardwareVolume",
             "HdaTryHardwareVolumeOnRouteNode", prelude=prelude)

    def test_stereo_mono_pcm_remain_bounded_to_30_percent(self):
        copy = definition(self.source, "HdaCopyBootChime")
        self.assertIn("(VOID)GainQ8", copy)
        self.assertIn("HDAU_BOOT_CHIME_VOLUME_PERCENT) / 100", copy)
        self.assertIn("INT32 Mono", copy)
        self.assertIn("INT32 OutLeft", copy)
        self.assertIn("INT32 OutRight", copy)


if __name__ == "__main__":
    unittest.main()
