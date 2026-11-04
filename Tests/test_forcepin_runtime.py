#!/usr/bin/env python3
"""Compile and execute the actual firmware route-selection functions with mock HDA I/O.

These narrow host tests exercise force-pin invariants without pretending that a
host compiler can compile or boot the full UEFI/EDK2 firmware driver.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.c"


def definition(source, name):
    pattern = r"(?m)^STATIC\s+[^;\n]*?\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{"
    match = re.search(pattern, source, re.S)
    if not match:
        raise AssertionError("missing definition " + name)
    opening = source.index("{", match.start(), match.end())
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        if source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():pos + 1]
    raise AssertionError("unclosed definition " + name)


PREFIX = r"""
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define CONST const
#define VOID void
#define UINT8 uint8_t
#define UINT16 uint16_t
#define UINT32 uint32_t
#define UINTN size_t
#define INTN ptrdiff_t
#define BOOLEAN int
#define TRUE 1
#define FALSE 0
#define HDAU_FORCE_NID_NONE 0x100U
#define HDAU_MAX_WIDGETS 128
#define HDAU_MAX_PATH_DEPTH 16
#define HDAU_ALCID_PROFILE_PAIR_COUNT 12
#define HDA_PARAM_PIN_CAP 1
#define HDA_PARAM_AUDIO_WIDGET_CAP 2
#define HDA_VERB_GET_CONFIG_DEFAULT 3
#define HDA_VERB12(a,b) 0
#define HDA_WIDGET_TYPE_PIN_COMPLEX 4
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ZeroMem(dst,n) memset(dst,0,n)
#define CopyMem(dst,src,n) memcpy(dst,src,n)
#define EFI_ERROR(status) ((status)!=0)
#define HdaLog(...) ((void)0)
typedef struct { int x; } EFI_PCI_IO_PROTOCOL;
typedef struct { UINT8 Pin,Dac,Afg; UINTN PathLen; UINT8 Path[HDAU_MAX_PATH_DEPTH]; INTN Score; BOOLEAN AppleAlcExact,Digital,Hdmi,DisplayPort; } HDAU_ROUTE_CANDIDATE;
typedef struct { EFI_PCI_IO_PROTOCOL *PciIo; UINT8 Codec,Afg,Pin,Dac; UINTN PathLen,RouteCandidateCount,RouteCandidateIndex; UINT8 Path[HDAU_MAX_PATH_DEPTH]; HDAU_ROUTE_CANDIDATE RouteCandidates[8]; } HDAU_CONTEXT;
typedef struct { UINT8 Pins[12],Dacs[12]; UINT32 LayoutId; const char *Name; } HDAU_ALCID_PROFILE;
static struct { UINTN ForcePin; BOOLEAN RouteFallback, Debug, AllowDigitalPins; } mHdaCfg;
static const HDAU_ALCID_PROFILE *mHdaActiveAlcIdProfile;
static const char *mHdaBootArgsAlcIdSource="test";
static EFI_PCI_IO_PROTOCOL pci;
static BOOLEAN HdaTryFullAlcIdRoute(HDAU_CONTEXT *c, UINT32 id, UINT8 afg, const HDAU_ALCID_PROFILE *p) { (void)c;(void)id;(void)afg;(void)p;return FALSE; }
static UINTN HdaCollectAudioOutputDacs(EFI_PCI_IO_PROTOCOL *io,UINT8 codec,UINT8 afg,UINT8 *dacs,UINTN capacity) { (void)io;(void)codec;(void)afg;if(!capacity)return 0;dacs[0]=2;return 1; }
static UINT32 HdaGetParam(EFI_PCI_IO_PROTOCOL *io, UINT8 codec, UINT8 nid, UINT32 param) { (void)io;(void)codec;(void)nid;(void)param;return HDA_WIDGET_TYPE_PIN_COMPLEX; }
static BOOLEAN HdaIsOutputPin(EFI_PCI_IO_PROTOCOL *io,UINT8 c,UINT8 nid){(void)io;(void)c;return nid>=0x10 && nid<=0x12;}
static int HdaImmediateCommand(EFI_PCI_IO_PROTOCOL *io,UINT8 c,UINT8 nid, UINT32 payload,UINT32 *result){(void)io;(void)c;(void)nid;(void)payload;*result=0;return 0;}
static BOOLEAN HdaPinDefaultLooksConnected(UINT32 config){(void)config;return TRUE;}
static BOOLEAN HdaPinDefaultIsDigital(UINT32 config){(void)config;return FALSE;}
static UINTN HdaBuildAlcIdDacOrder(const HDAU_ALCID_PROFILE *p,UINTN idx,const UINT8 *ds,UINTN nd,UINT8 *out,UINTN cap){(void)p;(void)idx;if(!nd||!cap)return 0;out[0]=ds[0];return 1;}
static BOOLEAN HdaIsAudioOutputDac(EFI_PCI_IO_PROTOCOL *io,UINT8 c,UINT8 afg,UINT8 dac){(void)io;(void)c;(void)afg;return dac==2;}
static BOOLEAN HdaPathSearch(EFI_PCI_IO_PROTOCOL *io,UINT8 c,UINT8 pin,UINT8 dac,UINT8 *path,UINTN *n){(void)io;(void)c;path[0]=pin;path[1]=dac;*n=2;return TRUE;}
static BOOLEAN HdaPathInputAmpsProgrammable(EFI_PCI_IO_PROTOCOL *io,UINT8 c,UINT8 afg,const UINT8 *path,UINTN n){(void)io;(void)c;(void)afg;(void)path;(void)n;return TRUE;}
static BOOLEAN HdaPathContainsPinIntermediate(EFI_PCI_IO_PROTOCOL *io,UINT8 c,const UINT8 *path,UINTN n){(void)io;(void)c;(void)path;(void)n;return FALSE;}
static BOOLEAN HdaClassifySelectedRoute(HDAU_CONTEXT *c){(void)c;return TRUE;}
static BOOLEAN HdaGetSubnodes(EFI_PCI_IO_PROTOCOL *io,UINT8 codec,UINT8 afg,UINT8 *start,UINT8 *count){(void)io;(void)codec;(void)afg;*start=0x10;*count=3;return TRUE;}
static UINT8 HdaWidgetType(UINT32 caps){return (UINT8)caps;}
static INTN HdaPinPriority(EFI_PCI_IO_PROTOCOL *io,UINT8 codec,UINT8 pin){(void)io;(void)codec;(void)pin;return 100;}
static void HdaAddRouteCandidate(HDAU_CONTEXT *c,UINT8 afg, UINT8 pin,UINT8 dac,const UINT8 *path,UINTN n,INTN score,BOOLEAN exact){(void)afg;(void)dac;(void)path;(void)n;(void)score;(void)exact;for(UINTN i=0;i<c->RouteCandidateCount;i++)if(c->RouteCandidates[i].Pin==pin)return;if(c->RouteCandidateCount<ARRAY_SIZE(c->RouteCandidates))c->RouteCandidates[c->RouteCandidateCount++].Pin=pin;}
static void HdaSortRouteCandidates(HDAU_CONTEXT *c){(void)c;}
"""


class ForcePinRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("cc") or shutil.which("clang")
        if cls.compiler is None:
            raise unittest.SkipTest("host C compiler unavailable: runtime checks not executed")
        cls.source = SOURCE.read_text(encoding="utf-8")

    def compile_run(self, function, body):
        with tempfile.TemporaryDirectory() as folder:
            file = Path(folder) / "route.c"
            executable = Path(folder) / "route"
            file.write_text(PREFIX + "\n" + definition(self.source, function) + "\n" + body, encoding="utf-8")
            build = subprocess.run([self.compiler, "-std=c11", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter", str(file), "-o", str(executable)], capture_output=True, text=True, timeout=30)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_applealc_runtime_path_does_not_override_forced_pin(self):
        self.compile_run("HdaTryAlcIdProfilePath", r"""
int main(void) {
    HDAU_CONTEXT c={0};
    HDAU_ALCID_PROFILE p={0};
    c.PciIo=&pci;
    p.Pins[0]=0x11;
    p.Pins[1]=0x10;
    p.Name="test";
    mHdaCfg.ForcePin=0x10;
    if (!HdaTryAlcIdProfilePath(&c,0x10ec0298,1,&p)) return 10;
    if (c.Pin!=0x10) return 11;
    c.Pin=0;
    mHdaCfg.ForcePin=HDAU_FORCE_NID_NONE;
    if (!HdaTryAlcIdProfilePath(&c,0x10ec0298,1,&p)) return 12;
    if (c.Pin!=0x11) return 13;
    return 0;
}
""")

    def test_route_fallback_does_not_override_forced_pin(self):
        self.compile_run("HdaCollectRouteCandidates", r"""
int main(void) {
    HDAU_CONTEXT c={0};
    c.PciIo=&pci;
    c.Afg=1;
    c.Pin=0x10;
    c.Dac=2;
    c.Path[0]=0x10;
    c.Path[1]=2;
    c.PathLen=2;
    mHdaCfg.RouteFallback=TRUE;
    mHdaCfg.ForcePin=0x10;
    HdaCollectRouteCandidates(&c);
    if (c.RouteCandidateCount!=1 || c.RouteCandidates[0].Pin!=0x10) return 20;
    mHdaCfg.ForcePin=HDAU_FORCE_NID_NONE;
    HdaCollectRouteCandidates(&c);
    if (c.RouteCandidateCount!=3) return 21;
    mHdaCfg.ForcePin=0x10;
    mHdaCfg.RouteFallback=FALSE;
    HdaCollectRouteCandidates(&c);
    if (c.RouteCandidateCount!=1) return 22;
    return 0;
}
""")

class FanoutSnapshotInvariantTests(unittest.TestCase):
    def test_legacy_fanout_captures_all_touched_routes_before_programming(self):
        body = definition(SOURCE.read_text(encoding="utf-8"), "HdaEnableLegacyRealtekFanout")
        snapshot = body.find("HdaSnapshotPathNodes(Context, Path, PathLen, Pin, Dac)")
        programming = body.find("HdaProgramAnalogOutputPath(PciIo, Codec, Context->Afg, Pin, Dac, Path, PathLen, FanoutReason)")
        self.assertGreaterEqual(snapshot, 0, "legacy fanout omitted transactional snapshots of extra routes")
        self.assertGreater(programming, snapshot)
        window = body[max(0, snapshot - 65):programming]
        self.assertIn("mHdaCfg.TransactionalRestore", window)
        self.assertIn("continue;", window)



class AppendLogOwnershipRound42Tests(unittest.TestCase):
    """Run the original C append-log ownership logic against failing file handles."""

    def test_failed_root_close_does_not_orphan_a_new_debug_log(self):
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        function = definition(SOURCE.read_text(encoding="utf-8"), "HdaEnsureAppendLog")
        prefix = r"""
#include <stddef.h>
#include <stdint.h>
#define STATIC static
#define VOID void
#define EFI_SUCCESS 0
#define EFI_DEVICE_ERROR 7
#define EFI_NOT_FOUND 14
#define EFI_INVALID_PARAMETER 2
#define TRUE 1
#define FALSE 0
#define BOOLEAN int
#define EFI_STATUS int
#define EFI_FILE_HANDLE void *
static struct { BOOLEAN FileLog; } mHdaCfg;
static EFI_FILE_HANDLE mHdaLogFile;
static int close_count;
static int root_close_status;
static EFI_STATUS HdaOpenLoadedImageRoot(EFI_FILE_HANDLE *root) {
    *root = (void *)(uintptr_t)1;
    return EFI_SUCCESS;
}
static EFI_STATUS HdaOpenDebugLog(EFI_FILE_HANDLE root, BOOLEAN append) {
    (void)root; (void)append;
    mHdaLogFile = (void *)(uintptr_t)2;
    return EFI_SUCCESS;
}
static EFI_STATUS HdaCloseFileHandleTerminal(EFI_FILE_HANDLE *root) {
    *root = NULL;
    return root_close_status;
}
static EFI_STATUS HdaCloseTrackedLogFile(BOOLEAN flush) {
    (void)flush;
    close_count++;
    mHdaLogFile = NULL;
    return EFI_SUCCESS;
}
"""
        main = r"""
int main(void) {
    BOOLEAN opened = TRUE, old_file_log = TRUE;
    root_close_status = EFI_DEVICE_ERROR;
    mHdaCfg.FileLog = FALSE;
    EFI_STATUS status = HdaEnsureAppendLog(&opened, &old_file_log);
    if (status != EFI_DEVICE_ERROR) return 10;
    if (opened != FALSE || old_file_log != FALSE) return 11;
    if (mHdaLogFile != NULL || close_count != 1) return 12;
    root_close_status = EFI_SUCCESS;
    status = HdaEnsureAppendLog(&opened, &old_file_log);
    if (status != EFI_SUCCESS) return 13;
    if (opened != TRUE || mHdaCfg.FileLog != TRUE) return 14;
    if (mHdaLogFile == NULL || close_count != 1) return 15;
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / "append-log.c"
            binary = Path(directory) / "append-log"
            file.write_text(prefix + "\n" + function + "\n" + main, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Wno-unused-function", "-Werror", str(file), "-o", str(binary)],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, "C append-log ownership regression: " + result.stderr + " exit=" + str(result.returncode))



class ConnectionDumpRound43Tests(unittest.TestCase):
    """Exercise the real C diagnostic printer, including the 512-byte HdaLog sink."""

    def test_all_connection_nodes_are_logged_without_truncation(self):
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        function = definition(SOURCE.read_text(encoding="utf-8"), "HdaDumpConnectionList")
        prefix = r"""
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define STATIC static
#define VOID void
#define UINT8 uint8_t
#define CHAR8 char
#define UINT32 uint32_t
#define UINTN size_t
#define HDAU_MAX_CONNECTIONS 128
#define EFI_FILE_HANDLE void *
typedef struct { int unused; } EFI_PCI_IO_PROTOCOL;
static size_t wanted_count;
static char captured[8192];
static size_t captured_len;
static size_t emitted_lines;
static UINTN HdaGetConnections(EFI_PCI_IO_PROTOCOL *io, UINT8 codec,
                               UINT8 nid, UINT8 *list, UINTN capacity) {
    (void)io; (void)codec; (void)nid;
    if (wanted_count > capacity) return 0;
    for (size_t index = 0; index < wanted_count; ++index) list[index] = (UINT8)(index + 1);
    return wanted_count;
}
static UINTN AsciiSPrint(char *dst, UINTN capacity, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    int wrote = vsnprintf(dst, capacity, fmt, va);
    va_end(va);
    return (wrote > 0) ? (UINTN)wrote : 0;
}
static VOID HdaDumpWrite(EFI_FILE_HANDLE file, const char *fmt, ...) {
    (void)file; (void)fmt;
    va_list va;
    va_start(va, fmt);
    const char *body = va_arg(va, const char *);
    va_end(va);
    /* Reproduce the 512-byte HdaLog formatting limit, including its prefix. */
    char logged[512];
    int length = snprintf(logged, sizeof(logged), "[HDAUDUMP] %s", body);
    if (length < 0) return;
    size_t bounded_length = strlen(logged);
    if (captured_len + bounded_length + 2 > sizeof(captured)) return;
    memcpy(captured + captured_len, logged, bounded_length);
    captured_len += bounded_length;
    captured[captured_len++] = '\n';
    captured[captured_len] = 0;
    emitted_lines++;
}
"""
        main = r"""
int main(void) {
    EFI_PCI_IO_PROTOCOL io = {0};
    const size_t cases[] = {1, 50, 100, 127};
    for (size_t case_id = 0; case_id < sizeof(cases) / sizeof(cases[0]); ++case_id) {
        wanted_count = cases[case_id];
        captured_len = 0;
        emitted_lines = 0;
        captured[0] = 0;
        HdaDumpConnectionList(NULL, &io, 0, 0);
        if (emitted_lines < 1) { fprintf(stderr, "missing output for count=%zu\n", wanted_count); return 20; }
        if (wanted_count <= 50 && emitted_lines != 1) return 21;
        for (size_t index = 1; index <= wanted_count; ++index) {
            char token[12];
            snprintf(token, sizeof(token), " 0x%02x", (unsigned)index);
            if (strstr(captured, token) == NULL) {
                fprintf(stderr, "missing connection %zu of %zu (lines=%zu)\n", index, wanted_count, emitted_lines);
                return 22;
            }
        }
        if (wanted_count == 127 && emitted_lines < 2) return 23;
    }
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "dump-connections.c"
            binary = Path(folder) / "dump-connections"
            source.write_text(prefix + "\n" + function + "\n" + main, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0,
                             "C HDA connection dump incomplete: " + result.stderr + " exit=" + str(result.returncode))

if __name__ == "__main__":
    unittest.main()
