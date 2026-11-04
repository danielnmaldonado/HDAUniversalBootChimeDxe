#!/usr/bin/env python3
"""Regression tests for ownership and AppleALC profile validation."""

import sys

# Keep the release tree deterministic even when this runner imports local tools.
sys.dont_write_bytecode = True

import importlib.util
import os
import plistlib
import re
import subprocess
import tempfile
import unittest
import warnings
import zipfile
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATOR_PATH = ROOT / "Tools" / "generate_applealc_profiles.py"
SOURCE_PATH = ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.c"
GENERATED_HEADER_PATH = ROOT / "HDAUniversalBootChimeDxe" / "HdaAppleAlcLayouts.h"


def load_generator():
    spec = importlib.util.spec_from_file_location("hdau_applealc_generator", GENERATOR_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load AppleALC generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GENERATOR = load_generator()


def plist_bytes(value):
    return plistlib.dumps(value, fmt=plistlib.FMT_XML, sort_keys=True)


def make_archive(path, config_defaults, layout_path="layout1.xml", platform_path="platform1.xml"):
    root = "AppleALC-test/Resources/"
    codec_folder = root + "TestCodec/"
    info = {
        "CodecName": "TestCodec",
        "Files": {
            "Layouts": [{"Id": 1, "Path": layout_path}],
            "Platforms": [{"Id": 1, "Path": platform_path}],
        },
    }
    layout = {"PathMapRef": [{"PathMapID": 1, "CodecID": [0x10EC0298]}]}
    platform = {
        "PathMaps": [
            {
                "PathMapID": 1,
                "PathMap": [[{"NodeID": 0x14}, {"NodeID": 0x0C}, {"NodeID": 0x02}]],
            }
        ]
    }
    pinconfigs = {
        "IOKitPersonalities": {
            "as.vit9696.AppleALC": {"HDAConfigDefault": config_defaults}
        }
    }
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(codec_folder + "Info.plist", plist_bytes(info))
        if layout_path == "layout1.xml":
            archive.writestr(codec_folder + layout_path, plist_bytes(layout))
        if platform_path == "platform1.xml":
            archive.writestr(codec_folder + platform_path, plist_bytes(platform))
        archive.writestr(
            root + "PinConfigs.kext/Contents/Info.plist", plist_bytes(pinconfigs)
        )


def exact_entry(verb, revision_id, revision_mask, subsystem_id=None, subsystem_mask=None):
    entry = {
        "CodecID": 0x10EC0298,
        "LayoutID": 1,
        "ConfigData": verb.to_bytes(4, "big"),
        "RevisionID": revision_id,
        "RevisionMask": revision_mask,
    }
    if subsystem_id is not None:
        entry["SubsystemID"] = subsystem_id
    if subsystem_mask is not None:
        entry["SubsystemMask"] = subsystem_mask
    return entry


def extract_c_function(text, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", text, re.S)
    if match is None:
        raise AssertionError("function not found: " + name)
    start = match.start()
    brace = text.find("{", match.start(), match.end())
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError("unterminated function: " + name)


def extract_c_function_definition(text, name):
    matches = list(re.finditer(r"(?:^|\n)STATIC\s+[^;\n]*?\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", text, re.S))
    if not matches:
        raise AssertionError("function definition not found: " + name)
    match = matches[-1]
    start = match.start() + (1 if text[match.start():match.start()+1] == "\n" else 0)
    brace = text.find("{", match.start(), match.end())
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError("unterminated function definition: " + name)


class AppleAlcGeneratorTests(unittest.TestCase):
    def test_valid_generic_profile_is_generated(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "valid.zip"
            make_archive(
                archive,
                [
                    {
                        "CodecID": 0x10EC0298,
                        "LayoutID": 1,
                        "ConfigData": bytes.fromhex("01471C10"),
                    }
                ],
            )
            text, stats = GENERATOR.generate(archive)
            self.assertEqual(stats["profiles"], 1)
            self.assertEqual(stats["full_routes"], 1)
            self.assertEqual(stats["config_profiles"], 1)
            self.assertIn("0x10EC0298", text)
            self.assertIn("0x01471C10", text)

    def test_equivalent_mask_domains_with_conflicting_verbs_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "equivalent-conflict.zip"
            make_archive(
                archive,
                [
                    exact_entry(0x01471C10, 0x00000100, 0x000000FF),
                    exact_entry(0x01471C20, 0x00000200, 0x000000FF),
                ],
            )
            with self.assertRaisesRegex(RuntimeError, "ambiguous overlapping exact"):
                GENERATOR.generate(archive)

    def test_strict_subset_exact_profile_remains_valid(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "strict-subset.zip"
            make_archive(
                archive,
                [
                    exact_entry(0x01471C10, 0x00000020, 0x000000F0),
                    exact_entry(0x01471C20, 0x00000021, 0x000000FF),
                ],
            )
            _text, stats = GENERATOR.generate(archive)
            self.assertEqual(stats["exact_profiles"], 2)

    def test_unsafe_resource_path_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "unsafe-path.zip"
            make_archive(archive, [], layout_path="../layout1.xml")
            with self.assertRaisesRegex(RuntimeError, "unsafe AppleALC resource path"):
                GENERATOR.generate(archive)

    def test_noncanonical_applealc_resource_paths_are_rejected(self):
        # PurePosixPath silently collapses these lexical spellings.  The plist
        # must refer to one unambiguous ZIP member rather than an alias.
        cases = (
            ("./{name}", "{name}"),
            ("{name}/", "{name}"),
            ("sub//{name}", "sub/{name}"),
            ("sub/./{name}", "sub/{name}"),
        )
        with tempfile.TemporaryDirectory() as temporary:
            for kind in ("layout", "platform"):
                for index, (untrusted, canonical) in enumerate(cases):
                    filename = "layout1.xml" if kind == "layout" else "platform1.xml"
                    path = Path(temporary) / f"{kind}-{index}.zip"
                    alias = untrusted.format(name=filename)
                    make_archive(
                        path, [],
                        layout_path=alias if kind == "layout" else "layout1.xml",
                        platform_path=alias if kind == "platform" else "platform1.xml",
                    )
                    contents = (
                        {"PathMapRef": [{"PathMapID": 1, "CodecID": [0x10EC0298]}]}
                        if kind == "layout" else
                        {"PathMaps": [{"PathMapID": 1, "PathMap": [
                            [{"NodeID": 0x14}, {"NodeID": 0x0C}, {"NodeID": 0x02}]
                        ]}]}
                    )
                    with zipfile.ZipFile(path, "a") as archive:
                        archive.writestr(
                            "AppleALC-test/Resources/TestCodec/" + canonical.format(name=filename),
                            plist_bytes(contents),
                        )
                    with self.subTest(kind=kind, path=alias):
                        with self.assertRaisesRegex(RuntimeError, "unsafe AppleALC resource path"):
                            GENERATOR.generate(path)

    def test_exact_overlap_checks_skip_unrelated_layout_domains(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive_path = Path(temporary) / "many-layouts.zip"
            root_name = "AppleALC-test/Resources/"
            codec_folder = root_name + "TestCodec/"
            layout_count = 64
            info = {
                "CodecName": "TestCodec",
                "Files": {
                    "Layouts": [
                        {"Id": layout_id, "Path": f"layout{layout_id}.xml"}
                        for layout_id in range(1, layout_count + 1)
                    ],
                    "Platforms": [
                        {"Id": layout_id, "Path": "platform1.xml"}
                        for layout_id in range(1, layout_count + 1)
                    ],
                },
            }
            platform = {
                "PathMaps": [{
                    "PathMapID": 1,
                    "PathMap": [[{"NodeID": 0x14}, {"NodeID": 0x0C}, {"NodeID": 0x02}]],
                }]
            }
            configs = []
            with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as output:
                output.writestr(codec_folder + "Info.plist", plist_bytes(info))
                output.writestr(codec_folder + "platform1.xml", plist_bytes(platform))
                for layout_id in range(1, layout_count + 1):
                    layout = {"PathMapRef": [{"PathMapID": 1, "CodecID": [0x10EC0298]}]}
                    output.writestr(codec_folder + f"layout{layout_id}.xml", plist_bytes(layout))
                    configs.append({
                        "CodecID": 0x10EC0298,
                        "LayoutID": layout_id,
                        "ConfigData": bytes.fromhex("01471C10"),
                        "RevisionID": layout_id,
                        "RevisionMask": 0xFFFFFFFF,
                    })
                output.writestr(
                    root_name + "PinConfigs.kext/Contents/Info.plist",
                    plist_bytes({"IOKitPersonalities": {
                        "as.vit9696.AppleALC": {"HDAConfigDefault": configs}
                    }}),
                )

            original_overlap = GENERATOR.masked_constraints_overlap
            calls = 0

            def counted_overlap(*args):
                nonlocal calls
                calls += 1
                return original_overlap(*args)

            GENERATOR.masked_constraints_overlap = counted_overlap
            try:
                _text, stats = GENERATOR.generate(archive_path)
            finally:
                GENERATOR.masked_constraints_overlap = original_overlap
            self.assertEqual(stats["exact_profiles"], layout_count)
            self.assertEqual(calls, 0)

    def test_oversized_configdata_is_rejected_before_verb_materialization(self):
        old = GENERATOR.MAX_CONFIG_VERBS_PER_PROFILE
        try:
            GENERATOR.MAX_CONFIG_VERBS_PER_PROFILE = 2
            with tempfile.TemporaryDirectory() as temporary:
                archive = Path(temporary) / "oversized-configdata.zip"
                make_archive(
                    archive,
                    [{
                        "CodecID": 0x10EC0298,
                        "LayoutID": 1,
                        "ConfigData": bytes.fromhex("01471C1001471C1101471C12"),
                    }],
                )
                _text, stats = GENERATOR.generate(archive)
                self.assertEqual(stats["config_invalid"], 1)
                self.assertEqual(stats["config_profiles"], 0)
        finally:
            GENERATOR.MAX_CONFIG_VERBS_PER_PROFILE = old

    def test_exact_profile_group_has_quadratic_work_safety_cap(self):
        old = GENERATOR.MAX_EXACT_PROFILES_PER_CODEC_LAYOUT
        try:
            GENERATOR.MAX_EXACT_PROFILES_PER_CODEC_LAYOUT = 2
            with tempfile.TemporaryDirectory() as temporary:
                archive = Path(temporary) / "too-many-exact.zip"
                make_archive(
                    archive,
                    [
                        exact_entry(0x01471C10, 1, 0xFFFFFFFF),
                        exact_entry(0x01471C10, 2, 0xFFFFFFFF),
                        exact_entry(0x01471C10, 3, 0xFFFFFFFF),
                    ],
                )
                with self.assertRaisesRegex(RuntimeError, "profile count exceeds safety limit"):
                    GENERATOR.generate(archive)
        finally:
            GENERATOR.MAX_EXACT_PROFILES_PER_CODEC_LAYOUT = old

    def test_duplicate_archive_members_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "duplicates.zip"
            root = "AppleALC-test/Resources/TestCodec/Info.plist"
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", UserWarning)
                with zipfile.ZipFile(archive, "w") as output:
                    output.writestr(root, plist_bytes({}))
                    output.writestr(root, plist_bytes({}))
            with self.assertRaisesRegex(RuntimeError, "duplicate archive members"):
                GENERATOR.generate(archive)

    def test_malformed_plist_parser_errors_are_normalized(self):
        malformed = (
            b'<?xml version="1.0"?><plist version="1.0"><integer>abc</integer></plist>',
            b'<?xml version="1.0"?><plist version="1.0"><real>x</real></plist>',
            b'<?xml version="1.0"?><plist version="1.0"><date>999999-99-99T99:99:99Z</date></plist>',
            b'<?xml version="1.0"?><plist version="1.0"><dict><key>x</key></plist>',
        )
        for payload in malformed:
            with self.subTest(payload=payload[:48]):
                with self.assertRaisesRegex(RuntimeError, "invalid AppleALC plist"):
                    GENERATOR.load_plist(payload)

    def test_malformed_plist_fragment_parser_errors_are_normalized(self):
        payload = b'<dict><key>x</key><integer>not-an-integer</integer></dict>'
        with self.assertRaisesRegex(RuntimeError, "invalid AppleALC plist"):
            GENERATOR.load_plist_fragment(payload)

    def test_malformed_optional_layout_keeps_existing_skip_semantics(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "malformed-layout.zip"
            make_archive(archive, [exact_entry(0x01471C10, 1, 0xFFFFFFFF)])
            entries = {}
            with zipfile.ZipFile(archive, "r") as source:
                for info in source.infolist():
                    entries[info.filename] = source.read(info)
            layout_name = "AppleALC-test/Resources/TestCodec/layout1.xml"
            entries[layout_name] = b'<dict><key>x</key><integer>bad</integer></dict>'
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
                for name, payload in entries.items():
                    output.writestr(name, payload)
            _text, stats = GENERATOR.generate(archive)
            self.assertEqual(stats["skipped_missing_platform"], 1)
            self.assertEqual(stats["profiles"], 0)


class GeneratedHeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = GENERATED_HEADER_PATH.read_text(encoding="utf-8")

    def array_body(self, name):
        match = re.search(
            re.escape(name) + r"\[\]\s*=\s*\{(.*?)\n\};", self.text, re.S
        )
        self.assertIsNotNone(match, "missing generated array " + name)
        return match.group(1)

    def test_profile_pair_arrays_match_c_capacity(self):
        body = self.array_body("mHdaAlcIdProfiles")
        rows = [line for line in body.splitlines() if re.match(r"\s*\{\s*0x", line)]
        self.assertGreater(len(rows), 0)
        for line in rows:
            arrays = re.findall(
                r"\{\s*((?:0x[0-9A-Fa-f]{2}(?:,\s*)?)+)\s*\}", line
            )
            self.assertEqual(len(arrays), 2)
            for values in arrays:
                self.assertEqual(len(re.findall(r"0x[0-9A-Fa-f]{2}", values)), 12)

    def test_generated_offsets_stay_inside_backing_arrays(self):
        verbs = re.findall(r"0x[0-9A-Fa-f]{8}", self.array_body("mHdaAlcConfigVerbs"))
        nodes = re.findall(r"0x[0-9A-Fa-f]{1,2}", self.array_body("mHdaAlcRouteNodes"))

        config_rows = re.findall(
            r'\{\s*0x[0-9A-Fa-f]+,\s*\d+,\s*"(?:[^"\\]|\\.)*",\s*(\d+)U?,\s*(\d+)U?\s*\}',
            self.array_body("mHdaAlcConfigProfiles"),
        )
        exact_rows = []
        for line in self.array_body("mHdaAlcExactProfiles").splitlines():
            values = re.findall(r"(?:0x[0-9A-Fa-f]+|\d+)U?", line)
            if len(values) >= 8:
                parsed = [int(value.rstrip("Uu"), 0) for value in values[:8]]
                exact_rows.append((parsed[6], parsed[7]))
        route_rows = re.findall(
            r'\{\s*0x[0-9A-Fa-f]+,\s*\d+,\s*"(?:[^"\\]|\\.)*",\s*(\d+)U?,\s*(\d+)U?,',
            self.array_body("mHdaAlcRouteProfiles"),
        )

        for offset, count in [(int(a), int(b)) for a, b in config_rows] + exact_rows:
            if count:
                self.assertLess(offset, len(verbs))
                self.assertLessEqual(offset + count, len(verbs))
        for offset, count in [(int(a), int(b)) for a, b in route_rows]:
            if count:
                self.assertLess(offset, len(nodes))
                self.assertLessEqual(offset + count, len(nodes))


class AsyncOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_timer_arm_failure_uses_central_cleanup_and_aborts_reuse(self):
        function = extract_c_function(self.source, "HdaStartFastBootPlayback")
        self.assertIn("CleanupStatus = HdaCleanupAsyncPlayback(FALSE);", function)
        self.assertIn("return EFI_ABORTED;", function)
        self.assertNotIn("(VOID)gBS->CloseEvent(mHdaAsyncEvent)", function)

    def test_blocking_fallback_is_suppressed_after_ownership_transfer(self):
        function = extract_c_function(self.source, "HdaPlayChime")
        guard = function.index("Status == EFI_ABORTED")
        fallback = function.index("falling back to blocking playback")
        self.assertLess(guard, fallback)
        self.assertIn("mHdaAsyncContext->PciIo == Context->PciIo", function)

    def test_central_cleanup_does_not_free_event_owned_buffers(self):
        function = extract_c_function(self.source, "HdaCleanupAsyncPlayback")
        close_event = function.index("EventStatus = HdaCloseEventTerminal(&mHdaAsyncEvent);")
        free_buffers = function.index("BuffersReleased = HdaFreePlaybackBuffers(Context)")
        self.assertLess(close_event, free_buffers)
        # CloseEvent is terminal by UEFI contract; an undocumented status must
        # not keep the event/context in a retry loop or trigger a second close.
        region = function[close_event:free_buffers]
        self.assertNotIn("if (EFI_ERROR(EventStatus))", region)
        self.assertNotIn("return EventStatus;", region)

    def test_repeated_callback_after_failed_close_avoids_stream_registers(self):
        function = extract_c_function(self.source, "HdaAsyncPlaybackTick")
        stopped_guard = function.index("if (Context->StreamOffset == 0)")
        first_stream_read = function.index("HdaMemRead32")
        self.assertLess(stopped_guard, first_stream_read)
        self.assertIn("HdaCleanupAsyncPlayback(FALSE)", function[stopped_guard:first_stream_read])

    def test_callback_releases_buffers_only_after_event_close_success(self):
        function = extract_c_function(self.source, "HdaAsyncPlaybackTick")
        close_call = function.index("EventCloseStatus = HdaCloseEventTerminal(&EventToClose);")
        clear_event = function.index("mHdaAsyncEvent = NULL;", close_call)
        free_buffers = function.index("BuffersReleased = HdaFreePlaybackBuffers(Context)")
        self.assertLess(close_call, clear_event)
        self.assertLess(clear_event, free_buffers)
        # File/event close is terminal; no retry gate may precede buffer release.
        self.assertNotIn("return;", function[close_call:free_buffers])


class CorbRirbHardeningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (SOURCE_PATH.parent / "HdaUefi.h").read_text(encoding="utf-8")
        cls.inf = (SOURCE_PATH.parent / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")

    def test_retained_ownership_has_exit_boot_services_retry(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        cleanup = extract_c_function(self.source, "HdaCleanupCorbRirbState")
        controller = extract_c_function(self.source, "HdaTryController")
        exit_cleanup = extract_c_function(self.source, "HdaExitBootServicesCleanup")
        self.assertIn("mHdaRetainedCorbRirbValid = TRUE", command)
        self.assertIn("HdaCleanupRetainedCorbRirb(TRUE)", exit_cleanup)
        self.assertIn("EfiPciIoAttributeOperationDisable", cleanup)
        self.assertIn("EFI_PCI_IO_ATTRIBUTE_BUS_MASTER", cleanup)
        self.assertIn("DmaStopUnconfirmed = TRUE", cleanup)
        self.assertIn("DmaStopUnconfirmed", controller)
        self.assertIn("PciDmaContainment = TRUE", controller)

    def test_corb_rirb_device_addresses_require_128_byte_alignment(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        self.assertIn("HDA_DMA_ADDRESS_ALIGNMENT", command)
        self.assertIn("CorbDevice", command)
        self.assertIn("RirbDevice", command)

    def test_corb_rirb_respect_gcap_64ok(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        self.assertIn("HDA_REG_GCAP", command)
        self.assertIn("HDA_GCAP_64OK", command)
        self.assertIn("0xFFFFFFFFULL", command)

    def test_unsolicited_rirb_responses_are_ignored(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        self.assertIn("HDA_RIRB_RESPONSE_UNSOLICITED", command)
        self.assertIn("continue;", command)
        self.assertIn("HDA_RIRB_RESPONSE_CODEC_MASK", command)
        self.assertIn("HDA_RIRB_RESPONSE_UNSOLICITED", self.header)

    def test_corb_read_pointer_reset_is_handshaked(self):
        reset = extract_c_function(self.source, "HdaResetCorbReadPointer")
        self.assertGreaterEqual(reset.count("HdaMemRead16"), 2)
        self.assertIn("HDA_CORBRP_RESET", reset)
        self.assertIn("EFI_TIMEOUT", reset)
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        self.assertIn("HdaResetCorbReadPointer(PciIo)", command)

    def test_dispatch_waits_for_pci_io_and_guard_is_after_handle_discovery(self):
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        self.assertIn("gEfiPciIoProtocolGuid", self.inf)
        self.assertNotIn("[Depex]\n  TRUE", self.inf)
        self.assertLess(entry.index("LocateHandleBuffer"), entry.index("HdaAcquireBootGuard"))

    def test_plist_member_read_errors_are_normalized(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive_path = Path(temporary) / "read-error.zip"
            member = "AppleALC-test/Resources/TestCodec/Info.plist"
            with zipfile.ZipFile(archive_path, "w") as output:
                output.writestr(member, plist_bytes({"CodecName": "x"}))
            with zipfile.ZipFile(archive_path) as archive:
                with mock.patch.object(archive, "read", side_effect=NotImplementedError("unsupported")):
                    with self.assertRaisesRegex(RuntimeError, "could not read AppleALC plist"):
                        GENERATOR.read_plist_member(archive, member)

    def test_atomic_output_failure_cleans_temporary_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "generated.h"
            with mock.patch.object(GENERATOR.os, "replace", side_effect=OSError("replace denied")):
                with self.assertRaisesRegex(RuntimeError, "could not write generated output atomically"):
                    GENERATOR.write_output_atomic(output, "test")
            self.assertFalse(output.exists())
            self.assertEqual(list(Path(temporary).glob(".generated.h.*.tmp")), [])


class FinalAuditRoundTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (SOURCE_PATH.parent / "HdaUefi.h").read_text(encoding="utf-8")
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        cls.setup = (ROOT / "Setup-EDK2.command").read_text(encoding="utf-8")

    def test_failed_corb_rirb_calls_never_publish_unowned_outputs(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        self.assertIn("&CorbHost", command)
        self.assertIn("&RirbHost", command)
        self.assertIn("&CorbMapping", command)
        self.assertIn("&RirbMapping", command)
        self.assertNotIn("&RingState.CorbHost", command)
        self.assertNotIn("&RingState.RirbHost", command)
        self.assertNotIn("&RingState.CorbMapping", command)
        self.assertNotIn("&RingState.RirbMapping", command)
        self.assertNotIn("CorbAllocated = (RingState.CorbHost != NULL)", command)
        self.assertNotIn("RirbAllocated = (RingState.RirbHost != NULL)", command)
        self.assertNotIn("CorbMapped = (RingState.CorbMapping != NULL)", command)
        self.assertNotIn("RirbMapped = (RingState.RirbMapping != NULL)", command)

        for local_name, publish in (
            ("&CorbHost", "RingState.CorbHost = CorbHost;"),
            ("&RirbHost", "RingState.RirbHost = RirbHost;"),
            ("&CorbMapping", "RingState.CorbMapping = CorbMapping;"),
            ("&RirbMapping", "RingState.RirbMapping = RirbMapping;"),
        ):
            call = command.index(local_name)
            publish_at = command.index(publish, call)
            self.assertIn("if (Status != EFI_SUCCESS)", command[call:publish_at])
            self.assertIn("HdaNormalizeExactSuccess(Status)", command[call:publish_at])

    def test_failed_playback_map_never_publishes_unowned_mapping(self):
        alloc = extract_c_function(self.source, "HdaAllocatePlaybackBuffers")
        self.assertIn("&AudioDeviceAddress", alloc)
        self.assertIn("&AudioMapping", alloc)
        self.assertIn("&BdlDeviceAddress", alloc)
        self.assertIn("&BdlMapping", alloc)
        self.assertNotIn("&Context->AudioMapping", alloc)
        self.assertNotIn("&Context->BdlMapping", alloc)

        audio_call = alloc.index("&AudioMapping")
        audio_publish = alloc.index("Context->AudioMapping = AudioMapping;", audio_call)
        self.assertIn("if (Status != EFI_SUCCESS)", alloc[audio_call:audio_publish])
        self.assertLess(audio_publish, alloc.index("if (MapAudioBytes < RequiredAudioBufferBytes)", audio_publish))

        bdl_call = alloc.index("&BdlMapping")
        bdl_publish = alloc.index("Context->BdlMapping = BdlMapping;", bdl_call)
        self.assertIn("if (Status != EFI_SUCCESS)", alloc[bdl_call:bdl_publish])
        self.assertLess(bdl_publish, alloc.index("if (MapBdlBytes <", bdl_publish))

    def test_failed_console_protocol_lookup_cannot_publish_invalid_protocol(self):
        poll = extract_c_function(self.source, "HdaPollForF6")
        self.assertIn("EFI_STATUS Status;", poll)
        self.assertGreaterEqual(poll.count("if (Status != EFI_SUCCESS) TextInEx = NULL;"), 2)
        self.assertIn("TextInEx != NULL && TextInEx->ReadKeyStrokeEx != NULL", poll)

    def test_failed_locate_handle_buffer_is_never_freed(self):
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        locate = entry.index("LocateHandleBuffer")
        no_handles = entry.index("if (HandleCount == 0 || Handles == NULL)", locate)
        error_branch = entry[locate:no_handles]
        self.assertIn("if (Status != EFI_SUCCESS)", error_branch)
        self.assertIn("Handles = NULL;", error_branch)
        self.assertIn("HandleCount = 0;", error_branch)
        self.assertNotIn("FreePool(Handles)", error_branch)

    def test_failed_log_open_cannot_publish_invalid_global_handle(self):
        log_open = extract_c_function(self.source, "HdaOpenDebugLog")
        call = log_open.index("&mHdaLogFile")
        error = log_open.index("if (Status != EFI_SUCCESS)", call)
        publish_guard = log_open.index("mHdaLogFile = NULL;", error)
        self.assertLess(call, error)
        self.assertLess(error, publish_guard)

    def test_directory_retry_clears_failed_open_output(self):
        open_dir = extract_c_function(self.source, "HdaOpenOrCreateDir")
        first_open = open_dir.index("Parent->Open")
        second_open = open_dir.index("Parent->Open", first_open + 1)
        between = open_dir[first_open:second_open]
        self.assertGreaterEqual(between.count("*Dir = NULL;"), 1)
        self.assertIn("if (Status != EFI_NOT_FOUND)", between)
        # Failed Open() does not establish NewHandle ownership. Never dereference
        # or Close() the failed OUT value.
        self.assertNotIn("(*Dir)->Close", between)
        self.assertNotIn("HdaCloseFileHandleTerminal", between)

    def test_loaded_image_root_publishes_handle_only_after_success(self):
        open_root = extract_c_function(self.source, "HdaOpenLoadedImageRoot")
        call = open_root.index("Fs->OpenVolume")
        region = open_root[call:]
        self.assertIn("if (Status != EFI_SUCCESS)", region)
        self.assertIn("*Root = NULL;", region)
        self.assertIn("if (*Root == NULL) return EFI_DEVICE_ERROR;", region)
        self.assertIn("return EFI_SUCCESS;", region)
        failure = region[region.index("if (Status != EFI_SUCCESS)"):region.index("if (*Root == NULL)")]
        self.assertNotIn("Close(", failure)

    def test_failed_single_shot_protocol_install_clears_output_handle(self):
        guard = extract_c_function(self.source, "HdaAcquireBootGuard")
        call = guard.index("InstallProtocolInterface")
        error = guard.index("if (Status != EFI_SUCCESS)", call)
        publish = guard.index("mHdaLoadedProtocolHandle = ProtocolHandle;", error)
        failure_region = guard[error:publish]
        self.assertIn("ProtocolHandle = NULL;", failure_region)
        self.assertIn("mHdaLoadedProtocolHandle = NULL;", failure_region)
        # A failed IN OUT installation does not establish protocol ownership.
        self.assertNotIn("UninstallProtocolInterface", failure_region)

    def test_generator_groups_exact_overlap_checks_by_codec_layout(self):
        generator_source = GENERATOR_PATH.read_text(encoding="utf-8")
        start = generator_source.index("exact_entries = list(exact_config_profiles.values())")
        end = generator_source.index("exact_entries.sort(", start)
        overlap = generator_source[start:end]
        self.assertIn("exact_groups", overlap)
        self.assertIn('(entry["codec"], entry["layout"])', overlap)
        self.assertIn("for exact_group in exact_groups.values():", overlap)
        self.assertIn("for right in exact_group[left_index + 1:]", overlap)
        self.assertIn("MAX_EXACT_PROFILES_PER_CODEC_LAYOUT", overlap)
        self.assertNotIn('left["codec"] != right["codec"]', overlap)
        config_parse = generator_source[
            generator_source.index("for raw_entry in config_defaults"):
            generator_source.index("exact_entries = list(exact_config_profiles.values())")
        ]
        self.assertLess(
            config_parse.index("MAX_CONFIG_VERBS_PER_PROFILE * 4"),
            config_parse.index("verbs = tuple("),
        )

    def test_generator_rejects_oversized_archive_before_zip_parsing(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "oversized.zip"
            with archive.open("wb") as stream:
                stream.truncate(GENERATOR.MAX_ARCHIVE_FILE_BYTES + 1)
            with self.assertRaisesRegex(RuntimeError, "archive exceeds"):
                GENERATOR.generate(archive)

    def test_generator_rejects_total_uncompressed_archive_limit(self):
        old = GENERATOR.MAX_ARCHIVE_UNCOMPRESSED_BYTES
        try:
            GENERATOR.MAX_ARCHIVE_UNCOMPRESSED_BYTES = 32
            with tempfile.TemporaryDirectory() as temporary:
                archive = Path(temporary) / "oversized.zip"
                with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
                    output.writestr("AppleALC-test/Resources/TestCodec/Info.plist", b"x" * 64)
                with self.assertRaisesRegex(RuntimeError, "archive expands"):
                    GENERATOR.generate(archive)
        finally:
            GENERATOR.MAX_ARCHIVE_UNCOMPRESSED_BYTES = old

    def test_generator_plist_depth_and_object_limits_are_bounded_iteratively(self):
        old_depth = GENERATOR.MAX_PLIST_DEPTH
        old_objects = GENERATOR.MAX_PLIST_OBJECTS
        try:
            GENERATOR.MAX_PLIST_DEPTH = 3
            with self.assertRaisesRegex(RuntimeError, "nesting exceeds"):
                GENERATOR.validate_plist_tree({"a": {"b": {"c": {"d": 1}}}})
            GENERATOR.MAX_PLIST_DEPTH = old_depth
            GENERATOR.MAX_PLIST_OBJECTS = 3
            with self.assertRaisesRegex(RuntimeError, "object count exceeds"):
                GENERATOR.validate_plist_tree([1, 2, 3, 4])
        finally:
            GENERATOR.MAX_PLIST_DEPTH = old_depth
            GENERATOR.MAX_PLIST_OBJECTS = old_objects

    def test_fixed_volume_copy_has_no_dead_gain_assignment(self):
        function = extract_c_function(self.source, "HdaCopyBootChime")
        self.assertIn("(VOID)GainQ8;", function)
        self.assertNotIn("GainQ8 = HDAU_BOOT_CHIME_GAIN_Q8", function)

    def test_audio_dma_device_range_cannot_wrap_before_bdl_split(self):
        alloc = extract_c_function(self.source, "HdaAllocatePlaybackBuffers")
        guard = alloc.index("Context->AudioDeviceAddress > (MAX_UINT64 - (UINT64)(RequiredAudioBufferBytes - 1U))")
        split = alloc.index("FirstBdlBytes =", guard)
        second = alloc.index("Bdl[1].Address = Context->AudioDeviceAddress + FirstBdlBytes", split)
        self.assertLess(guard, split)
        self.assertLess(split, second)
        self.assertIn("HdaFreePlaybackBuffers(Context);", alloc[guard:split])
        self.assertIn("return EFI_DEVICE_ERROR;", alloc[guard:split])

    def test_bdl_has_at_least_two_entries_and_lvi_matches(self):
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        lvi = extract_c_function(self.source, "HdaWriteStreamLviPreserve")
        self.assertRegex(self.header, r"#define\s+HDAU_BDL_ENTRY_COUNT\s+2U")
        self.assertIn("HDAU_BDL_ENTRY_COUNT - 1U", prepare)
        self.assertIn("HDA_SD_LVI", lvi)
        alloc = extract_c_function(self.source, "HdaAllocatePlaybackBuffers")
        self.assertIn("HDAU_BDL_ENTRY_COUNT * sizeof(HDA_BDL_ENTRY)", alloc)

    def test_fastboot_requires_exit_boot_services_cleanup_before_transfer(self):
        function = extract_c_function(self.source, "HdaStartFastBootPlayback")
        install = function.index("HdaInstallExitBootServicesCleanup")
        allocation = function.index("AllocateCopyPool")
        self.assertLess(install, allocation)
        self.assertIn("if (EFI_ERROR(Status))", function[install:allocation])

    def test_terminal_cleanup_minimizes_external_work_at_exit_boot_services(self):
        cleanup = extract_c_function(self.source, "HdaCleanupAsyncPlayback")
        stopped = cleanup.index("if (!EFI_ERROR(StopStatus))")
        branch = cleanup.index("if (ExitBootServices) {", stopped)
        nonterminal = cleanup.index("} else if (Context->PciDmaContainment)", branch)
        terminal_region = cleanup[branch:nonterminal]
        self.assertIn("Context->HaveOriginalPciAttributes = FALSE;", terminal_region)
        self.assertNotIn("HdaRestorePciAttributes", terminal_region)
        self.assertNotIn("HdaRestoreCodecStateRetry", terminal_region)

    def test_exit_boot_services_never_reenables_ring_dma(self):
        cleanup = extract_c_function(self.source, "HdaCleanupCorbRirbState")
        self.assertGreaterEqual(cleanup.count("ExitBootServices"), 3)
        self.assertIn("State->OriginalCorbCtl & (UINT8)~HDA_CORBCTL_DMA_ENABLE", cleanup)
        self.assertIn("State->OriginalRirbCtl & (UINT8)~HDA_RIRBCTL_DMA_ENABLE", cleanup)

    def test_largest_supported_corb_rirb_ring_is_preferred(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        pos256 = command.find("HDA_RING_SIZE_CAP_256")
        pos16 = command.find("HDA_RING_SIZE_CAP_16")
        pos2 = command.find("HDA_RING_SIZE_CAP_2) != 0")
        self.assertTrue(0 <= pos256 < pos16 < pos2)
        self.assertGreaterEqual(command.count("HDA_RING_SIZE_CAP_256"), 2)

    def test_rirb_overrun_is_detected_and_not_treated_as_timeout(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        poll = command.index("HDAU_CORB_RIRB_TIMEOUT_US")
        overrun = command.index("if ((RirbSts & HDA_RIRBSTS_RIRBOIS) != 0)", poll)
        device_error = command.index("Status = EFI_DEVICE_ERROR;", overrun)
        loop_break = command.index("break;", device_error)
        self.assertLess(overrun, device_error)
        self.assertLess(device_error, loop_break)

    def test_configdata_write_failure_is_propagated(self):
        function = extract_c_function(self.source, "HdaApplyAlcIdPrebootVerbs")
        self.assertIn("Status = HdaSetVerb", function)
        self.assertIn("if (EFI_ERROR(Status))", function)
        self.assertIn("return Status;", function)

    def test_coefficient_index_uses_4bit_verb_16bit_payload(self):
        self.assertRegex(self.header, r"#define\s+HDA_VERB_SET_COEF_INDEX\s+0x5\b")
        self.assertRegex(self.header, r"UINT16\s+Index;")
        self.assertRegex(self.header, r"UINT16\s+OriginalIndex;")
        span = extract_c_function(self.source, "HdaSnapshotCoefficientSpan")
        restore = extract_c_function(self.source, "HdaRestoreCodecState")
        self.assertIn("HDA_VERB4(HDA_VERB_SET_COEF_INDEX", span)
        self.assertIn("HDA_VERB4(HDA_VERB_SET_COEF_INDEX", restore)
        self.assertNotIn("HDA_VERB12(HDA_VERB_SET_COEF_INDEX", span + restore)

    def test_generated_profiles_fit_coefficient_snapshot_capacity(self):
        text = GENERATED_HEADER_PATH.read_text(encoding="utf-8")
        verbs_match = re.search(r"mHdaAlcConfigVerbs\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
        profiles_match = re.search(r"mHdaAlcConfigProfiles\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
        self.assertIsNotNone(verbs_match)
        self.assertIsNotNone(profiles_match)
        verbs = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{8})", verbs_match.group(1))]
        rows = re.findall(
            r'\{\s*0x[0-9A-Fa-f]+,\s*\d+,\s*"(?:[^"\\]|\\.)*",\s*(\d+)U?,\s*(\d+)U?\s*\}',
            profiles_match.group(1),
        )
        worst = 0
        high_index_seen = False
        for off_s, count_s in rows:
            off, count = int(off_s), int(count_s)
            current = {}
            touched = set()
            for encoded in verbs[off:off + count]:
                nid = (encoded >> 20) & 0xFF
                payload = encoded & 0xFFFFF
                verb4 = (payload >> 16) & 0xF
                if verb4 == 0x5:
                    current[nid] = payload & 0xFFFF
                    high_index_seen |= current[nid] > 0xFF
                elif verb4 == 0x4 and nid in current:
                    touched.add((nid, current[nid]))
                    current[nid] = (current[nid] + 1) & 0xFFFF
            worst = max(worst, len(touched))
        self.assertLessEqual(worst, 64)
        self.assertTrue(high_index_seen, "generated data must exercise 16-bit coefficient indices")

    def test_coefficient_snapshot_restores_reused_index_snapshot(self):
        span = extract_c_function(self.source, "HdaSnapshotCoefficientSpan")
        self.assertIn("TouchedIndex[HDAU_MAX_WIDGETS]", span)
        self.assertIn("TouchedIndex[Nid] = TRUE", span)
        self.assertIn("for (Index = 0; Index < Context->CoefIndexSnapshotCount; Index++)", span)
        self.assertIn("if (!TouchedIndex[IndexSnap->Nid]) continue;", span)

    def test_exact_profile_is_snapshotted_or_skipped_before_application(self):
        snapshot = extract_c_function(self.source, "HdaSnapshotCodecState")
        apply_exact = extract_c_function(self.source, "HdaApplyExactProfileVerbs")
        self.assertIn("HdaCaptureProfileVerbState", snapshot)
        self.assertIn("HdaSnapshotCoefficientSpan", snapshot)
        self.assertIn("SkipExactProfileData = TRUE", snapshot)
        self.assertIn("Context->SkipExactProfileData", apply_exact)

    def test_exact_profile_code_does_not_reference_nonexistent_name_field(self):
        apply_exact = extract_c_function(self.source, "HdaApplyExactProfileVerbs")
        self.assertNotIn("Exact->Name", apply_exact)

    def test_general_hardware_access_requires_exit_boot_services_cleanup_hook(self):
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        install = entry.index("Status = HdaInstallExitBootServicesCleanup();")
        notify = entry.index("HdaInstallBootScreenDNotify();")
        dump_path = entry.index("if (mHdaCodecDumpRequested)")
        controller_loop = entry.index("for (Pass = 0; Pass < PassCount; Pass++)")
        self.assertLess(install, notify)
        self.assertLess(install, dump_path)
        self.assertLess(install, controller_loop)
        gate = entry[install:notify]
        self.assertIn("if (EFI_ERROR(Status))", gate)
        self.assertIn("ExitBootServicesCleanupUnavailable", gate)
        self.assertIn("return EFI_SUCCESS;", gate)

    def test_controller_iteration_stops_on_any_retained_hardware_ownership(self):
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        call = entry.index("LastStatus = HdaTryController(PciIo);")
        retry_break = entry.index("if (!EFI_ERROR(LastStatus) || LastStatus == EFI_ABORTED) break;", call)
        region = entry[call:retry_break]
        self.assertIn("mHdaAsyncContext != NULL || mHdaRetainedCorbRirbValid", region)
        self.assertIn("if (EFI_ERROR(LastStatus)) LastStatus = EFI_ABORTED;", region)
        self.assertIn("break;", region)

    def test_deferred_codec_dump_cannot_reenter_controller_critical_section(self):
        deferred = extract_c_function(self.source, "HdaDeferredCodecDumpNotify")
        critical_gate = deferred.index("if (mHdaControllerOperationInProgress) return;")
        consume = deferred.index("mHdaDumpDeferredPending = FALSE;", critical_gate)
        self.assertLess(critical_gate, consume)
        self.assertIn("STATIC BOOLEAN mHdaControllerOperationInProgress = FALSE;", self.source)

    def test_controller_critical_section_services_deferred_dump_after_owner_returns(self):
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        enter = entry.index("mHdaControllerOperationInProgress = TRUE;")
        attempt = entry.index("LastStatus = HdaTryController(PciIo);", enter)
        leave = entry.index("mHdaControllerOperationInProgress = FALSE;", attempt)
        service = entry.index("HdaServiceDeferredCodecDumpIfSafe();", leave)
        self.assertLess(enter, attempt)
        self.assertLess(attempt, leave)
        self.assertLess(leave, service)
        after_service = entry[service : service + 300]
        self.assertIn("mHdaF6DumpDone && mHdaCfg.CodecDumpSkipsPlayback", after_service)
        self.assertIn("LastStatus = EFI_ABORTED;", after_service)

    def test_interactive_dump_notify_is_installed_after_config_requested_dump(self):
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        requested = entry.index("if (mHdaCodecDumpRequested)")
        synchronous_dump = entry.index("HdaDumpAllControllers(Handles, HandleCount)", requested)
        notify = entry.index("HdaInstallBootScreenDNotify();")
        controller_loop = entry.index("for (Pass = 0; Pass < PassCount; Pass++)")
        self.assertLess(requested, synchronous_dump)
        self.assertLess(synchronous_dump, notify)
        self.assertLess(notify, controller_loop)

    def test_build_runs_repeated_source_regressions_before_edk2_setup(self):
        gate = self.build.index("run_source_regressions")
        call = self.build.rindex("run_source_regressions")
        edk2 = self.build.index("prepare_edk2", call)
        self.assertLess(call, edk2)
        self.assertIn('SOURCE_TEST_ROUNDS="${SOURCE_TEST_ROUNDS:-5}"', self.build)
        self.assertIn("python3 -B -m unittest discover -s Tests -v", self.build)
        self.assertIn("PYTHONDONTWRITEBYTECODE=1", self.build)
        self.assertGreater(call, gate)

    def test_boot_args_success_size_cannot_exceed_allocated_capacity(self):
        boot_args = extract_c_function(self.source, "HdaReadAlcIdFromBootArgs")
        self.assertIn("UINTN Capacity;", boot_args)
        self.assertIn("Capacity = Size;", boot_args)
        self.assertIn("Size = Capacity;", boot_args)
        success_guard = boot_args.index("if (Size > Capacity)")
        ascii_scan = boot_args.index("HdaFindAlcIdInAscii")
        utf16_convert = boot_args.index("HdaConvertUtf16BootArgsInPlace")
        self.assertLess(success_guard, ascii_scan)
        self.assertLess(success_guard, utf16_convert)
        self.assertIn("FreePool(Buffer);", boot_args[success_guard:ascii_scan])
        self.assertIn("return;", boot_args[success_guard:ascii_scan])

    def test_stream_descriptor_ownership_is_published_only_before_first_mutation(self):
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        self.assertIn("Context->StreamOffset = 0;", prepare)
        capture = prepare.index("HdaCaptureIdleStreamState(Context, Sd)")
        revalidate = prepare.index("HdaRequireIdleStreamSnapshotUnchanged(Context, Sd)", capture)
        publish = prepare.index("Context->StreamOffset = Sd;", revalidate)
        first_mutation = prepare.index("HdaResetStreamDescriptor(PciIo, Sd)", publish)
        self.assertLess(capture, revalidate)
        self.assertLess(revalidate, publish)
        self.assertLess(publish, first_mutation)
        self.assertIn("if (EFI_ERROR(Status))", prepare[capture:publish])
        self.assertEqual(prepare[:publish].count("Context->StreamOffset = Sd;"), 0)

    def test_hda_atapi_connection_type_matches_spec_encoding(self):
        self.assertIn("#define HDA_DEFAULT_CONN_ATAPI        0x3", self.header)
        self.assertNotIn("#define HDA_DEFAULT_CONN_ATAPI        0xF", self.header)

    def test_critical_create_event_success_requires_nonnull_handle(self):
        deferred = extract_c_function(self.source, "HdaInstallBootScreenDNotify")
        exit_cleanup = extract_c_function(self.source, "HdaInstallExitBootServicesCleanup")
        async_start = extract_c_function(self.source, "HdaStartFastBootPlayback")
        for fn, local_name, published in (
            (deferred, "DeferredEvent", "mHdaDumpDeferredEvent = DeferredEvent;"),
            (exit_cleanup, "CleanupEvent", "mHdaExitBootServicesEvent = CleanupEvent;"),
            (async_start, "AsyncEvent", "mHdaAsyncEvent = AsyncEvent;"),
        ):
            call = fn.index("CreateEvent")
            status_check = fn.index("Status != EFI_SUCCESS", call)
            null_check = fn.index(f"{local_name} == NULL", status_check)
            publish = fn.index(published, null_check)
            self.assertLess(status_check, null_check)
            self.assertLess(null_check, publish)
            failed_region = fn[status_check:null_check]
            self.assertNotIn("CloseEvent", failed_region)
            self.assertNotIn("HdaCloseEventTerminal", failed_region)

    def test_release_metadata_and_diagnostics_are_consistent(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        release_ini = (ROOT / "HDAUChimeRelease.ini").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', self.build)
        for setting in ("Debug=0", "FileLog=0", "ScreenLog=0", "NvramStatus=0"):
            self.assertIn(setting, release_ini)

    def test_release_tree_has_single_consolidated_auxiliary_log(self):
        extensions = {".log", ".diff", ".patch", ".sha256"}
        files = [p for p in ROOT.rglob("*") if p.is_file() and p.suffix.lower() in extensions]
        logs = [p for p in files if p.suffix.lower() == ".log"]
        self.assertEqual([p.relative_to(ROOT).as_posix() for p in logs], ["Release/Logs/BUILD-COMPILE.log"])
        allowed_patch = ROOT / "Audit" / "diffs" / "R37-to-R38.patch"
        self.assertEqual([p for p in files if p.suffix.lower() != ".log" and p != allowed_patch], [])

    def test_release_tree_has_no_cache_or_orphan_temp_artifacts(self):
        forbidden = []
        for path in ROOT.rglob("*"):
            rel = path.relative_to(ROOT).as_posix()
            runner_cache = rel == "Tests/__pycache__" or rel.startswith("Tests/__pycache__/")
            external_edk2 = rel == "edk2" or rel.startswith("edk2/")
            if runner_cache or external_edk2:
                continue
            if path.is_dir() and path.name == "__pycache__":
                forbidden.append(rel + "/")
            elif path.is_file() and (
                path.suffix.lower() == ".pyc"
                or path.name == ".DS_Store"
                or ".tmp." in path.name
            ):
                forbidden.append(rel)
        self.assertEqual(forbidden, [])

    def test_active_source_uses_requested_copyright_and_contact(self):
        active = [
            ROOT / "Build-HDAUniversalBootChimeDxe.command",
            ROOT / "HDAUniversalBootChimeDxePkg.dsc",
            ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.c",
            ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h",
            ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf",
            ROOT / "docs" / "PROJECT-HISTORY.md",
        ]
        for path in active:
            text = path.read_text(encoding="utf-8")
            self.assertIn("Copyright © 2026 DNMTechLabs", text, path.name)
            self.assertIn("dnmtechlabs@gmail.com", text, path.name)

    def test_codec_dump_helper_cleans_interrupted_temp_file(self):
        helper = (ROOT / "Enable-HDAUCodecDump-NextBoot.command").read_text(encoding="utf-8")
        self.assertIn('TMP_CFG=""', helper)
        self.assertIn("trap cleanup_tmp EXIT", helper)
        self.assertIn("trap 'cleanup_tmp; exit 129' HUP", helper)
        self.assertIn("trap 'cleanup_tmp; exit 130' INT", helper)
        self.assertIn("trap 'cleanup_tmp; exit 143' TERM", helper)
        self.assertIn('rm -f "$TMP_CFG"', helper)
        self.assertIn('mv -f "$tmp" "$file" ||', helper)

    def test_codec_dump_helper_uses_single_deterministic_backup(self):
        helper = (ROOT / "Enable-HDAUCodecDump-NextBoot.command").read_text(encoding="utf-8")
        self.assertIn('CFG_BACKUP="$CFG.backup"', helper)
        self.assertIn('RELEASE_CFG_BACKUP="$RELEASE_CFG.backup"', helper)
        self.assertIn('if [ ! -f "$CFG_BACKUP" ]; then', helper)
        self.assertIn('cp "$CFG" "$CFG_BACKUP" || {', helper)
        self.assertIn('exit 1', helper)
        self.assertNotIn('backup.$(date', helper)

    def test_build_tooling_uses_one_log_and_pinned_edk2_ref(self):
        self.assertIn('AUTO_FETCH_EDK2="${AUTO_FETCH_EDK2:-1}"', self.build)
        self.assertIn('EDK2_DEFAULT_REF="edk2-stable202608"', self.build)
        self.assertIn('EDK2_REF="${EDK2_REF:-$EDK2_DEFAULT_REF}"', self.build)
        self.assertIn('EDK2_DEFAULT_REF="edk2-stable202608"', self.setup)
        self.assertIn('EDK2_REF="${EDK2_REF:-$EDK2_DEFAULT_REF}"', self.setup)
        self.assertIn('MASTER_LOG="Release/Logs/BUILD-COMPILE.log"', self.build)
        self.assertNotRegex(self.build, r"build-master-.*date|build-(?:debug|release)\.log")
        self.assertNotIn("release/", self.build)

    def test_codec_dump_requires_pci_attribute_snapshot_before_mmio_enable(self):
        dump = extract_c_function(self.source, "HdaDumpControllerFull")
        get_pos = dump.index("HdaGetPciAttributesExact(PciIo, &OriginalAttributes)")
        failure_pos = dump.index("Controller attributes snapshot failed", get_pos)
        enable_pos = dump.index("EfiPciIoAttributeOperationEnable", failure_pos)
        region = dump[get_pos:enable_pos]
        self.assertLess(get_pos, failure_pos)
        self.assertLess(failure_pos, enable_pos)
        self.assertIn("if (EFI_ERROR(Status))", region)
        self.assertIn("return Status;", region)

    def test_codec_dump_restore_failure_is_not_masked(self):
        dump = extract_c_function(self.source, "HdaDumpControllerFull")
        restore_pos = dump.index("Controller managed attributes restore failed")
        region = dump[restore_pos:restore_pos + 700]
        self.assertIn("Status = RestoreStatus;", region)
        self.assertNotIn("if (!EFI_ERROR(Status))", region)

    def test_corb_rirb_successful_allocatebuffer_requires_nonnull_host(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        corb_publish = command.index("RingState.CorbAllocated = TRUE;")
        corb_zero = command.index("ZeroMem(RingState.CorbHost", corb_publish)
        corb_guard = command.index("if (RingState.CorbHost == NULL)", corb_publish)
        rirb_publish = command.index("RingState.RirbAllocated = TRUE;")
        rirb_zero = command.index("ZeroMem(RingState.RirbHost", rirb_publish)
        rirb_guard = command.index("if (RingState.RirbHost == NULL)", rirb_publish)
        self.assertLess(corb_publish, corb_guard)
        self.assertLess(corb_guard, corb_zero)
        self.assertLess(rirb_publish, rirb_guard)
        self.assertLess(rirb_guard, rirb_zero)
        self.assertIn("Status = EFI_DEVICE_ERROR;", command[corb_guard:corb_zero])
        self.assertIn("Status = EFI_DEVICE_ERROR;", command[rirb_guard:rirb_zero])

    def test_corb_rirb_cleanup_error_overrides_transport_error(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        cleanup_log = command.index("CORB/RIRB cleanup failed")
        region = command[cleanup_log:cleanup_log + 700]
        self.assertIn("Status = CleanupStatus;", region)
        self.assertNotIn("if (!EFI_ERROR(Status)) Status = CleanupStatus;", region)

    def test_codec_dump_propagates_controller_errors_and_stops_on_retained_rings(self):
        dump_all = extract_c_function(self.source, "HdaDumpAllControllers")
        self.assertIn("EFI_STATUS FirstControllerError;", dump_all)
        self.assertIn("FirstControllerError = EFI_SUCCESS;", dump_all)
        self.assertIn("mHdaRetainedCorbRirbValid", dump_all)
        self.assertIn("Codec dump stopped: CORB/RIRB cleanup ownership remains pending", dump_all)
        self.assertIn("CodecDumpControllerError", dump_all)
        self.assertIn("if (FirstControllerError != EFI_SUCCESS) return FirstControllerError;", dump_all)


    def test_corb_rirb_dma_ranges_cannot_wrap_physical_address_space(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        size_check = command.index("if (CorbBytes < Entries * sizeof(UINT32) || RirbBytes < Entries * sizeof(UINT64))")
        wrap_check = command.index("CORB/RIRB DMA range wraps physical address space", size_check)
        address_programming = command.index("HDA_REG_CORBLBASE", wrap_check)
        region = command[size_check:address_programming]
        self.assertIn("MAX_UINT64 - (UINT64)(CorbBytes - 1U)", region)
        self.assertIn("MAX_UINT64 - (UINT64)(RirbBytes - 1U)", region)
        self.assertIn("Status = EFI_DEVICE_ERROR;", region)
        self.assertLess(size_check, wrap_check)
        self.assertLess(wrap_check, address_programming)

    def test_bdl_dma_range_cannot_wrap_physical_address_space(self):
        allocate = extract_c_function(self.source, "HdaAllocatePlaybackBuffers")
        mapped = allocate.index("Context->BdlBytes = HDAU_BDL_ENTRY_COUNT * sizeof(HDA_BDL_ENTRY);")
        wrap = allocate.index("DMA BDL device range wraps physical address space", mapped)
        flush = allocate.index("HdaPciFlushExact(Context->PciIo)", wrap)
        region = allocate[mapped:flush]
        self.assertIn("MAX_UINT64 - (UINT64)(Context->BdlBytes - 1U)", region)
        self.assertIn("return EFI_DEVICE_ERROR;", region)
        self.assertLess(mapped, wrap)
        self.assertLess(wrap, flush)

    def test_codec_dump_statests_mmio_failure_is_not_misclassified_as_no_codecs(self):
        dump = extract_c_function(self.source, "HdaDumpControllerFull")
        read = dump.index("Status = HdaMemRead16(PciIo, HDA_REG_STATESTS, &StateSts);")
        failure = dump.index("STATESTS read failed: %r", read)
        no_codecs = dump.index("No codecs reported by STATESTS", failure)
        region = dump[read:no_codecs]
        self.assertIn("if (EFI_ERROR(Status))", region)
        self.assertIn("goto Exit;", region)
        self.assertLess(read, failure)
        self.assertLess(failure, no_codecs)


    def test_32bit_dma_controllers_validate_full_mapped_ranges_not_only_bases(self):
        rings = extract_c_function(self.source, "HdaCorbRirbCommand")
        self.assertIn("0xFFFFFFFFULL - (UINT64)(CorbBytes - 1U)", rings)
        self.assertIn("0xFFFFFFFFULL - (UINT64)(RirbBytes - 1U)", rings)
        self.assertIn("CORB/RIRB range exceeds 4 GiB", rings)

        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        self.assertIn("0xFFFFFFFFULL - (UINT64)(Context->AudioBufferBytes - 1U)", prepare)
        self.assertIn("0xFFFFFFFFULL - (UINT64)(Context->BdlBytes - 1U)", prepare)
        self.assertIn("mapped range exceeds 4 GiB", prepare)


    def test_codec_dump_preserves_bus_master_containment_for_retained_rings(self):
        dump = extract_c_function(self.source, "HdaDumpControllerFull")
        self.assertIn("UINT64 RestoreAttributes;", dump)
        self.assertIn("mHdaRetainedCorbRirbState.PciIo == PciIo", dump)
        self.assertIn("mHdaRetainedCorbRirbState.DmaContainment || mHdaRetainedCorbRirbState.DmaStopUnconfirmed", dump)
        self.assertIn("RestoreAttributes &= ~((UINT64)EFI_PCI_IO_ATTRIBUTE_BUS_MASTER);", dump)
        self.assertIn("PCI bus-master restore suppressed while CORB/RIRB DMA ownership is retained", dump)
        self.assertIn("mHdaRetainedCorbRirbState.DmaContainment = TRUE;", dump)

    def test_codec_dump_stops_same_controller_when_ring_ownership_is_retained(self):
        dump = extract_c_function(self.source, "HdaDumpControllerFull")
        codec_call = dump.index("HdaDumpCodecFull(File, PciIo, Info, Codec, StateSts);")
        retained = dump.index("mHdaRetainedCorbRirbValid", codec_call)
        exit_jump = dump.index("goto Exit;", retained)
        self.assertIn("Status = EFI_ABORTED;", dump[retained:exit_jump])
        self.assertIn("Codec enumeration stopped: CORB/RIRB cleanup ownership remains pending", dump[retained:exit_jump])

    def test_requested_codec_dump_never_enters_playback_with_retained_ring_ownership(self):
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        requested = entry.index("if (mHdaCodecDumpRequested)")
        dump_call = entry.index("HdaDumpAllControllers(Handles, HandleCount)", requested)
        retained = entry.index("if (mHdaRetainedCorbRirbValid)", dump_call)
        loop = entry.index("for (Pass = 0; Pass < PassCount; Pass++)", retained)
        region = entry[retained:loop]
        self.assertIn("CodecDumpCleanupOwnershipRetained", region)
        self.assertIn("return EFI_SUCCESS;", region)
        self.assertLess(dump_call, retained)
        self.assertLess(retained, loop)

    def test_preboot_key_poll_has_finite_input_drain_budget(self):
        poll = extract_c_function(self.source, "HdaPollForF6")
        self.assertIn("#define HDAU_MAX_INPUT_EVENTS_PER_POLL 64U", self.header)
        bounded = "for (KeyReads = 0; KeyReads < HDAU_MAX_INPUT_EVENTS_PER_POLL; KeyReads++)"
        self.assertGreaterEqual(poll.count(bounded), 2)
        self.assertNotIn("while (!EFI_ERROR(TextInEx->ReadKeyStrokeEx", poll)
        self.assertNotIn("while (!EFI_ERROR(gST->ConIn->ReadKeyStroke", poll)

    def test_corb_rirb_cleanup_reverifies_dma_disabled_before_freeing_resources(self):
        cleanup = extract_c_function(self.source, "HdaCleanupCorbRirbState")
        restore_flush = cleanup.index("CleanupStatus = HdaPciFlushExact(State->PciIo);", cleanup.index("RestoreError = EFI_SUCCESS;"))
        verify_log = cleanup.index("CORB/RIRB restore verification found DMA still enabled", restore_flush)
        registers_clear = cleanup.index("State->RegistersTouched = FALSE;", verify_log)
        region = cleanup[restore_flush:registers_clear]
        self.assertGreaterEqual(region.count("HdaMemRead8(State->PciIo, HDA_REG_CORBCTL"), 1)
        self.assertGreaterEqual(region.count("HdaMemRead8(State->PciIo, HDA_REG_RIRBCTL"), 1)
        self.assertIn("HDA_CORBCTL_DMA_ENABLE", region)
        self.assertIn("HDA_RIRBCTL_DMA_ENABLE", region)
        self.assertIn("RestoreError = EFI_DEVICE_ERROR;", region)

    def test_append_log_has_hard_size_cap_and_recreates_single_file(self):
        log_open = extract_c_function(self.source, "HdaOpenDebugLog")
        self.assertIn("#define HDAU_MAX_LOG_FILE_BYTES", self.header)
        self.assertIn("LogSize >= HDAU_MAX_LOG_FILE_BYTES", log_open)
        cap = log_open.index("LogSize >= HDAU_MAX_LOG_FILE_BYTES")
        reopen = log_open.index("goto OpenLog;", cap)
        region = log_open[cap:reopen]
        self.assertIn("mHdaLogFile->Delete(mHdaLogFile)", region)
        self.assertIn("Append = FALSE;", region)

    def test_file_warning_statuses_are_not_accepted_as_clean_log_io(self):
        logger = extract_c_function(self.source, "HdaLog")
        log_open = extract_c_function(self.source, "HdaOpenDebugLog")
        dump_all = extract_c_function(self.source, "HdaDumpAllControllers")
        self.assertIn("WriteStatus != EFI_SUCCESS || Size != ExpectedSize", logger)
        self.assertIn("if (WriteStatus != EFI_SUCCESS)", logger)
        self.assertIn("if (Status != EFI_SUCCESS)", log_open[log_open.index("OldFile->Delete"):])
        self.assertIn("if (Status != EFI_SUCCESS)", dump_all[dump_all.index("mHdaLogFile->Flush"):])

    def test_config_file_warnings_are_rejected_before_parsing(self):
        read_config = extract_c_function(self.source, "HdaReadConfigFromPath")
        read_call = read_config.index("Status = File->Read")
        parse_call = read_config.index("HdaParseConfigBuffer", read_call)
        region = read_config[read_call:parse_call]
        self.assertIn("if (Status != EFI_SUCCESS)", region)
        self.assertIn("Status = EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;", region)
        self.assertIn("if (Status == EFI_SUCCESS)", region)


    def test_debug_init_requires_exact_openvolume_success_and_closes_ambiguous_root(self):
        body = extract_c_function(self.source, "HdaDebugInit")
        call = body.index("Fs->OpenVolume")
        failure = body.index("if (Status != EFI_SUCCESS)", call)
        self.assertIn("Root = NULL;", body[failure:])
        # A failed OpenVolume() output is not caller-owned and must not be closed.
        next_success = body.index("if (Root == NULL)", failure)
        self.assertNotIn("Close(", body[failure:next_success])
        self.assertIn("HdaCloseFileHandleTerminal(&Root)", body[next_success:])

    def test_single_shot_protocol_guard_requires_clean_install_and_valid_handle(self):
        body = extract_c_function(self.source, "HdaAcquireBootGuard")
        call = body.index("InstallProtocolInterface")
        failure = body.index("if (Status != EFI_SUCCESS)", call)
        success_publish = body.index("mHdaLoadedProtocolHandle = ProtocolHandle;", failure)
        region = body[failure:success_publish]
        self.assertIn("ProtocolHandle = NULL;", region)
        self.assertIn("mHdaLoadedProtocolHandle = NULL;", region)
        self.assertNotIn("UninstallProtocolInterface", region)
        self.assertIn("else if (ProtocolHandle == NULL)", region)

    def test_deferred_codec_dump_failure_remains_retryable(self):
        worker = extract_c_function(self.source, "HdaDeferredCodecDumpNotify")
        call = worker.index("DumpStatus = HdaDumpAllControllersDynamic(TRUE);")
        done = worker.index("mHdaF6DumpDone = TRUE;", call)
        failure = worker.index("if (EFI_ERROR(DumpStatus))", call)
        failure_region = worker[failure:done]
        self.assertIn('HdaSetLastStatus("CodecDumpDeferredFailed")', failure_region)
        self.assertIn("mHdaF6DumpInProgress = FALSE;", failure_region)
        self.assertIn("return;", failure_region)
        self.assertNotIn("HdaUninstallBootScreenDNotify", failure_region)


    def test_pci_attribute_reads_require_exact_success_and_read_back_mutated_bits(self):
        getter = extract_c_function(self.source, "HdaGetPciAttributesExact")
        verifier = extract_c_function(self.source, "HdaVerifyPciAttributeBits")
        self.assertIn("if (Status == EFI_SUCCESS) return EFI_SUCCESS;", getter)
        self.assertIn("return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;", getter)
        self.assertIn("HdaGetPciAttributesExact(PciIo, &CurrentAttributes)", verifier)
        self.assertIn("(CurrentAttributes & Mask) != (ExpectedAttributes & Mask)", verifier)
        self.assertIn("return EFI_DEVICE_ERROR;", verifier)

    def test_playback_pci_memory_enable_is_verified_before_mmio(self):
        attempt = extract_c_function(self.source, "HdaTryController")
        enable = attempt.index("EfiPciIoAttributeOperationEnable")
        verify = attempt.index("HdaVerifyPciAttributeBits", enable)
        ready = attempt.index("HdaControllerEnsureReady", verify)
        region = attempt[enable:ready]
        self.assertIn("Status != EFI_SUCCESS", region)
        self.assertIn("EFI_PCI_IO_ATTRIBUTE_MEMORY", region)
        self.assertNotIn("EFI_PCI_IO_ATTRIBUTE_MEMORY | EFI_PCI_IO_ATTRIBUTE_BUS_MASTER", region)
        self.assertIn("PciAttributesVerifyFailed", region)
        self.assertLess(enable, verify)
        self.assertLess(verify, ready)

    def test_pci_restore_is_verified_before_ownership_is_cleared(self):
        restore = extract_c_function(self.source, "HdaRestorePciAttributes")
        helper = extract_c_function(self.source, "HdaApplyPciAttributeBits")
        apply_pos = restore.index("HdaApplyPciAttributeBits")
        clear = restore.index("Context->HaveOriginalPciAttributes = FALSE;", apply_pos)
        region = restore[apply_pos:clear]
        self.assertIn("ManagedMask", restore)
        self.assertIn("~Context->OriginalPciAttributes", restore)
        self.assertIn("if (EFI_ERROR(Status))", region)
        self.assertIn("EfiPciIoAttributeOperationDisable", helper)
        self.assertIn("EfiPciIoAttributeOperationEnable", helper)
        self.assertIn("HdaVerifyPciAttributeBits", helper)
        self.assertNotIn("EfiPciIoAttributeOperationSet", helper)
        self.assertLess(apply_pos, clear)

    def test_dma_containment_requires_bus_master_read_back(self):
        stop = extract_c_function(self.source, "HdaStopOutputStream")
        contain = stop.index("EfiPciIoAttributeOperationDisable")
        verify = stop.index("HdaVerifyPciAttributeBits", contain)
        publish = stop.index("Context->PciDmaContainment = TRUE;", verify)
        region = stop[contain:publish]
        self.assertIn("EFI_PCI_IO_ATTRIBUTE_BUS_MASTER", region)
        self.assertIn("containment read-back failed", region)
        self.assertNotIn("EfiPciIoAttributeOperationSet", region)
        self.assertLess(contain, verify)
        self.assertLess(verify, publish)

        rings = extract_c_function(self.source, "HdaCleanupCorbRirbState")
        disable = rings.index("EfiPciIoAttributeOperationDisable")
        ring_verify = rings.index("HdaVerifyPciAttributeBits", disable)
        retained = rings.index("State->DmaContainment = TRUE;", ring_verify)
        self.assertLess(disable, ring_verify)
        self.assertLess(ring_verify, retained)

    def test_event_and_key_notify_ownership_requires_exact_success(self):
        install = extract_c_function(self.source, "HdaInstallBootScreenDNotify")
        uninstall = extract_c_function(self.source, "HdaUninstallBootScreenDNotify")
        cleanup = extract_c_function(self.source, "HdaCleanupAsyncPlayback")
        fast = extract_c_function(self.source, "HdaStartFastBootPlayback")
        exit_hook = extract_c_function(self.source, "HdaInstallExitBootServicesCleanup")
        callback = extract_c_function(self.source, "HdaAsyncPlaybackTick")
        key = extract_c_function(self.source, "HdaF6KeyNotify")

        self.assertIn("if (Status != EFI_SUCCESS)", install)
        self.assertIn("if (DeferredEvent == NULL)", install)
        self.assertIn("mHdaDumpDeferredEvent = DeferredEvent;", install)
        self.assertIn("UEFI 2.11 documents EFI_OUT_OF_RESOURCES as the only failure result", install)
        self.assertIn("Status == EFI_SUCCESS || Status == EFI_INVALID_PARAMETER", uninstall)
        self.assertIn("HdaCloseEventTerminal(&mHdaAsyncEvent)", cleanup)
        self.assertIn("Status != EFI_SUCCESS || AsyncEvent == NULL", fast)
        self.assertIn("if (Status != EFI_SUCCESS)", fast[fast.index("SetTimer"):])
        self.assertIn("if (Status != EFI_SUCCESS)", exit_hook)
        self.assertIn("if (CleanupEvent == NULL)", exit_hook)
        self.assertIn("HdaCloseEventTerminal(&EventToClose)", callback)
        self.assertIn("if (Status != EFI_SUCCESS)", key)

    def test_protocol_and_handle_enumeration_reject_warning_statuses(self):
        dump_all = extract_c_function(self.source, "HdaDumpAllControllers")
        dynamic = extract_c_function(self.source, "HdaDumpAllControllersDynamic")
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        pci = extract_c_function(self.source, "HdaReadPciInfo")
        guard = extract_c_function(self.source, "HdaAcquireBootGuard")

        self.assertIn("Status != EFI_SUCCESS || PciIo == NULL", dump_all)
        self.assertIn("Status == EFI_SUCCESS || Status == EFI_NOT_FOUND", dump_all)
        self.assertIn("if (Status != EFI_SUCCESS)", dynamic)
        self.assertIn("Handles = NULL;", dynamic)
        self.assertIn("HandleCount = 0;", dynamic)
        self.assertIn("if (Status != EFI_SUCCESS)", entry[entry.index("LocateHandleBuffer"):])
        self.assertIn("Status != EFI_SUCCESS || PciIo == NULL", entry)
        self.assertIn("if (Status != EFI_SUCCESS) return FALSE;", pci)
        self.assertIn("Status == EFI_SUCCESS && Size == sizeof(Value) && Value == 1", guard)
        self.assertIn("Status == EFI_SUCCESS && Existing != NULL", guard)

    def test_codec_dump_warning_status_cannot_be_lost_by_efi_error_macro(self):
        dump_all = extract_c_function(self.source, "HdaDumpAllControllers")
        controller = dump_all.index("Status = HdaDumpControllerFull")
        final_return = dump_all.rindex("return (Dumped != 0)")
        region = dump_all[controller:final_return]
        self.assertIn("Status = HdaNormalizeExactSuccess(Status);", region)
        self.assertIn("if (FirstControllerError == EFI_SUCCESS) FirstControllerError = Status;", region)
        self.assertIn("if (FirstControllerError != EFI_SUCCESS)", region)
        self.assertNotIn("EFI_ERROR(FirstControllerError)", region)

    def test_mmio_and_location_outputs_require_exact_success(self):
        pci = extract_c_function(self.source, "HdaReadPciInfo")
        self.assertIn("return (Status == EFI_SUCCESS);", pci)
        for name in ("HdaMemRead8", "HdaMemRead16", "HdaMemRead32",
                     "HdaMemWrite8", "HdaMemWrite16", "HdaMemWrite32"):
            fn = extract_c_function(self.source, name)
            self.assertIn("HdaNormalizeExactSuccess(Status)", fn)

    def test_dma_acquisition_rejects_warning_status_before_publishing_ownership(self):
        rings = extract_c_function(self.source, "HdaCorbRirbCommand")
        playback = extract_c_function(self.source, "HdaAllocatePlaybackBuffers")
        self.assertGreaterEqual(rings.count("if (Status != EFI_SUCCESS)"), 4)
        self.assertIn("Status = HdaNormalizeExactSuccess(Status);", rings)
        corb_alloc = rings.index("RingState.CorbHost = CorbHost;")
        corb_check = rings.rfind("if (Status != EFI_SUCCESS)", 0, corb_alloc)
        self.assertLess(corb_check, corb_alloc)
        corb_map = rings.index("RingState.CorbMapping = CorbMapping;")
        map_check = rings.rfind("if (Status != EFI_SUCCESS)", 0, corb_map)
        self.assertLess(map_check, corb_map)

        self.assertIn("if (Status != EFI_SUCCESS) return HdaNormalizeExactSuccess(Status);", playback)
        self.assertGreaterEqual(playback.count("if (Status != EFI_SUCCESS)"), 4)
        audio_publish = playback.index("Context->AudioMapping = AudioMapping;")
        audio_check = playback.rfind("if (Status != EFI_SUCCESS)", 0, audio_publish)
        self.assertLess(audio_check, audio_publish)
        bdl_publish = playback.index("Context->BdlMapping = BdlMapping;")
        bdl_check = playback.rfind("if (Status != EFI_SUCCESS)", 0, bdl_publish)
        self.assertLess(bdl_check, bdl_publish)

    def test_dma_release_requires_exact_success_before_clearing_ownership(self):
        rings = extract_c_function(self.source, "HdaCleanupCorbRirbState")
        playback = extract_c_function(self.source, "HdaFreePlaybackBuffers")

        # PCI I/O Unmap consumes the mapping even if data commit reports an
        # error; ownership must be cleared immediately and never retried.
        rirb_unmap = rings.index("State->PciIo->Unmap(State->PciIo, State->RirbMapping)")
        rirb_clear = rings.index("State->RirbMapping = NULL;", rirb_unmap)
        rirb_status = rings.index("if (CleanupStatus != EFI_SUCCESS)", rirb_clear)
        self.assertLess(rirb_unmap, rirb_clear)
        self.assertLess(rirb_clear, rirb_status)

        bdl_unmap = playback.index("Context->PciIo->Unmap(Context->PciIo, Context->BdlMapping)")
        bdl_clear = playback.index("Context->BdlMapping = NULL;", bdl_unmap)
        bdl_status = playback.index("if (Status != EFI_SUCCESS)", bdl_clear)
        self.assertLess(bdl_unmap, bdl_clear)
        self.assertLess(bdl_clear, bdl_status)

        # FreeBuffer(INVALID_PARAMETER) and FreePages(NOT_FOUND) prove the
        # caller no longer has the corresponding allocation ownership.
        self.assertIn("CleanupStatus == EFI_SUCCESS || CleanupStatus == EFI_INVALID_PARAMETER", rings)
        self.assertIn("Status == EFI_SUCCESS || Status == EFI_NOT_FOUND", playback)

    def test_pci_flush_warning_is_normalized_before_hardware_progress(self):
        helper = extract_c_function(self.source, "HdaPciFlushExact")
        self.assertIn("HdaNormalizeExactSuccess(PciIo->Flush(PciIo))", helper)
        source_without_file_flushes = self.source.replace("mHdaLogFile->Flush", "FileFlush")
        self.assertNotIn("PciIo->Flush(PciIo)", source_without_file_flushes.replace("HdaNormalizeExactSuccess(PciIo->Flush(PciIo))", ""))
        self.assertNotIn("Context->PciIo->Flush(Context->PciIo)", source_without_file_flushes)
        self.assertNotIn("State->PciIo->Flush(State->PciIo)", source_without_file_flushes)

    def test_boot_args_requires_clean_success_before_text_scan(self):
        reader = extract_c_function(self.source, "HdaReadAlcIdFromBootArgs")
        second = reader.rindex('Status = gRT->GetVariable(L"boot-args"')
        scan = reader.index("HdaFindAlcIdInAscii", second)
        region = reader[second:scan]
        self.assertIn("if (Status != EFI_SUCCESS)", region)
        self.assertIn("if (Size > Capacity)", region)


class Uefi211ConformanceRound14Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")

    def test_failed_create_event_output_is_never_consumed(self):
        for name, local in (
            ("HdaInstallBootScreenDNotify", "DeferredEvent"),
            ("HdaInstallExitBootServicesCleanup", "CleanupEvent"),
        ):
            fn = extract_c_function(self.source, name)
            call = fn.index("CreateEvent")
            failure = fn.index("if (Status != EFI_SUCCESS)", call)
            null_contract = fn.index(f"if ({local} == NULL)", failure)
            failed_region = fn[failure:null_contract]
            self.assertIn(f"{local} = NULL;", failed_region)
            self.assertNotIn("CloseEvent", failed_region)
            self.assertNotIn("HdaCloseEventTerminal", failed_region)

    def test_failed_file_open_outputs_are_never_closed(self):
        read_cfg = extract_c_function(self.source, "HdaReadConfigFromPath")
        call = read_cfg.index("Root->Open")
        failure = read_cfg.index("if (Status != EFI_SUCCESS)", call)
        end = read_cfg.index("if (File == NULL)", failure)
        self.assertNotIn("Close(", read_cfg[failure:end])
        self.assertIn("File = NULL;", read_cfg[failure:end])

        root = extract_c_function(self.source, "HdaOpenLoadedImageRoot")
        call = root.index("Fs->OpenVolume")
        failure = root.index("if (Status != EFI_SUCCESS)", call)
        end = root.index("if (*Root == NULL)", failure)
        self.assertNotIn("Close(", root[failure:end])
        self.assertIn("*Root = NULL;", root[failure:end])

    def test_file_close_helper_is_terminal(self):
        fn = extract_c_function(self.source, "HdaCloseFileHandleTerminal")
        call = fn.index("LocalHandle->Close")
        clear = fn.index("*Handle = NULL;", call)
        self.assertLess(call, clear)
        self.assertNotIn("return Status;", fn[call:clear])

    def test_file_delete_is_terminal_even_on_delete_warning(self):
        fn = extract_c_function(self.source, "HdaOpenDebugLog")
        for token, clear in (
            ("OldFile->Delete(OldFile)", "OldFile = NULL;"),
            ("mHdaLogFile->Delete(mHdaLogFile)", "mHdaLogFile = NULL;"),
        ):
            call = fn.index(token)
            clear_pos = fn.index(clear, call)
            status_pos = fn.index("if (Status != EFI_SUCCESS)", clear_pos)
            self.assertLess(call, clear_pos)
            self.assertLess(clear_pos, status_pos)

    def test_close_event_helper_is_terminal(self):
        fn = extract_c_function(self.source, "HdaCloseEventTerminal")
        call = fn.index("gBS->CloseEvent")
        clear = fn.index("*Event = NULL;", call)
        self.assertLess(call, clear)
        self.assertNotIn("gBS->CloseEvent", fn[clear:])

    def test_pci_unmap_is_never_retried_after_status(self):
        for name, token, clear in (
            ("HdaCleanupCorbRirbState", "State->PciIo->Unmap(State->PciIo, State->RirbMapping)", "State->RirbMapping = NULL;"),
            ("HdaFreePlaybackBuffers", "Context->PciIo->Unmap(Context->PciIo, Context->BdlMapping)", "Context->BdlMapping = NULL;"),
        ):
            fn = extract_c_function(self.source, name)
            call = fn.index(token)
            clear_pos = fn.index(clear, call)
            self.assertLess(call, clear_pos)
            self.assertEqual(fn[call:].count(token), 1)

    def test_unregister_invalid_parameter_is_terminal_and_undocumented_status_quarantines(self):
        fn = extract_c_function(self.source, "HdaUninstallBootScreenDNotify")
        self.assertGreaterEqual(fn.count("Status == EFI_SUCCESS || Status == EFI_INVALID_PARAMETER"), 2)
        self.assertIn("mHdaBootScreenNotifyQuarantined = TRUE;", fn)
        self.assertIn("Tokens are intentionally forgotten: retry would be unsafe.", fn)

    def test_exit_boot_services_callback_has_no_forbidden_boot_service_release_calls(self):
        fn = extract_c_function(self.source, "HdaExitBootServicesCleanup")
        code = re.sub(r"/\*.*?\*/", "", fn, flags=re.S)
        for forbidden in (
            "CloseEvent(", "SetTimer(", "FreePages(", "FreePool(", "Unmap(",
            "FreeBuffer(", "UnregisterKeyNotify(", "Stall(", "SetVariable(",
        ):
            self.assertNotIn(forbidden, code)
        for setting in (
            "mHdaCfg.Debug = FALSE;",
            "mHdaCfg.FileLog = FALSE;",
            "mHdaCfg.ScreenLog = FALSE;",
            "mHdaCfg.NvramStatus = FALSE;",
            "mHdaCfg.CorbRirbFallback = FALSE;",
        ):
            self.assertIn(setting, fn)
        first_cleanup = fn.index("HdaCleanupAsyncPlayback(TRUE)")
        for setting in ("mHdaCfg.Debug = FALSE;", "mHdaCfg.CorbRirbFallback = FALSE;"):
            self.assertLess(fn.index(setting), first_cleanup)

    def test_stall_is_centralized_and_boot_services_safe_during_exit(self):
        fn = extract_c_function(self.source, "HdaStall")
        self.assertIn("mHdaExitBootServicesInProgress || gBS == NULL", fn)
        self.assertIn("CpuPause()", fn)
        source_without_helper = self.source.replace(fn, "")
        self.assertNotIn("gBS->Stall(", source_without_helper)

    def test_keyboard_reads_require_exact_success(self):
        fn = extract_c_function(self.source, "HdaPollForF6")
        self.assertGreaterEqual(fn.count("if (Status != EFI_SUCCESS) break;"), 2)

    def test_gettime_output_is_used_only_on_exact_success(self):
        fn = extract_c_function(self.source, "HdaDumpAllControllers")
        self.assertIn("HaveTime = (gRT->GetTime(&Now, NULL) == EFI_SUCCESS);", fn)

    def test_controller_dump_root_handle_uses_terminal_close_helper(self):
        fn = extract_c_function(self.source, "HdaDumpAllControllers")
        self.assertIn("if (Status == EFI_SUCCESS && Root != NULL)", fn)
        self.assertIn("HdaCloseFileHandleTerminal(&Root)", fn)
        self.assertNotIn("Root->Close(Root)", fn)

    def test_build_stages_project_outside_edk2_workspace(self):
        self.assertIn('STAGING_ROOT="$(mktemp -d', self.build)
        self.assertIn('STAGED_PKG_DIR="$STAGING_ROOT/$PKG"', self.build)
        self.assertIn('export PACKAGES_PATH="$STAGING_ROOT${PACKAGES_PATH:+:$PACKAGES_PATH}"', self.build)
        self.assertNotIn('rm -rf "$WORKSPACE/$PKG"', self.build)
        self.assertNotIn('cp -R HDAUniversalBootChimeDxe "$WORKSPACE/', self.build)

    def test_build_restores_temporary_basetools_patch(self):
        self.assertIn("restore_basetools_wrapper()", self.build)
        self.assertIn("trap cleanup_build_transaction EXIT", self.build)
        self.assertIn('cp -p "$BASETOOLS_WRAPPER_BACKUP" "$BASETOOLS_WRAPPER"', self.build)
        self.assertNotIn(".hdau-backup", self.build)

    def test_build_failure_removes_partial_release_outputs_but_keeps_master_log(self):
        cleanup_start = self.build.index("cleanup_build_transaction()")
        cleanup_end = self.build.index("trap cleanup_build_transaction EXIT", cleanup_start)
        cleanup = self.build[cleanup_start:cleanup_end]
        self.assertIn("rm -rf Release/Debug Release/Release Release/EFI-Test-Pack", cleanup)
        self.assertIn('rm -f Release/"${APP}"-*-DEBUG-RELEASE.zip', cleanup)
        self.assertNotIn("BUILD-COMPILE.log", cleanup)

    def test_r16_version_is_consistent_in_active_metadata(self):
        header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        self.assertIn("1.0.87-final-audit", header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', self.build)


class Uefi211ConformanceRound15Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_notification_events_use_callback_tpl_not_application_tpl(self):
        for name in (
            "HdaInstallBootScreenDNotify",
            "HdaInstallExitBootServicesCleanup",
            "HdaStartFastBootPlayback",
        ):
            fn = extract_c_function(self.source, name)
            self.assertIn("TPL_CALLBACK", fn)
            self.assertNotIn("TPL_APPLICATION", fn)

    def test_playback_dma_uses_bus_master_read_not_common_buffer(self):
        alloc = extract_c_function(self.source, "HdaAllocatePlaybackBuffers")
        self.assertEqual(alloc.count("EfiPciIoOperationBusMasterRead"), 2)
        self.assertNotIn("EfiPciIoOperationBusMasterCommonBuffer", alloc)
        self.assertIn("gBS->AllocatePages", alloc)

        ring = extract_c_function(self.source, "HdaCorbRirbCommand")
        self.assertEqual(ring.count("EfiPciIoOperationBusMasterCommonBuffer"), 2)
        self.assertGreaterEqual(ring.count("PciIo->AllocateBuffer"), 2)

    def test_successful_playback_map_ownership_is_independent_of_mapping_token_value(self):
        alloc = extract_c_function(self.source, "HdaAllocatePlaybackBuffers")
        release = extract_c_function(self.source, "HdaFreePlaybackBuffers")
        self.assertIn("BOOLEAN             AudioMapped;", self.header)
        self.assertIn("BOOLEAN             BdlMapped;", self.header)
        for flag in ("AudioMapped", "BdlMapped"):
            self.assertIn(f"Context->{flag} = TRUE;", alloc)
            self.assertIn(f"if (Context->{flag})", release)
            self.assertIn(f"Context->{flag} = FALSE;", release)
        self.assertNotIn("BdlMapping != NULL", release)
        self.assertNotIn("AudioMapping != NULL", release)

    def test_exit_boot_services_does_not_replay_codec_or_pci_snapshot(self):
        cleanup = extract_c_function(self.source, "HdaCleanupAsyncPlayback")
        terminal_start = cleanup.index("if (ExitBootServices) {", cleanup.index("if (!EFI_ERROR(StopStatus))"))
        terminal_end = cleanup.index("} else if (Context->PciDmaContainment)", terminal_start)
        terminal = cleanup[terminal_start:terminal_end]
        self.assertIn("Context->HaveOriginalPciAttributes = FALSE;", terminal)
        self.assertNotIn("HdaRestorePciAttributes", terminal)
        self.assertNotIn("HdaRestoreCodecStateRetry", terminal)

    def test_exit_boot_services_stream_stop_returns_before_descriptor_restore(self):
        stop = extract_c_function(self.source, "HdaStopOutputStream")
        terminal = stop.index("if (mHdaExitBootServicesInProgress)")
        restore = stop.index("HdaRestoreStreamDescriptorState(Context)", terminal)
        self.assertLess(terminal, restore)
        self.assertIn("Context->StreamOffset = 0;", stop[terminal:restore])
        self.assertIn("return EFI_SUCCESS;", stop[terminal:restore])

    def test_exit_boot_services_ring_cleanup_does_not_replay_register_snapshot(self):
        cleanup = extract_c_function(self.source, "HdaCleanupCorbRirbState")
        terminal = cleanup.index("if (RingsStopped && ExitBootServices)")
        normal = cleanup.index("} else if (RingsStopped)", terminal)
        terminal_region = cleanup[terminal:normal]
        self.assertIn("State->RegistersTouched = FALSE;", terminal_region)
        self.assertNotIn("HDA_REG_CORBLBASE", terminal_region)
        self.assertNotIn("HdaResetCorbReadPointer", terminal_region)

    def test_r16_documentation_is_authoritative_and_marks_superseded_rules(self):
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.61 final audit R16", readme)
        self.assertIn("1.0.59 / R14", readme)
        self.assertIn("supersede", readme.lower())
        self.assertIn("1.0.61 — final audit R16", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))
        self.assertIn("UEFI 2.11", report)
        self.assertIn("EfiPciIoOperationBusMasterRead", report)
        self.assertIn("BEFORE_EXIT_BOOT_SERVICES handoff", report)

    def test_r16_version_is_consistent_in_all_active_metadata(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        release_ini = (ROOT / "HDAUChimeRelease.ini").read_text(encoding="utf-8")
        debug_ini = (ROOT / "HDAUChimeDebug.ini").read_text(encoding="utf-8")
        helper = (ROOT / "Enable-HDAUCodecDump-NextBoot.command").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertIn("1.0.87-final-audit", release_ini)
        self.assertIn("1.0.87-final-audit", debug_ini)
        self.assertIn("1.0.87-final-audit", helper)



class Uefi211ConformanceRound16Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        cls.inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")

    def test_terminal_handoff_uses_before_exit_boot_services_group(self):
        fn = extract_c_function(self.source, "HdaInstallExitBootServicesCleanup")
        self.assertIn("gBS->CreateEventEx", fn)
        self.assertIn("EVT_NOTIFY_SIGNAL", fn)
        self.assertIn("TPL_CALLBACK", fn)
        self.assertIn("&gEfiEventBeforeExitBootServicesGuid", fn)
        self.assertNotIn("EVT_SIGNAL_EXIT_BOOT_SERVICES", fn)
        self.assertIn("#include <Guid/EventGroup.h>", self.header)
        self.assertIn("gEfiEventBeforeExitBootServicesGuid", self.inf)

    def test_before_exit_handoff_remains_allocation_and_delayed_work_free(self):
        callback = extract_c_function(self.source, "HdaExitBootServicesCleanup")
        callback_code = re.sub(r"/\*.*?\*/|//[^\n]*", "", callback, flags=re.S)
        for forbidden in (
            "AllocatePool", "AllocateZeroPool", "AllocatePages", "FreePool",
            "FreePages", "Unmap(", "FreeBuffer(", "SetTimer(", "CloseEvent(",
            "UnregisterKeyNotify", "SetVariable(", "OpenVolume", "->Open("
        ):
            self.assertNotIn(forbidden, callback_code)
        self.assertIn("HdaCleanupAsyncPlayback(TRUE)", callback)
        self.assertIn("HdaCleanupRetainedCorbRirb(TRUE)", callback)

    def test_single_shot_fails_closed_when_no_guard_can_be_armed(self):
        guard = extract_c_function(self.source, "HdaAcquireBootGuard")
        self.assertIn("BOOLEAN    GuardArmed;", guard)
        self.assertIn("GuardArmed = FALSE;", guard)
        self.assertGreaterEqual(guard.count("GuardArmed = TRUE;"), 2)
        fail = guard.index("if (!GuardArmed)")
        self.assertIn("SingleShotGuardUnavailable", guard[fail:])
        self.assertIn("return FALSE;", guard[fail:])
        self.assertLess(fail, guard.rindex("return TRUE;"))

    def test_r16_metadata_is_exact_and_prior_round_history_is_retained(self):
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.61 final audit R16", readme)
        self.assertIn("1.0.60 final audit R15", readme)
        self.assertIn("1.0.61 — final audit R16", changelog)
        self.assertIn("1.0.60 — final audit R15", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class Uefi211ConformanceRound17Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_existing_directory_open_uses_zero_attributes_without_create(self):
        fn = extract_c_function(self.source, "HdaOpenOrCreateDir")
        first = fn.index("Parent->Open")
        second = fn.index("Parent->Open", first + 1)
        existing = fn[first:second]
        creating = fn[second:]
        self.assertIn("EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,\n                     0", existing)
        self.assertNotIn("EFI_FILE_DIRECTORY", existing)
        self.assertIn("EFI_FILE_MODE_CREATE", creating)
        self.assertIn("EFI_FILE_DIRECTORY", creating)

    def test_key_dump_worker_is_deferred_to_next_timer_tick(self):
        install = extract_c_function(self.source, "HdaInstallBootScreenDNotify")
        key = extract_c_function(self.source, "HdaF6KeyNotify")
        self.assertIn("EVT_TIMER | EVT_NOTIFY_SIGNAL", install)
        self.assertIn("TPL_CALLBACK", install)
        self.assertIn("HdaArmDeferredCodecDumpTimer(0)", key)
        self.assertNotIn("gBS->SignalEvent(mHdaDumpDeferredEvent)", key)

    def test_successful_deferred_dump_releases_timer_event_in_worker(self):
        worker = extract_c_function(self.source, "HdaDeferredCodecDumpNotify")
        success = worker.index("mHdaF6DumpDone = TRUE;")
        close = worker.index("HdaCloseEventTerminal(&mHdaDumpDeferredEvent)", success)
        self.assertGreater(close, success)
        self.assertIn("mHdaDumpDeferredEvent == Event", worker[success:close])
        failure = worker.index("D codec dump failed and remains retryable")
        self.assertLess(failure, success)
        self.assertNotIn("HdaCloseEventTerminal", worker[failure:success])

    def test_r17_metadata_and_history_are_consistent(self):
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.62 final audit R17", readme)
        self.assertIn("1.0.61 final audit R16", readme)
        self.assertIn("1.0.62 — final audit R17", changelog)
        self.assertIn("1.0.61 — final audit R16", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))
        self.assertIn("R16 / 1.0.61", report)


class Uefi211ConformanceRound18Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_pci_attribute_rollback_never_replaces_the_entire_mask(self):
        helper = extract_c_function(self.source, "HdaApplyPciAttributeBits")
        restore = extract_c_function(self.source, "HdaRestorePciAttributes")
        stop = extract_c_function(self.source, "HdaStopOutputStream")
        self.assertNotIn("EfiPciIoAttributeOperationSet", self.source)
        self.assertIn("EfiPciIoAttributeOperationDisable", helper)
        self.assertIn("EfiPciIoAttributeOperationEnable", helper)
        self.assertIn("~Context->OriginalPciAttributes", restore)
        self.assertIn("ManagedMask |= EFI_PCI_IO_ATTRIBUTE_BUS_MASTER", restore)
        self.assertIn("EfiPciIoAttributeOperationDisable", stop)
        self.assertIn("HdaVerifyPciAttributeBits", stop)

    def test_corb_fallback_owns_temporary_bus_master_enable(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        cleanup = extract_c_function(self.source, "HdaCleanupCorbRirbState")
        pending = extract_c_function(self.source, "HdaCorbRirbStatePending")
        idle = command.index("OriginalCorbCtl")
        reset_pos = command.index("OriginalRirbWp", idle)
        attr_get = command.index("HdaGetPciAttributesExact", reset_pos)
        enable = command.index("EfiPciIoAttributeOperationEnable", attr_get)
        allocate = command.index("AllocateBuffer", enable)
        self.assertLess(idle, attr_get)
        self.assertLess(reset_pos, attr_get)
        self.assertLess(attr_get, enable)
        self.assertLess(enable, allocate)
        self.assertIn("RingState.BusMasterEnabledByFallback = TRUE;", command)
        self.assertIn("BusMasterEnabledByFallback", pending)
        self.assertIn("if (!State->RegistersTouched && State->BusMasterEnabledByFallback)", cleanup)
        restore_disable = cleanup.index("EfiPciIoAttributeOperationDisable", cleanup.index("BusMasterEnabledByFallback"))
        restore_verify = cleanup.index("HdaVerifyPciAttributeBits", restore_disable)
        self.assertLess(restore_disable, restore_verify)

    def test_failed_fallback_bus_master_enable_still_enters_cleanup(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        enable = command.index("EfiPciIoAttributeOperationEnable")
        fail = command.index("if (Status != EFI_SUCCESS)", enable)
        done = command.index("goto Done;", fail)
        region = command[fail:done + len("goto Done;")]
        self.assertIn("RingState.BusMasterEnabledByFallback = TRUE;", region)
        self.assertIn("Status = EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;", region)

    def test_playback_bus_master_is_delayed_until_final_pre_run_boundary(self):
        attempt = extract_c_function(self.source, "HdaTryController")
        play = extract_c_function(self.source, "HdaPlayChime")
        initial_enable = attempt.index("EfiPciIoAttributeOperationEnable")
        initial_verify = attempt.index("HdaVerifyPciAttributeBits", initial_enable)
        initial_region = attempt[initial_enable:initial_verify]
        self.assertNotIn("EFI_PCI_IO_ATTRIBUTE_BUS_MASTER", initial_region)
        prepare = play.index("HdaPrepareOutputStream")
        bme_enable = play.index("EfiPciIoAttributeOperationEnable", prepare)
        bme_verify = play.index("HdaVerifyPciAttributeBits", bme_enable)
        run = play.index("HdaStartOutputStream", bme_verify)
        self.assertIn("EFI_PCI_IO_ATTRIBUTE_BUS_MASTER", play[bme_enable:bme_verify])
        self.assertLess(prepare, bme_enable)
        self.assertLess(bme_enable, bme_verify)
        self.assertLess(bme_verify, run)

    def test_codec_dump_does_not_pre_enable_bus_master(self):
        dump = extract_c_function(self.source, "HdaDumpControllerFull")
        enable = dump.index("EfiPciIoAttributeOperationEnable")
        verify = dump.index("HdaVerifyPciAttributeBits", enable)
        region = dump[enable:verify]
        self.assertIn("EFI_PCI_IO_ATTRIBUTE_MEMORY", region)
        self.assertNotIn("EFI_PCI_IO_ATTRIBUTE_BUS_MASTER", region)
        self.assertIn("HdaApplyPciAttributeBits", dump)

    def test_r18_metadata_and_history_are_consistent(self):
        header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.62 final audit R17", readme)
        self.assertIn("1.0.63 — final audit R18", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class Uefi211ConformanceRound19Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_external_pci_restore_can_discharge_fallback_bus_master_ownership(self):
        helper = extract_c_function(self.source, "HdaDischargeRetainedCorbRirbBusMasterRestore")
        restore = extract_c_function(self.source, "HdaRestorePciAttributes")
        self.assertIn("mHdaRetainedCorbRirbState.BusMasterEnabledByFallback = FALSE;", helper)
        self.assertIn("ManagedMask & EFI_PCI_IO_ATTRIBUTE_BUS_MASTER", helper)
        self.assertIn("DesiredAttributes & EFI_PCI_IO_ATTRIBUTE_BUS_MASTER", helper)
        self.assertIn("HdaCorbRirbStatePending", helper)
        self.assertIn("mHdaRetainedCorbRirbValid = FALSE;", helper)
        apply = restore.index("HdaApplyPciAttributeBits")
        discharge = restore.index("HdaDischargeRetainedCorbRirbBusMasterRestore", apply)
        clear = restore.index("Context->HaveOriginalPciAttributes = FALSE;", discharge)
        self.assertLess(apply, discharge)
        self.assertLess(discharge, clear)

    def test_codec_dump_restore_manages_fallback_owned_bus_master_bit(self):
        dump = extract_c_function(self.source, "HdaDumpControllerFull")
        mask = dump.index("RestoreMask |= EFI_PCI_IO_ATTRIBUTE_BUS_MASTER")
        guard = dump.rfind("BusMasterEnabledByFallback", 0, mask)
        apply = dump.index("HdaApplyPciAttributeBits", mask)
        discharge = dump.index("HdaDischargeRetainedCorbRirbBusMasterRestore", apply)
        self.assertGreaterEqual(guard, 0)
        self.assertLess(guard, mask)
        self.assertLess(mask, apply)
        self.assertLess(apply, discharge)

    def test_bus_master_enable_is_refused_while_any_hda_stream_is_running(self):
        guard = extract_c_function(self.source, "HdaRequireNoRunningStreamsBeforeBusMasterEnable")
        self.assertIn("HDA_REG_GCAP", guard)
        self.assertIn("HDA_REG_SD_BASE", guard)
        self.assertIn("HDA_SD_CTL_RUN", guard)
        self.assertIn("return EFI_ACCESS_DENIED;", guard)
        self.assertIn("HdaMemRead8", guard)

    def test_playback_and_corb_check_stream_quiescence_before_enabling_bus_master(self):
        command = extract_c_function(self.source, "HdaCorbRirbCommand")
        play = extract_c_function(self.source, "HdaPlayChime")
        corb_guard = command.index("HdaRequireNoRunningStreamsBeforeBusMasterEnable")
        corb_enable = command.index("EfiPciIoAttributeOperationEnable", corb_guard)
        play_guard = play.index("HdaRequireNoRunningStreamsBeforeBusMasterEnable")
        play_enable = play.index("EfiPciIoAttributeOperationEnable", play_guard)
        self.assertLess(corb_guard, corb_enable)
        self.assertLess(play_guard, play_enable)
        self.assertIn("PciBusMasterForeignDmaActive", play[play_guard:play_enable])

    def test_r19_metadata_and_all_prior_round_history_are_retained(self):
        header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.63 final audit R18", readme)
        self.assertIn("1.0.64 — final audit R19", changelog)
        self.assertIn("1.0.63 — final audit R18", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))



class Uefi211ConformanceRound20Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_fastboot_callback_cannot_preempt_entry_owned_log_state(self):
        tick = extract_c_function(self.source, "HdaAsyncPlaybackTick")
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        gate = tick.index("if (mHdaEntryWorkInProgress) return;")
        first_stream_read = tick.index("HdaMemRead32", gate)
        self.assertLess(gate, first_stream_read)
        set_busy = entry.index("mHdaEntryWorkInProgress = TRUE;")
        install = entry.index("HdaInstallBootScreenDNotify();", set_busy)
        finish = entry.index("HdaDebugFinish();", install)
        clear_busy = entry.index("mHdaEntryWorkInProgress = FALSE;", finish)
        self.assertLess(set_busy, install)
        self.assertLess(install, finish)
        self.assertLess(finish, clear_busy)

    def test_key_request_stays_synchronous_while_entry_is_active(self):
        key = extract_c_function(self.source, "HdaF6KeyNotify")
        pending = key.index("mHdaDumpDeferredPending = TRUE;")
        entry_gate = key.index("if (mHdaEntryWorkInProgress) goto Done;", pending)
        arm = key.index("HdaArmDeferredCodecDumpTimer(0)", entry_gate)
        self.assertLess(pending, entry_gate)
        self.assertLess(entry_gate, arm)
        entry = extract_c_function(self.source, "HdaUniversalBootChimeDxeEntry")
        service = entry.index("HdaServiceDeferredCodecDumpIfSafe();")
        finish = entry.index("HdaDebugFinish();", service)
        clear_busy = entry.index("mHdaEntryWorkInProgress = FALSE;", finish)
        final_arm = entry.index("HdaArmDeferredCodecDumpTimer(0)", clear_busy)
        self.assertLess(service, finish)
        self.assertLess(finish, clear_busy)
        self.assertLess(clear_busy, final_arm)

    def test_key_callback_reentrancy_rearms_timer_until_callback_unwinds(self):
        key = extract_c_function(self.source, "HdaF6KeyNotify")
        worker = extract_c_function(self.source, "HdaDeferredCodecDumpNotify")
        set_active = key.index("mHdaKeyNotifyDepth++;")
        pending_gate = key.index("mHdaDumpDeferredPending", set_active)
        arm = key.index("HdaArmDeferredCodecDumpTimer(0)", pending_gate)
        clear_active = key.index("mHdaKeyNotifyDepth--;", arm)
        self.assertLess(set_active, pending_gate)
        self.assertLess(pending_gate, arm)
        self.assertLess(arm, clear_active)
        gate = worker.index("if (mHdaKeyNotifyDepth != 0)")
        rearm = worker.index("HdaArmDeferredCodecDumpTimer(HDAU_DEFERRED_DUMP_RETRY_100NS)", gate)
        heavy = worker.index("HdaCleanupAsyncPlayback(FALSE)", rearm)
        self.assertLess(gate, rearm)
        self.assertLess(rearm, heavy)

    def test_r20_metadata_and_history_are_consistent(self):
        header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.64 final audit R19", readme)
        self.assertIn("1.0.65 — final audit R20", changelog)
        self.assertIn("1.0.64 — final audit R19", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class Uefi211ConformanceRound21Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_unsupported_common_ring_size_runs_cleanup_after_fallback_bme_enable(self):
        body = extract_c_function(self.source, "HdaCorbRirbCommand")
        bme = body.index("RingState.BusMasterEnabledByFallback = TRUE;")
        unsupported = body.index("Status = EFI_UNSUPPORTED;", bme)
        done = body.index("Done:", unsupported)
        self.assertIn("goto Done;", body[unsupported:done])
        self.assertNotIn("return EFI_UNSUPPORTED;", body[bme:done])

    def test_bus_master_containment_failure_has_error_priority(self):
        body = extract_c_function(self.source, "HdaStopOutputStream")
        marker = 'HdaLog("[HDAU] PCI bus-master containment also failed: %r", ContainStatus);'
        pos = body.index(marker)
        tail = body[pos:pos + 500]
        self.assertIn("return ContainStatus;", tail)
        self.assertNotIn("return EFI_ERROR(Status) ? Status : ContainStatus;", tail)

    def test_r21_metadata_and_history_are_consistent(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text()
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text()
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text()
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text()
        changelog = (ROOT / "CHANGELOG.md").read_text()
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text()
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.66 — final audit R21", changelog)
        self.assertIn("1.0.65 — final audit R20", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class Uefi211ConformanceRound22Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_key_notify_exclusion_is_nesting_safe(self):
        key = extract_c_function(self.source, "HdaF6KeyNotify")
        worker = extract_c_function(self.source, "HdaDeferredCodecDumpNotify")
        self.assertIn("STATIC UINTN mHdaKeyNotifyDepth = 0;", self.source)
        self.assertNotIn("mHdaKeyNotifyInProgress", self.source)
        overflow = key.index("if (mHdaKeyNotifyDepth == MAX_UINTN) return EFI_OUT_OF_RESOURCES;")
        enter = key.index("mHdaKeyNotifyDepth++;", overflow)
        done = key.index("Done:", enter)
        leave = key.index("mHdaKeyNotifyDepth--;", done)
        self.assertLess(overflow, enter)
        self.assertLess(enter, done)
        self.assertLess(done, leave)
        self.assertIn("if (mHdaKeyNotifyDepth != 0)", worker)

    def test_r22_metadata_and_history_are_consistent(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text()
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text()
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text()
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text()
        changelog = (ROOT / "CHANGELOG.md").read_text()
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text()
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.67 — final audit R22", changelog)
        self.assertIn("1.0.66 — final audit R21", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class Uefi211ConformanceRound23Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_success_without_key_notify_handle_is_permanently_contained(self):
        install = extract_c_function(self.source, "HdaInstallBootScreenDNotify")
        key = extract_c_function(self.source, "HdaF6KeyNotify")
        self.assertIn("STATIC BOOLEAN mHdaBootScreenUnknownRegistration = FALSE;", self.source)
        self.assertIn("mHdaBootScreenUnknownRegistration ||", key)
        self.assertIn("mHdaBootScreenNotifyQuarantined ||", install)
        self.assertIn("mHdaBootScreenUnknownRegistration ||", install)
        self.assertIn("mHdaBootScreenNotifyMutationInProgress", install)
        lower = install.index("D lower-case registration violated UEFI success contract; unknown callback contained")
        lower_gate = install.rfind("mHdaBootScreenUnknownRegistration = TRUE;", 0, lower)
        lower_close = install.index("HdaUninstallBootScreenDNotify(TRUE);", lower)
        lower_return = install.index("return;", lower_close)
        self.assertLess(lower_gate, lower)
        self.assertLess(lower, lower_close)
        self.assertLess(lower_close, lower_return)

    def test_upper_unknown_registration_is_gated_before_known_lower_cleanup(self):
        install = extract_c_function(self.source, "HdaInstallBootScreenDNotify")
        upper = install.index("D upper-case registration violated UEFI success contract; unknown callback contained")
        gate = install.rfind("mHdaBootScreenUnknownRegistration = TRUE;", 0, upper)
        cleanup = install.index("HdaUninstallBootScreenDNotify(TRUE);", upper)
        self.assertLess(gate, upper)
        self.assertLess(upper, cleanup)
        # The lower-case exact token is published before the upper-case attempt,
        # so common cleanup can still unregister that known registration.
        lower_publish = install.index("mHdaF6NotifyRegistration = Registration;")
        upper_register = install.index("HDAU_DUMP_KEY_UPPER", lower_publish)
        self.assertLess(lower_publish, upper_register)
        self.assertLess(upper_register, gate)

    def test_registration_in_progress_blocks_reentrant_callback_and_worker(self):
        install = extract_c_function(self.source, "HdaInstallBootScreenDNotify")
        key = extract_c_function(self.source, "HdaF6KeyNotify")
        worker = extract_c_function(self.source, "HdaDeferredCodecDumpNotify")
        self.assertIn("STATIC BOOLEAN mHdaBootScreenNotifyMutationInProgress = FALSE;", self.source)
        key_gate = key.index("mHdaBootScreenNotifyMutationInProgress ||")
        key_pending = key.index("mHdaDumpDeferredPending = TRUE;")
        self.assertLess(key_gate, key_pending)

        calls = [m.start() for m in re.finditer(r"mHdaF6TextInEx->RegisterKeyNotify\(", install)]
        self.assertEqual(len(calls), 2)
        for call in calls:
            set_true = install.rfind("mHdaBootScreenNotifyMutationInProgress = TRUE;", 0, call)
            set_false = install.find("mHdaBootScreenNotifyMutationInProgress = FALSE;", call)
            self.assertGreaterEqual(set_true, 0)
            self.assertGreaterEqual(set_false, 0)
            self.assertLess(set_true, call)
            self.assertLess(call, set_false)

        reg_gate = worker.index("if (mHdaBootScreenNotifyMutationInProgress)")
        rearm = worker.index("HdaArmDeferredCodecDumpTimer", reg_gate)
        depth = worker.index("if (mHdaKeyNotifyDepth != 0)")
        self.assertLess(reg_gate, rearm)
        self.assertLess(rearm, depth)

    def test_unknown_registration_discards_any_reentrant_pending_request(self):
        install = extract_c_function(self.source, "HdaInstallBootScreenDNotify")
        for marker in (
            "D lower-case registration violated UEFI success contract; unknown callback contained",
            "D upper-case registration violated UEFI success contract; unknown callback contained",
        ):
            log = install.index(marker)
            gate = install.rfind("mHdaBootScreenUnknownRegistration = TRUE;", 0, log)
            pending = install.find("mHdaDumpDeferredPending = FALSE;", gate, log)
            self.assertGreaterEqual(pending, 0)
            self.assertLess(gate, pending)
            self.assertLess(pending, log)

    def test_deferred_worker_honors_terminal_notify_containment_before_rearm(self):
        worker = extract_c_function(self.source, "HdaDeferredCodecDumpNotify")
        quarantine = worker.index("mHdaBootScreenNotifyQuarantined ||")
        unknown = worker.index("mHdaBootScreenUnknownRegistration ||", quarantine)
        clear = worker.index("mHdaDumpDeferredPending = FALSE;", unknown)
        depth = worker.index("if (mHdaKeyNotifyDepth != 0)")
        heavy = worker.index("HdaCleanupAsyncPlayback(FALSE)")
        self.assertLess(quarantine, unknown)
        self.assertLess(unknown, clear)
        self.assertLess(clear, depth)
        self.assertLess(depth, heavy)

    def test_codec_dump_helper_enables_release_debug_override_transactionally(self):
        helper = ROOT / "Enable-HDAUCodecDump-NextBoot.command"
        with tempfile.TemporaryDirectory() as tmp:
            efi = Path(tmp) / "EFI_VOLUME"
            driver = efi / "EFI" / "OC" / "Drivers" / "HDAUniversalBootChimeDxe.efi"
            cfg_dir = efi / "EFI" / "HDAUniversalBootChimeDxe"
            driver.parent.mkdir(parents=True)
            cfg_dir.mkdir(parents=True)
            driver.write_bytes(b"test-driver")

            release = cfg_dir / "HDAUChimeRelease.ini"
            debug = cfg_dir / "HDAUChimeDebug.ini"
            release_original = "Debug=0\nReadDebugConfig=0\nReadDebugConfig=0\n"
            debug_original = "Debug=0\nFileLog=0\nCodecDumpAlways=0\n"
            release.write_text(release_original, encoding="utf-8")
            debug.write_text(debug_original, encoding="utf-8")

            env = os.environ.copy()
            env["EFI_VOL"] = str(efi)
            completed = subprocess.run(
                ["/bin/bash", str(helper)],
                check=True,
                text=True,
                capture_output=True,
                env=env,
            )
            self.assertIn("Done. EFI selected:", completed.stdout)

            release_after = release.read_text(encoding="utf-8")
            debug_after = debug.read_text(encoding="utf-8")
            self.assertEqual(release_after.count("ReadDebugConfig=1"), 1)
            self.assertNotIn("ReadDebugConfig=0", release_after)
            for setting in (
                "Debug=1",
                "FileLog=1",
                "NvramStatus=1",
                "ReadConfig=1",
                "ReadDebugConfig=1",
                "CodecDumpAlways=1",
                "DumpCodec=1",
                "DPollMs=0",
            ):
                self.assertIn(setting, debug_after)

            self.assertEqual(
                (cfg_dir / "HDAUChimeRelease.ini.backup").read_text(encoding="utf-8"),
                release_original,
            )
            self.assertEqual(
                (cfg_dir / "HDAUChimeDebug.ini.backup").read_text(encoding="utf-8"),
                debug_original,
            )
            self.assertFalse(list(cfg_dir.glob("*.tmp.*")))
            self.assertFalse(list(cfg_dir.glob("*.stage.*")))

    def test_codec_dump_helper_preserves_first_backups_across_repeated_runs(self):
        helper = ROOT / "Enable-HDAUCodecDump-NextBoot.command"
        with tempfile.TemporaryDirectory() as tmp:
            efi = Path(tmp) / "EFI_VOLUME"
            driver = efi / "EFI" / "CLOVER" / "drivers" / "UEFI" / "HDAUniversalBootChimeDxe.efi"
            cfg_dir = efi / "EFI" / "HDAUniversalBootChimeDxe"
            driver.parent.mkdir(parents=True)
            cfg_dir.mkdir(parents=True)
            driver.write_bytes(b"test-driver")
            release = cfg_dir / "HDAUChimeRelease.ini"
            debug = cfg_dir / "HDAUChimeDebug.ini"
            release.write_text("ReadDebugConfig=0\nDebug=0\n", encoding="utf-8")
            debug.write_text("Debug=0\nFileLog=0\n", encoding="utf-8")

            env = os.environ.copy()
            env["EFI_VOL"] = str(efi)
            for _ in range(2):
                subprocess.run(
                    ["/bin/bash", str(helper)],
                    check=True,
                    text=True,
                    capture_output=True,
                    env=env,
                )

            self.assertEqual(
                (cfg_dir / "HDAUChimeRelease.ini.backup").read_text(encoding="utf-8"),
                "ReadDebugConfig=0\nDebug=0\n",
            )
            self.assertEqual(
                (cfg_dir / "HDAUChimeDebug.ini.backup").read_text(encoding="utf-8"),
                "Debug=0\nFileLog=0\n",
            )
            self.assertEqual(release.read_text(encoding="utf-8").count("ReadDebugConfig=1"), 1)
            self.assertFalse(list(cfg_dir.glob("*.tmp.*")))
            self.assertFalse(list(cfg_dir.glob("*.stage.*")))

    def test_codec_dump_helper_failure_never_publishes_partial_debug_profile(self):
        helper = ROOT / "Enable-HDAUCodecDump-NextBoot.command"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            efi = root / "EFI_VOLUME"
            driver = efi / "EFI" / "OC" / "Drivers" / "HDAUniversalBootChimeDxe.efi"
            cfg_dir = efi / "EFI" / "HDAUniversalBootChimeDxe"
            driver.parent.mkdir(parents=True)
            cfg_dir.mkdir(parents=True)
            driver.write_bytes(b"test-driver")
            release = cfg_dir / "HDAUChimeRelease.ini"
            debug = cfg_dir / "HDAUChimeDebug.ini"
            release_original = "ReadDebugConfig=1\nDebug=0\n"
            debug_original = "Debug=0\nFileLog=0\nCodecDumpAlways=0\n"
            release.write_text(release_original, encoding="utf-8")
            debug.write_text(debug_original, encoding="utf-8")

            fakebin = root / "bin"
            fakebin.mkdir()
            counter = root / "awk-count"
            fake_awk = fakebin / "awk"
            fake_awk.write_text(
                "#!/bin/bash\n"
                "n=0\n"
                "[ ! -f \"$AWK_COUNT_FILE\" ] || n=$(cat \"$AWK_COUNT_FILE\")\n"
                "n=$((n + 1))\n"
                "printf '%s' \"$n\" > \"$AWK_COUNT_FILE\"\n"
                "if [ \"$n\" -eq 5 ]; then exit 42; fi\n"
                "exec /usr/bin/awk \"$@\"\n",
                encoding="utf-8",
            )
            fake_awk.chmod(0o755)

            env = os.environ.copy()
            env["EFI_VOL"] = str(efi)
            env["AWK_COUNT_FILE"] = str(counter)
            env["PATH"] = str(fakebin) + os.pathsep + env.get("PATH", "")
            completed = subprocess.run(
                ["/bin/bash", str(helper)],
                check=False,
                text=True,
                capture_output=True,
                env=env,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(debug.read_text(encoding="utf-8"), debug_original)
            self.assertEqual(release.read_text(encoding="utf-8"), release_original)
            self.assertFalse(list(cfg_dir.glob("*.tmp.*")))
            self.assertFalse(list(cfg_dir.glob("*.stage.*")))

    def test_codec_dump_helper_does_not_invent_release_profile_when_missing(self):
        helper = ROOT / "Enable-HDAUCodecDump-NextBoot.command"
        with tempfile.TemporaryDirectory() as tmp:
            efi = Path(tmp) / "EFI_VOLUME"
            driver = efi / "EFI" / "OC" / "Drivers" / "HDAUniversalBootChimeDxe.efi"
            driver.parent.mkdir(parents=True)
            driver.write_bytes(b"test-driver")

            env = os.environ.copy()
            env["EFI_VOL"] = str(efi)
            subprocess.run(
                ["/bin/bash", str(helper)],
                check=True,
                text=True,
                capture_output=True,
                env=env,
            )

            cfg_dir = efi / "EFI" / "HDAUniversalBootChimeDxe"
            self.assertFalse((cfg_dir / "HDAUChimeRelease.ini").exists())
            debug = (cfg_dir / "HDAUChimeDebug.ini").read_text(encoding="utf-8")
            self.assertIn("ReadDebugConfig=1", debug)
            self.assertIn("CodecDumpAlways=1", debug)
            # With no release file, the compiled default remains the authority.
            init = self.source[self.source.index("STATIC HDAU_DEBUG_CONFIG mHdaCfg = {"):]
            self.assertIn("TRUE,                       /* ReadDebugConfig:", init[:5000])

    def test_r23_metadata_and_history_are_consistent(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text()
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text()
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text()
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text()
        changelog = (ROOT / "CHANGELOG.md").read_text()
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text()
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.68 — final audit R23", changelog)
        self.assertIn("1.0.67 — final audit R22", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class Uefi211ConformanceRound24Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_registration_barrier_is_released_only_after_ownership_decision(self):
        install = extract_c_function(self.source, "HdaInstallBootScreenDNotify")
        # There must be no unconditional barrier drop immediately after the
        # firmware call returns; ownership is still being classified there.
        self.assertNotRegex(
            install,
            r"RegisterKeyNotify\([\s\S]*?&Registration\s*\n\s*\);\s*\n\s*mHdaBootScreenNotifyMutationInProgress = FALSE;",
        )

        self.assertEqual(install.count("mHdaBootScreenNotifyMutationInProgress = TRUE;"), 1)
        lower_publish = install.index("mHdaF6NotifyRegistration = Registration;")
        upper_publish = install.index("mHdaDNotifyRegistrationUpper = Registration;")
        release = install.index("mHdaBootScreenNotifyMutationInProgress = FALSE;", upper_publish)
        final_log = install.index("D boot-screen notify registered", release)
        self.assertLess(lower_publish, upper_publish)
        self.assertLess(upper_publish, release)
        self.assertLess(release, final_log)

        for marker in (
            "D lower-case registration violated UEFI success contract; unknown callback contained",
            "D upper-case registration violated UEFI success contract; unknown callback contained",
        ):
            log = install.index(marker)
            gate = install.rfind("mHdaBootScreenUnknownRegistration = TRUE;", 0, log)
            cleanup = install.index("HdaUninstallBootScreenDNotify(TRUE);", log)
            self.assertLess(gate, log)
            self.assertLess(log, cleanup)

    def test_notify_ownership_mutation_barrier_covers_unregister_transaction(self):
        uninstall = extract_c_function(self.source, "HdaUninstallBootScreenDNotify")
        self.assertIn("STATIC BOOLEAN mHdaBootScreenNotifyMutationInProgress = FALSE;", self.source)
        enter = uninstall.index("mHdaBootScreenNotifyMutationInProgress = TRUE;")
        unregister_calls = [m.start() for m in re.finditer(r"->UnregisterKeyNotify\(", uninstall)]
        self.assertEqual(len(unregister_calls), 2)
        for call in unregister_calls:
            self.assertLess(enter, call)
        close_gate = uninstall.index("if (CloseDeferredEvent", unregister_calls[-1])
        close_call = uninstall.index("HdaCloseEventTerminal", close_gate)
        leave = uninstall.index("mHdaBootScreenNotifyMutationInProgress = FALSE;", close_call)
        self.assertLess(unregister_calls[-1], close_gate)
        self.assertLess(close_gate, close_call)
        self.assertLess(close_call, leave)

    def test_notify_mutation_barrier_blocks_key_and_deferred_worker_side_effects(self):
        key = extract_c_function(self.source, "HdaF6KeyNotify")
        worker = extract_c_function(self.source, "HdaDeferredCodecDumpNotify")
        key_gate = key.index("mHdaBootScreenNotifyMutationInProgress ||")
        key_publish = key.index("mHdaDumpDeferredPending = TRUE;")
        self.assertLess(key_gate, key_publish)

        terminal = worker.index("mHdaBootScreenUnknownRegistration ||")
        mutation = worker.index("if (mHdaBootScreenNotifyMutationInProgress)", terminal)
        rearm = worker.index("HdaArmDeferredCodecDumpTimer", mutation)
        depth = worker.index("if (mHdaKeyNotifyDepth != 0)", rearm)
        heavy = worker.index("HdaCleanupAsyncPlayback(FALSE)", depth)
        self.assertLess(terminal, mutation)
        self.assertLess(mutation, rearm)
        self.assertLess(rearm, depth)
        self.assertLess(depth, heavy)

    def test_primary_compile_command_is_present_executable_and_transactional(self):
        command = ROOT / "Build-HDAUniversalBootChimeDxe.command"
        self.assertTrue(command.is_file())
        self.assertNotEqual(command.stat().st_mode & 0o111, 0)
        text = command.read_text(encoding="utf-8")
        self.assertTrue(text.startswith("#!/bin/bash"))
        self.assertIn('VERSION="1.0.87-final-audit"', text)
        self.assertIn('EDK2_REF="${EDK2_REF:-$EDK2_DEFAULT_REF}"', text)
        self.assertIn('SOURCE_TEST_ROUNDS="${SOURCE_TEST_ROUNDS:-5}"', text)
        self.assertIn("trap cleanup_build_transaction EXIT", text)
        self.assertIn("build_one DEBUG Release/Debug", text)
        self.assertIn("build_one RELEASE Release/Release", text)
        self.assertIn('unzip -tq "$ZIP"', text)

    def test_r24_metadata_and_history_are_consistent(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text()
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text()
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text()
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text()
        changelog = (ROOT / "CHANGELOG.md").read_text()
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text()
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.69 — final audit R24", changelog)
        self.assertIn("1.0.68 — final audit R23", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class Uefi211ConformanceRound25Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_undocumented_register_key_notify_status_is_terminally_contained(self):
        install = extract_c_function(self.source, "HdaInstallBootScreenDNotify")
        self.assertEqual(
            install.count("if (Status != EFI_OUT_OF_RESOURCES)"),
            2,
            "UEFI 2.11 documents EFI_OUT_OF_RESOURCES as the only RegisterKeyNotify failure",
        )
        self.assertEqual(
            install.count("registration returned undocumented status; unknown callback contained"),
            2,
        )
        for marker in (
            "D lower-case registration returned undocumented status; unknown callback contained",
            "D upper-case registration returned undocumented status; unknown callback contained",
        ):
            log = install.index(marker)
            gate = install.rfind("mHdaBootScreenUnknownRegistration = TRUE;", 0, log)
            pending = install.rfind("mHdaDumpDeferredPending = FALSE;", 0, log)
            cleanup = install.index("HdaUninstallBootScreenDNotify(TRUE);", log)
            ret = install.index("return;", cleanup)
            self.assertGreaterEqual(gate, 0)
            self.assertGreaterEqual(pending, 0)
            self.assertLess(gate, log)
            self.assertLess(pending, log)
            self.assertLess(log, cleanup)
            self.assertLess(cleanup, ret)

        # A documented resource failure must remain retry-safe rather than
        # permanently disabling the feature for the rest of the boot.
        self.assertGreaterEqual(
            install.count("D boot-screen notify lower-case register failed: %r"), 1
        )
        self.assertGreaterEqual(
            install.count("D boot-screen notify upper-case register failed: %r"), 1
        )

    def test_default_edk2_toolchain_is_pinned_to_official_stable_commit(self):
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        setup = (ROOT / "Setup-EDK2.command").read_text(encoding="utf-8")
        expected = "2970e5699ba6267f3384ffab20f96647578aebc8"
        for text in (build, setup):
            self.assertIn('EDK2_DEFAULT_REF="edk2-stable202608"', text)
            self.assertIn('EDK2_REF="${EDK2_REF:-$EDK2_DEFAULT_REF}"', text)
            self.assertIn('EDK2_EXPECTED_COMMIT="${EDK2_EXPECTED_COMMIT:-}"', text)
            self.assertIn(expected, text)
            self.assertRegex(
                text,
                r'if \[ "\$EDK2_REF" = "\$EDK2_DEFAULT_REF" \] && '
                r'\[ -z "\$EDK2_EXPECTED_COMMIT" \]; then',
            )
            self.assertRegex(
                text,
                r'\[ -n "\$EDK2_EXPECTED_COMMIT" \].*\[ "\$head" != "\$EDK2_EXPECTED_COMMIT" \]',
            )

        bootstrap = build[build.index("bootstrap_edk2_if_needed()") : build.index("prepare_edk2()")]
        verify = bootstrap.index('verify_edk2_ref "$EDK2_DEFAULT_DIR"')
        submodule = bootstrap.index("submodule update --init --recursive --depth 1")
        self.assertLess(verify, submodule)

    def test_r25_metadata_and_history_are_consistent(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text()
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text()
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text()
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text()
        changelog = (ROOT / "CHANGELOG.md").read_text()
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text()
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.70 — final audit R25", changelog)
        self.assertIn("1.0.69 — final audit R24", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class Uefi211ConformanceRound26Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_pinned_edk2_workspace_requires_verifiable_git_provenance(self):
        body = self.build[
            self.build.index("verify_edk2_ref()") : self.build.index("prepare_edk2()")
        ]
        self.assertIn('if [ ! -d "$ws/.git" ]; then', body)
        pin = body.index('if [ -n "$EDK2_EXPECTED_COMMIT" ]; then')
        reject = body.index('cannot verify pinned EDK2 commit', pin)
        warning = body.index('custom EDK2_REF=$EDK2_REF cannot be provenance-verified', reject)
        allow = body.index('return 0', warning)
        git_head = body.index('head="$(git -C "$ws" rev-parse HEAD', allow)
        self.assertLess(pin, reject)
        self.assertLess(reject, warning)
        self.assertLess(warning, allow)
        self.assertLess(allow, git_head)

    def test_unpinned_custom_non_git_workspace_keeps_explicit_compatibility_path(self):
        body = self.build[
            self.build.index("verify_edk2_ref()") : self.build.index("prepare_edk2()")
        ]
        non_git = body[body.index('if [ ! -d "$ws/.git" ]; then'):]
        fail_block_end = non_git.index('fi', non_git.index('if [ -n "$EDK2_EXPECTED_COMMIT" ]; then'))
        warning = non_git.index('warn "WORKSPACE is not a git checkout; custom EDK2_REF=')
        allow = non_git.index('return 0', warning)
        self.assertLess(fail_block_end, warning)
        self.assertLess(warning, allow)

    def test_r26_metadata_and_history_are_consistent(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text()
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text()
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text()
        changelog = (ROOT / "CHANGELOG.md").read_text()
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text()
        release_ini = (ROOT / "HDAUChimeRelease.ini").read_text()
        debug_ini = (ROOT / "HDAUChimeDebug.ini").read_text()
        helper = (ROOT / "Enable-HDAUCodecDump-NextBoot.command").read_text()
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', self.build)
        self.assertIn("1.0.87-final-audit", release_ini)
        self.assertIn("1.0.87-final-audit", debug_ini)
        self.assertIn("1.0.87-final-audit", helper)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.71 — final audit R26", changelog)
        self.assertIn("1.0.70 — final audit R25", changelog)
        self.assertIn("1.0.69 — final audit R24", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class HdaSpecConformanceRound27Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_controller_reset_requires_idle_rings_and_streams_before_crst_assert(self):
        helper = extract_c_function(self.source, "HdaRequireControllerResetQuiescent")
        reset = extract_c_function(self.source, "HdaControllerReset")
        for token in (
            "HDA_REG_CORBCTL",
            "HDA_REG_RIRBCTL",
            "HDA_CORBCTL_DMA_ENABLE",
            "HDA_RIRBCTL_DMA_ENABLE",
            "HDA_REG_GCAP",
            "HDA_SD_CTL_RUN",
            "EFI_ACCESS_DENIED",
        ):
            self.assertIn(token, helper)
        gate = reset.index("HdaRequireControllerResetQuiescent(PciIo)")
        assert_crst = reset.index("Gctl &= ~(UINT32)HDA_GCTL_CRST;")
        write_crst = reset.index("HdaMemWrite32(PciIo, HDA_REG_GCTL, Gctl)", assert_crst)
        self.assertLess(gate, assert_crst)
        self.assertLess(assert_crst, write_crst)

    def test_full_controller_reset_waits_521us_after_observing_crst_one(self):
        reset = extract_c_function(self.source, "HdaControllerReset")
        self.assertIn("#define HDAU_CODEC_POST_RESET_MIN_US  521", self.header)
        release_loop = reset.index("HDAU_RESET_RELEASE_TIMEOUT_US")
        observed = reset.index("(Gctl & HDA_GCTL_CRST) != 0", release_loop)
        settle = reset.index("HdaStall(HDAU_CODEC_POST_RESET_MIN_US);", observed)
        success = reset.index("return EFI_SUCCESS;", settle)
        self.assertLess(release_loop, observed)
        self.assertLess(observed, settle)
        self.assertLess(settle, success)

    def test_immediate_command_timeout_aborts_owned_icb_and_clears_racing_irv(self):
        helper = extract_c_function(self.source, "HdaAbortImmediateCommand")
        raw = extract_c_function(self.source, "HdaImmediateCommandRaw")
        self.assertIn("HdaMemWrite16(PciIo, HDA_REG_ICIS, 0)", helper)
        self.assertIn("HDA_ICIS_ICB", helper)
        self.assertIn("HDA_ICIS_IRV", helper)
        self.assertIn("HdaMemWrite16(PciIo, HDA_REG_ICIS, HDA_ICIS_IRV)", helper)
        response_timeout = raw.index("HDAU_IMMEDIATE_RESPONSE_TIMEOUT_US")
        abort = raw.index("HdaAbortImmediateCommand(PciIo)")
        timeout = raw.index("return EFI_TIMEOUT;", abort)
        self.assertEqual(raw.count("HdaAbortImmediateCommand(PciIo)"), 2)
        self.assertLess(response_timeout, abort)
        self.assertLess(abort, timeout)

    def test_low_latency_already_running_controller_obeys_codec_discovery_settle(self):
        ready = extract_c_function(self.source, "HdaControllerEnsureReady")
        gctl_read = ready.index("HdaMemRead32(PciIo, HDA_REG_GCTL, &Gctl)")
        running = ready.index("if ((Gctl & HDA_GCTL_CRST) != 0)", gctl_read)
        settle = ready.index("HdaStall(HDAU_CODEC_POST_RESET_MIN_US);", running)
        state_read = ready.index("HdaMemRead16(PciIo, HDA_REG_STATESTS, &StateSts)", settle)
        success = ready.index("if (StateSts != 0) return EFI_SUCCESS;", state_read)
        self.assertLess(gctl_read, running)
        self.assertLess(running, settle)
        self.assertLess(settle, state_read)
        self.assertLess(state_read, success)

    def test_immediate_and_corb_transports_are_mutually_exclusive(self):
        raw = extract_c_function(self.source, "HdaImmediateCommandRaw")
        abort = extract_c_function(self.source, "HdaAbortImmediateCommand")
        corb = extract_c_function(self.source, "HdaCorbRirbCommand")
        for body in (raw, abort):
            self.assertIn("HDA_REG_CORBCTL", body)
            self.assertIn("HDA_REG_RIRBCTL", body)
            self.assertIn("HDA_CORBCTL_DMA_ENABLE", body)
            self.assertIn("HDA_RIRBCTL_DMA_ENABLE", body)
            self.assertIn("return EFI_ACCESS_DENIED;", body)
        icis_read = corb.index("HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis)")
        icb_gate = corb.index("(Icis & (HDA_ICIS_ICB | HDA_ICIS_IRV)) != 0", icis_read)
        first_ring_touch = corb.index("HDA_REG_CORBCTL", icb_gate)
        self.assertLess(icis_read, icb_gate)
        self.assertLess(icb_gate, first_ring_touch)

        # Revalidate after ownership-sensitive reads/allocations, at the last
        # safe point before each conflicting transport is actually launched.
        icoi_write = raw.index("HdaMemWrite32(PciIo, HDA_REG_ICOI, Command)")
        self.assertGreaterEqual(raw[:icoi_write].count("HDA_REG_CORBCTL"), 2)
        self.assertGreaterEqual(raw[:icoi_write].count("HDA_REG_RIRBCTL"), 2)
        self.assertGreaterEqual(raw[:icoi_write].count("HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis)"), 2)
        rirb_enable = corb.index("HDA_RIRBCTL_RSVDP_MASK) | HDA_RIRBCTL_DMA_ENABLE")
        self.assertGreaterEqual(corb[:rirb_enable].count("HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis)"), 2)
        abort_irv_clear = abort.index("HdaMemWrite16(PciIo, HDA_REG_ICIS, HDA_ICIS_IRV)")
        self.assertGreaterEqual(abort[:abort_irv_clear].count("HDA_REG_CORBCTL"), 2)
        self.assertGreaterEqual(abort[:abort_irv_clear].count("HDA_REG_RIRBCTL"), 2)

    def test_owned_immediate_command_is_aborted_after_response_poll_read_error(self):
        raw = extract_c_function(self.source, "HdaImmediateCommandRaw")
        launch = raw.index("HdaMemWrite16(PciIo, HDA_REG_ICIS, HDA_ICIS_ICB)")
        response_loop = raw.index("HDAU_IMMEDIATE_RESPONSE_TIMEOUT_US", launch)
        status_read = raw.index("HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis)", response_loop)
        abort = raw.index("AbortStatus = HdaAbortImmediateCommand(PciIo)", status_read)
        propagate = raw.index("return EFI_ERROR(AbortStatus) ? AbortStatus : Status;", abort)
        self.assertLess(launch, response_loop)
        self.assertLess(response_loop, status_read)
        self.assertLess(status_read, abort)
        self.assertLess(abort, propagate)

    def test_immediate_command_preserves_ambiguous_entry_state(self):
        raw = extract_c_function(self.source, "HdaImmediateCommandRaw")
        gate = "if ((Icis & (HDA_ICIS_ICB | HDA_ICIS_IRV)) != 0) return EFI_ACCESS_DENIED;"
        self.assertIn(gate, raw)
        self.assertNotIn("HDAU_IMMEDIATE_BUSY_TIMEOUT_US", raw)
        self.assertNotIn("HdaMemWrite16(PciIo, HDA_REG_ICIS, HDA_ICIS_IRV)", raw)
        first_gate = raw.index(gate)
        icoi = raw.index("HdaMemWrite32(PciIo, HDA_REG_ICOI, Command)")
        second_gate = raw.index(gate, first_gate + len(gate))
        self.assertLess(first_gate, second_gate)
        self.assertLess(second_gate, icoi)

    def test_owned_immediate_response_is_retired_even_if_response_read_fails(self):
        raw = extract_c_function(self.source, "HdaImmediateCommandRaw")
        ack = extract_c_function(self.source, "HdaAcknowledgeImmediateResult")
        for token in (
            "HDA_REG_CORBCTL",
            "HDA_REG_RIRBCTL",
            "HDA_CORBCTL_DMA_ENABLE",
            "HDA_RIRBCTL_DMA_ENABLE",
            "EFI_ACCESS_DENIED",
            "HdaMemWrite16(PciIo, HDA_REG_ICIS, HDA_ICIS_IRV)",
        ):
            self.assertIn(token, ack)
        response_read = raw.index("ResponseStatus = HdaMemRead32(PciIo, HDA_REG_ICII, Response)")
        acknowledge = raw.index("AckStatus = HdaAcknowledgeImmediateResult(PciIo)", response_read)
        ack_error = raw.index("if (EFI_ERROR(AckStatus)) return AckStatus;", acknowledge)
        response_return = raw.index("return ResponseStatus;", ack_error)
        self.assertLess(response_read, acknowledge)
        self.assertLess(acknowledge, ack_error)
        self.assertLess(ack_error, response_return)

    def test_corb_fallback_never_clears_foreign_w1c_status_on_entry(self):
        corb = extract_c_function(self.source, "HdaCorbRirbCommand")
        preflight_corb = corb.index("HdaMemRead8(PciIo, HDA_REG_CORBSTS, &CorbSts)")
        preflight_rirb = corb.index("HdaMemRead8(PciIo, HDA_REG_RIRBSTS, &RirbSts)", preflight_corb)
        status_gate = corb.index("(CorbSts & HDA_CORBSTS_CMEI) != 0", preflight_rirb)
        touched = corb.index("RingState.RegistersTouched = TRUE")
        self.assertLess(preflight_corb, preflight_rirb)
        self.assertLess(preflight_rirb, status_gate)
        self.assertLess(status_gate, touched)
        self.assertIn("HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS", corb[:touched])
        self.assertNotIn("(VOID)HdaMemWrite8(PciIo, HDA_REG_CORBSTS", corb)
        self.assertNotIn("(VOID)HdaMemWrite8(PciIo, HDA_REG_RIRBSTS", corb)

    def test_corb_snapshot_is_revalidated_before_first_register_mutation(self):
        helper = extract_c_function(self.source, "HdaRequireCorbRirbSnapshotUnchanged")
        corb = extract_c_function(self.source, "HdaCorbRirbCommand")
        for token in (
            "HDA_REG_CORBCTL", "HDA_REG_RIRBCTL",
            "HDA_REG_CORBSIZE", "HDA_REG_RIRBSIZE",
            "HDA_REG_CORBWP", "HDA_REG_CORBRP", "HDA_REG_RIRBWP", "HDA_REG_RINTCNT",
            "HDA_REG_CORBLBASE", "HDA_REG_CORBUBASE",
            "HDA_REG_RIRBLBASE", "HDA_REG_RIRBUBASE",
            "HDA_REG_CORBSTS", "HDA_REG_RIRBSTS", "HDA_REG_ICIS",
            "EFI_ACCESS_DENIED",
        ):
            self.assertIn(token, helper)
        call = corb.index("Status = HdaRequireCorbRirbSnapshotUnchanged(PciIo, &RingState)")
        gate = corb.index("if (EFI_ERROR(Status)) goto Done;", call)
        touched = corb.index("RingState.RegistersTouched = TRUE", gate)
        first_register_write = corb.index("PciIo, HDA_REG_CORBCTL,", touched)
        self.assertLess(call, gate)
        self.assertLess(gate, touched)
        self.assertLess(touched, first_register_write)

    def test_corb_rirb_rsvdp_fields_are_preserved_on_temporary_writes(self):
        header = self.header
        corb = extract_c_function(self.source, "HdaCorbRirbCommand")
        cleanup = extract_c_function(self.source, "HdaCleanupCorbRirbState")
        corbrp = extract_c_function(self.source, "HdaResetCorbReadPointer")
        rirbwp = extract_c_function(self.source, "HdaResetRirbWritePointer")
        for token in (
            "HDA_CORBCTL_RSVDP_MASK", "HDA_RIRBCTL_RSVDP_MASK",
            "HDA_CORBWP_RSVDP_MASK", "HDA_CORBRP_RSVDP_MASK",
            "HDA_RIRBWP_RSVDP_MASK", "HDA_RINTCNT_RSVDP_MASK",
            "HDA_RING_SIZE_SELECT_MASK",
        ):
            self.assertIn(token, header)
        self.assertIn("OriginalCorbCtl & HDA_CORBCTL_RSVDP_MASK", corb)
        self.assertIn("OriginalRirbCtl & HDA_RIRBCTL_RSVDP_MASK", corb)
        self.assertIn("OriginalCorbSize & (UINT8)~HDA_RING_SIZE_SELECT_MASK", corb)
        self.assertIn("OriginalRirbSize & (UINT8)~HDA_RING_SIZE_SELECT_MASK", corb)
        self.assertIn("OriginalCorbWp & HDA_CORBWP_RSVDP_MASK", corb)
        self.assertIn("OriginalCorbRp & HDA_CORBRP_RESET", corb)
        self.assertIn("OriginalRintCnt & HDA_RINTCNT_RSVDP_MASK", corb)
        self.assertIn("OriginalCorbCtl & HDA_CORBCTL_RSVDP_MASK", cleanup)
        self.assertIn("OriginalRirbCtl & HDA_RIRBCTL_RSVDP_MASK", cleanup)
        self.assertIn("CorbRp & HDA_CORBRP_RSVDP_MASK", corbrp)
        self.assertIn("RirbWp & HDA_RIRBWP_RSVDP_MASK", rirbwp)
        self.assertNotIn("HdaMemWrite8(PciIo, HDA_REG_CORBCTL, 0)", corb)
        self.assertNotIn("HdaMemWrite8(PciIo, HDA_REG_RIRBCTL, 0)", corb)
        self.assertNotIn("HdaMemWrite16(PciIo, HDA_REG_RINTCNT, 1)", corb)

    def test_temporary_ring_programming_and_dma_enable_are_read_back_before_dispatch(self):
        program = extract_c_function(self.source, "HdaVerifyTemporaryCorbRirbProgramming")
        enabled = extract_c_function(self.source, "HdaVerifyCorbRirbDmaEnabled")
        corb = extract_c_function(self.source, "HdaCorbRirbCommand")
        for token in (
            "HDA_REG_CORBCTL", "HDA_REG_RIRBCTL",
            "HDA_REG_CORBSIZE", "HDA_REG_RIRBSIZE",
            "HDA_REG_CORBWP", "HDA_REG_CORBRP", "HDA_REG_RIRBWP", "HDA_REG_RINTCNT",
            "HDA_REG_CORBLBASE", "HDA_REG_CORBUBASE",
            "HDA_REG_RIRBLBASE", "HDA_REG_RIRBUBASE",
            "EFI_DEVICE_ERROR",
        ):
            self.assertIn(token, program)
        self.assertIn("HDA_CORBCTL_DMA_ENABLE", enabled)
        self.assertIn("HDA_RIRBCTL_DMA_ENABLE", enabled)
        verify_program = corb.index("Status = HdaVerifyTemporaryCorbRirbProgramming(")
        enable_rirb = corb.index("HDA_RIRBCTL_RSVDP_MASK) | HDA_RIRBCTL_DMA_ENABLE", verify_program)
        enable_corb = corb.index("HDA_CORBCTL_RSVDP_MASK) | HDA_CORBCTL_DMA_ENABLE", enable_rirb)
        verify_enable = corb.index("Status = HdaVerifyCorbRirbDmaEnabled(PciIo, &RingState)", enable_corb)
        dispatch = corb.index("OriginalCorbWp & HDA_CORBWP_RSVDP_MASK) | 1U", verify_enable)
        self.assertLess(verify_program, enable_rirb)
        self.assertLess(enable_rirb, enable_corb)
        self.assertLess(enable_corb, verify_enable)
        self.assertLess(verify_enable, dispatch)

    def test_corb_memory_error_is_detected_and_reset_only_after_cleanup(self):
        corb = extract_c_function(self.source, "HdaCorbRirbCommand")
        poll = corb.index("HDAU_CORB_RIRB_TIMEOUT_US")
        corb_status_read = corb.index("HdaMemRead8(PciIo, HDA_REG_CORBSTS, &CorbSts)", poll)
        cmei = corb.index("CorbMemoryError = TRUE;", corb_status_read)
        done = corb.index("Done:", cmei)
        cleanup = corb.index("HdaCleanupCorbRirbState(&RingState, FALSE)", done)
        late_rirb = corb.index("RirbStatusRead = HdaMemRead8(PciIo, HDA_REG_RIRBSTS, &RirbSts)", done)
        late_overrun = corb.index("(RirbSts & HDA_RIRBSTS_RIRBOIS) != 0", late_rirb)
        reset = corb.index("ResetStatus = HdaControllerReset(PciIo)", cleanup)
        self.assertLess(poll, corb_status_read)
        self.assertLess(corb_status_read, cmei)
        self.assertLess(cmei, done)
        self.assertLess(done, late_rirb)
        self.assertLess(late_rirb, late_overrun)
        self.assertLess(late_overrun, cleanup)
        self.assertLess(cleanup, reset)
        self.assertIn("mHdaCorbRirbFallbackDisabled = TRUE;", corb[reset:])

    def test_idle_stream_descriptor_is_snapshotted_and_revalidated_before_reset(self):
        capture = extract_c_function(self.source, "HdaCaptureIdleStreamState")
        gate = extract_c_function(self.source, "HdaRequireIdleStreamSnapshotUnchanged")
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        for token in (
            "HDA_SD_CTL", "HDA_SD_STS", "HDA_SD_CBL", "HDA_SD_LVI",
            "HDA_SD_FMT", "HDA_SD_BDLPL", "HDA_SD_BDLPU",
            "HDA_SD_STS_OWNERSHIP_MASK", "StreamStateValid",
        ):
            self.assertIn(token, capture + gate)
        snapshot = prepare.index("HdaCaptureIdleStreamState(Context, Sd)")
        revalidate = prepare.index("HdaRequireIdleStreamSnapshotUnchanged(Context, Sd)", snapshot)
        publish = prepare.index("Context->StreamOffset = Sd;", revalidate)
        reset = prepare.index("HdaResetStreamDescriptor(PciIo, Sd)", publish)
        self.assertLess(snapshot, revalidate)
        self.assertLess(revalidate, publish)
        self.assertLess(publish, reset)

    def test_bidirectional_output_sets_dir_immediately_after_stream_reset(self):
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        self.assertIn("HDA_SD_CTL3_DIR               BIT3  /* SDnCTL bit 19; set for output", self.header)
        reset = prepare.index("HdaResetStreamDescriptor(PciIo, Sd)")
        ctl2_write = prepare.index("HdaMemWrite8(PciIo, Sd + HDA_SD_CTL + 2, Ctl2)", reset)
        cbl_write = prepare.index("HdaMemWrite32(PciIo, Sd + HDA_SD_CBL", ctl2_write)
        self.assertIn("if (UsingBidirectional) Ctl2 |= HDA_SD_CTL3_DIR;", prepare[reset:ctl2_write])
        self.assertNotIn("Ctl2 &= (UINT8)~HDA_SD_CTL3_DIR", prepare)
        self.assertLess(reset, ctl2_write)
        self.assertLess(ctl2_write, cbl_write)

    def test_stream_lvi_and_format_preserve_reserved_fields(self):
        lvi = extract_c_function(self.source, "HdaWriteStreamLviPreserve")
        fmt = extract_c_function(self.source, "HdaWriteStreamFormatPreserve")
        self.assertIn("HDA_SD_LVI_RSVDP_MASK", self.header)
        self.assertIn("HDA_SD_FMT_PRESERVE_MASK", self.header)
        self.assertIn("CurrentLvi & HDA_SD_LVI_RSVDP_MASK", lvi)
        self.assertIn("DesiredLvi & HDA_SD_LVI_VALUE_MASK", lvi)
        self.assertIn("CurrentFmt & HDA_SD_FMT_PRESERVE_MASK", fmt)
        self.assertIn("DesiredFmt & (UINT16)~HDA_SD_FMT_PRESERVE_MASK", fmt)

    def test_stream_programming_is_read_back_before_run_and_run_is_confirmed(self):
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        verify = extract_c_function(self.source, "HdaVerifyPreparedOutputStream")
        start = extract_c_function(self.source, "HdaStartOutputStream")
        flush = prepare.index("Status = HdaPciFlushExact(PciIo)")
        readback = prepare.index("HdaVerifyPreparedOutputStream(Context, UsingBidirectional)", flush)
        self.assertLess(flush, readback)
        for token in ("HDA_SD_CBL", "HDA_SD_LVI", "HDA_SD_FMT", "HDA_SD_BDLPL", "HDA_SD_BDLPU"):
            self.assertIn(token, verify)
        run_write = start.index("HdaMemWrite8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL, Ctl)")
        flush_run = start.index("HdaPciFlushExact(Context->PciIo)", run_write)
        run_readback = start.index("HdaMemRead8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL, &Ctl)", flush_run)
        self.assertLess(run_write, flush_run)
        self.assertLess(flush_run, run_readback)
        self.assertIn("(Ctl & HDA_SD_CTL_RUN) != 0", start[run_readback:])

    def test_stopped_stream_is_reset_before_cbl_restore_and_prior_state_is_restored(self):
        restore = extract_c_function(self.source, "HdaRestoreStreamDescriptorState")
        stop = extract_c_function(self.source, "HdaStopOutputStream")
        reset = restore.index("HdaResetStreamDescriptor(Context->PciIo, Sd)")
        ctl2 = restore.index("HDA_SD_CTL + 2, Context->OriginalStreamCtl2", reset)
        cbl = restore.index("HDA_SD_CBL, Context->OriginalStreamCbl", ctl2)
        self.assertLess(reset, ctl2)
        self.assertLess(ctl2, cbl)
        self.assertIn("HdaWriteStreamLviPreserve", restore[cbl:])
        self.assertIn("HdaWriteStreamFormatPreserve", restore[cbl:])
        self.assertIn("Context->OriginalStreamBdlpl", restore[cbl:])
        self.assertIn("Context->OriginalStreamBdlpu", restore[cbl:])
        restore_call = stop.index("HdaRestoreStreamDescriptorState(Context)")
        self.assertNotIn("HdaMemWrite32(PciIo, Sd + HDA_SD_CBL, 0)", stop[restore_call:])
        self.assertNotIn("HdaMemWrite16(PciIo, Sd + HDA_SD_LVI, 0)", stop[restore_call:])

    def test_all_nonzero_descriptor_tags_are_reserved_even_when_run_is_clear(self):
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        scan_start = prepare.index("ZeroMem(UsedTags, sizeof(UsedTags))")
        scan_end = prepare.index("mHdaActiveStreamTag = 0", scan_start)
        scan = prepare[scan_start:scan_end]
        self.assertIn("HDA_SD_CTL + 2", scan)
        self.assertIn("if (Tag != 0) UsedTags[Tag] = TRUE;", scan)
        self.assertNotIn("HDA_SD_CTL_RUN", scan)

    def test_stream_candidate_rejects_reset_or_pending_status(self):
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        self.assertGreaterEqual(prepare.count("HDA_SD_CTL_RUN | HDA_SD_CTL_SRST"), 2)
        self.assertGreaterEqual(prepare.count("HDA_SD_STS_OWNERSHIP_MASK"), 2)
        self.assertIn("HDA_SD_STS_OWNERSHIP_MASK", self.header)

    def test_stream_tag_is_revalidated_immediately_before_descriptor_ownership_and_run(self):
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        start = extract_c_function(self.source, "HdaStartOutputStream")
        helper = extract_c_function(self.source, "HdaRequireStreamTagAvailable")
        self.assertIn("HDA_REG_GCAP", helper)
        self.assertIn("HDA_SD_CTL_SRST", helper)
        self.assertIn("if (Tag == StreamTag) return EFI_ACCESS_DENIED;", helper)
        snapshot_gate = prepare.index("HdaRequireIdleStreamSnapshotUnchanged(Context, Sd)")
        tag_gate = prepare.index("HdaRequireStreamTagAvailable(PciIo, Sd, mHdaActiveStreamTag)", snapshot_gate)
        publish = prepare.index("Context->StreamOffset = Sd;", tag_gate)
        self.assertLess(snapshot_gate, tag_gate)
        self.assertLess(tag_gate, publish)
        start_tag_gate = start.index("HdaRequireStreamTagAvailable(Context->PciIo, Context->StreamOffset, mHdaActiveStreamTag)")
        run_write = start.index("HdaMemWrite8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL, Ctl)")
        self.assertLess(start_tag_gate, run_write)

    def test_r28_metadata_and_history_are_consistent(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        release_ini = (ROOT / "HDAUChimeRelease.ini").read_text(encoding="utf-8")
        debug_ini = (ROOT / "HDAUChimeDebug.ini").read_text(encoding="utf-8")
        helper = (ROOT / "Enable-HDAUCodecDump-NextBoot.command").read_text(encoding="utf-8")
        credit = "©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)"
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertIn("1.0.87-final-audit", release_ini)
        self.assertIn("1.0.87-final-audit", debug_ini)
        self.assertIn("1.0.87-final-audit", helper)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.74 — final audit R29", changelog)
        self.assertIn("1.0.72 — final audit R27", changelog)
        self.assertIn("1.0.71 — final audit R26", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))
        self.assertIn(credit, readme)
        self.assertIn(credit, changelog)


class HdaSpecConformanceRound28Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_32bit_only_controller_never_writes_read_only_ring_upper_bases(self):
        corb = extract_c_function(self.source, "HdaCorbRirbCommand")
        cleanup = extract_c_function(self.source, "HdaCleanupCorbRirbState")
        verify = extract_c_function(self.source, "HdaVerifyTemporaryCorbRirbProgramming")
        self.assertIn("RingState.Supports64BitDma = ((Gcap & HDA_GCAP_64OK) != 0);", corb)
        for token in ("HDA_REG_CORBUBASE", "HDA_REG_RIRBUBASE"):
            pos = corb.index(f"HdaMemWrite32(PciIo, {token}")
            gate = corb.rfind("if (RingState.Supports64BitDma) {", 0, pos)
            self.assertGreaterEqual(gate, 0)
            self.assertLess(pos - gate, 220)
            pos = cleanup.index(f"HdaMemWrite32(State->PciIo, {token}")
            gate = cleanup.rfind("if (State->Supports64BitDma) {", 0, pos)
            self.assertGreaterEqual(gate, 0)
            self.assertLess(pos - gate, 220)
        self.assertIn("State->OriginalCorbUbase", verify)
        self.assertIn("State->OriginalRirbUbase", verify)

    def test_32bit_only_controller_never_writes_read_only_stream_bdl_upper_base(self):
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        restore = extract_c_function(self.source, "HdaRestoreStreamDescriptorState")
        verify = extract_c_function(self.source, "HdaVerifyPreparedOutputStream")
        self.assertIn("BOOLEAN             Controller64BitDma;", self.header)
        self.assertIn("Context->Controller64BitDma = ((Gcap & HDA_GCAP_64OK) != 0);", prepare)
        for body in (prepare, restore):
            # Locate the upper-base write and require the capability gate to be local.
            pos = body.index("HDA_SD_BDLPU")
            write = body.rfind("HdaMemWrite32", 0, pos)
            gate = body.rfind("if (Context->Controller64BitDma) {", 0, write)
            self.assertGreaterEqual(gate, 0)
            self.assertLess(write - gate, 220)
        self.assertIn(": Context->OriginalStreamBdlpu", verify)

    def test_stream_tag_scan_rejects_concurrent_descriptor_mutation(self):
        helper = extract_c_function(self.source, "HdaRequireStreamTagAvailable")
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        self.assertIn("CtlAfter != CtlBefore", helper)
        self.assertIn("Ctl2After != Ctl2", helper)
        self.assertIn("CandidateCtlAfter != Ctl", prepare)
        self.assertIn("Ctl2After != Ctl2", prepare)

    def test_stream_restore_is_read_back_before_descriptor_ownership_is_released(self):
        verify = extract_c_function(self.source, "HdaVerifyRestoredStreamDescriptorState")
        restore = extract_c_function(self.source, "HdaRestoreStreamDescriptorState")
        stop = extract_c_function(self.source, "HdaStopOutputStream")
        for token in (
            "HDA_SD_CTL", "HDA_SD_STS", "HDA_SD_CBL", "HDA_SD_LVI",
            "HDA_SD_FMT", "HDA_SD_BDLPL", "HDA_SD_BDLPU",
            "HDA_SD_CTL_INTERRUPT_MASK", "HDA_SD_STS_OWNERSHIP_MASK",
        ):
            self.assertIn(token, verify)
        flush = restore.index("HdaPciFlushExact(Context->PciIo)")
        readback = restore.index("HdaVerifyRestoredStreamDescriptorState(Context)", flush)
        release = restore.index("Context->StreamStateValid = FALSE;", readback)
        self.assertLess(flush, readback)
        self.assertLess(readback, release)
        first_restore = stop.index("HdaRestoreStreamDescriptorState(Context)")
        restore_tail = stop[first_restore:]
        # Once RUN is proven clear, a local descriptor-restore failure must not
        # escalate into CRST and destroy controller-wide foreign state.
        self.assertNotIn("HdaControllerReset(PciIo)", restore_tail)
        self.assertIn("Context->PostPlaybackCleanupStatus = Status;", restore_tail)
        self.assertIn("return EFI_SUCCESS;", restore_tail)



class HdaSpecConformanceRound29Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_start_revalidates_complete_descriptor_after_foreign_tag_scan(self):
        start = extract_c_function(self.source, "HdaStartOutputStream")
        tag_scan = start.index("HdaRequireStreamTagAvailable")
        full_verify = start.index("HdaVerifyPreparedOutputStream(Context, Context->StreamBidirectional)", tag_scan)
        run_write = start.index("HdaMemWrite8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL, Ctl)", full_verify)
        self.assertLess(tag_scan, full_verify)
        self.assertLess(full_verify, run_write)
        self.assertIn("HDA_SD_CBL", extract_c_function(self.source, "HdaVerifyPreparedOutputStream"))
        self.assertIn("HDA_SD_BDLPL", extract_c_function(self.source, "HdaVerifyPreparedOutputStream"))

    def test_local_descriptor_restore_failure_never_escalates_to_global_crst(self):
        stop = extract_c_function(self.source, "HdaStopOutputStream")
        restore = stop.index("HdaRestoreStreamDescriptorState(Context)")
        tail = stop[restore:]
        self.assertNotIn("HdaControllerReset(PciIo)", tail)
        self.assertIn("global controller reset intentionally avoided", tail)
        self.assertIn("Context->PostPlaybackCleanupStatus = Status;", tail)
        self.assertIn("return EFI_SUCCESS;", tail)
        self.assertIn("Context->StreamOffset = 0;", tail)
        self.assertIn("Context->StreamStateValid = FALSE;", tail)

    def test_cleanup_degradation_keeps_real_status_without_forcing_replay(self):
        finalize = extract_c_function(self.source, "HdaFinalizePlaybackContext")
        attempt = extract_c_function(self.source, "HdaTryController")
        self.assertIn("Context->PostPlaybackCleanupStatus", finalize)
        self.assertIn("return EFI_SUCCESS;", finalize)
        self.assertIn('"SuccessWithCleanupDegradation"', attempt)
        self.assertIn("Context.PostPlaybackCleanupStatus", attempt)
        self.assertIn("Result = Status;", attempt)
        self.assertNotIn("EFI_WARN_WRITE_FAILURE", finalize)

    def test_stream_descriptor_kind_is_owned_and_cleared_with_descriptor(self):
        prepare = extract_c_function(self.source, "HdaPrepareOutputStream")
        stop = extract_c_function(self.source, "HdaStopOutputStream")
        self.assertIn("BOOLEAN             StreamBidirectional;", self.header)
        self.assertIn("Context->StreamBidirectional = UsingBidirectional;", prepare)
        self.assertGreaterEqual(stop.count("Context->StreamBidirectional = FALSE;"), 4)

    def test_async_cleanup_retains_real_cleanup_status_for_retry(self):
        cleanup = extract_c_function(self.source, "HdaCleanupAsyncPlayback")
        self.assertIn("Context->PostPlaybackCleanupStatus", cleanup)
        self.assertIn("EFI_ERROR(RestoreStatus)", cleanup)
        self.assertIn("mHdaAsyncContext = Context;", cleanup)

    def test_generic_uefi_warning_code_is_not_misused_for_mmio_cleanup(self):
        self.assertNotIn("EFI_WARN_WRITE_FAILURE", self.source)
        self.assertIn("EFI_STATUS          PostPlaybackCleanupStatus;", self.header)

    def test_fastboot_surfaces_stream_restore_degradation_before_context_release(self):
        tick = extract_c_function(self.source, "HdaAsyncPlaybackTick")
        restore = tick.index("EFI_ERROR(RestoreStatus)")
        degrade = tick.index("EFI_ERROR(Context->PostPlaybackCleanupStatus)", restore)
        release = tick.index("FreePool(Context)", degrade)
        self.assertLess(restore, degrade)
        self.assertLess(degrade, release)
        self.assertIn('"PlaybackDoneFastBootCleanupDegraded"', tick)



class ReleaseAuditRound30Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")

    def test_packaging_uses_the_same_path_resolved_zip_tools_as_preflight(self):
        self.assertIn("require_command zip", self.build)
        self.assertIn("require_command unzip", self.build)
        self.assertNotIn("/usr/bin/zip", self.build)
        self.assertNotIn("/usr/bin/unzip", self.build)
        self.assertIn('zip -qry "$(basename "$ZIP")" Debug Release EFI-Test-Pack', self.build)
        self.assertIn('unzip -tq "$ZIP"', self.build)

    def test_r30_release_metadata_is_synchronized_and_history_is_preserved(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        release_ini = (ROOT / "HDAUChimeRelease.ini").read_text(encoding="utf-8")
        debug_ini = (ROOT / "HDAUChimeDebug.ini").read_text(encoding="utf-8")
        helper = (ROOT / "Enable-HDAUCodecDump-NextBoot.command").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', self.build)
        self.assertIn("1.0.87-final-audit", release_ini)
        self.assertIn("1.0.87-final-audit", debug_ini)
        self.assertIn("1.0.87-final-audit", helper)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.75 — final audit R30", changelog)
        self.assertIn("1.0.74 — final audit R29", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))
        self.assertIn("©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)", readme)
        self.assertIn("©DNMTechLabs / [dnmtechlabs@gmail.com](mailto:dnmtechlabs@gmail.com)", changelog)


class ReleaseAuditRound31Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_source_hygiene_is_gated_before_python_test_imports(self):
        self.assertIn("verify_source_tree_hygiene()", self.build)
        self.assertIn("verify_source_tree_hygiene\nrun_source_regressions", self.build)
        fn_start = self.build.index("verify_source_tree_hygiene()")
        fn_end = self.build.index("\n}\n", fn_start) + 3
        fn = self.build[fn_start:fn_end]
        for token in ("__pycache__", "*.pyc", ".DS_Store", "*.tmp.*"):
            self.assertIn(token, fn)
        self.assertIn("-path './edk2' -prune", fn)
        preflight = self.build[self.build.index("preflight_tools()"):self.build.index("run_source_regressions()") ]
        self.assertIn("require_command find", preflight)
        self.assertIn("require_command sort", preflight)

    def test_append_log_recreates_file_at_the_exact_size_limit(self):
        log_open = extract_c_function(self.source, "HdaOpenDebugLog")
        self.assertIn("LogSize >= HDAU_MAX_LOG_FILE_BYTES", log_open)
        self.assertNotIn("LogSize > HDAU_MAX_LOG_FILE_BYTES", log_open)

    def test_r31_release_metadata_is_synchronized_and_r30_history_is_preserved(self):
        header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        release_ini = (ROOT / "HDAUChimeRelease.ini").read_text(encoding="utf-8")
        debug_ini = (ROOT / "HDAUChimeDebug.ini").read_text(encoding="utf-8")
        helper = (ROOT / "Enable-HDAUCodecDump-NextBoot.command").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', self.build)
        self.assertIn("1.0.87-final-audit", release_ini)
        self.assertIn("1.0.87-final-audit", debug_ini)
        self.assertIn("1.0.87-final-audit", helper)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.76 — final audit R31", changelog)
        self.assertIn("1.0.75 — final audit R30", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class HdaSpecConformanceRound32Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        cls.generator = (ROOT / "Tools" / "generate_applealc_profiles.py").read_text(encoding="utf-8")

    def test_connection_list_rejects_sequential_range_entries(self):
        body = extract_c_function(self.source, "HdaGetConnections")
        self.assertIn("BOOLEAN PreviousWasRange;", body)
        self.assertIn("if (!HavePrevious || PreviousWasRange || Node <= Previous)", body)
        self.assertIn("PreviousWasRange = IsRange;", body)

    def test_command_transports_reject_unsupported_indirect_nids(self):
        self.assertIn("#define HDAU_MAX_NID                  0x7FU", self.header)
        for name in ("HdaImmediateCommandRaw", "HdaCorbRirbCommand"):
            body = extract_c_function(self.source, name)
            gate = body.index("Nid > HDAU_MAX_NID")
            compose = body.index("Command = ((UINT32)Codec << 28)")
            self.assertLess(gate, compose)

    def test_generator_rejects_indirect_configdata_nid_encoding(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "indirect-nid.zip"
            make_archive(archive, [{
                "CodecID": 0x10EC0298,
                "LayoutID": 1,
                "ConfigData": (0x09471C10).to_bytes(4, "big"),
            }])
            text, stats = GENERATOR.generate(archive)
            self.assertEqual(stats["config_profiles"], 0)
            self.assertEqual(stats["config_invalid"], 1)
            self.assertNotIn("0x09471C10", text)

    def test_applealc_profile_fallback_skips_pin_when_configdefault_read_fails(self):
        body = extract_c_function(self.source, "HdaTryAlcIdProfilePath")
        self.assertIn("if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Pin,", body)
        failure = body.index("if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Pin,")
        connected = body.index("if (!AllowNoConnect && !HdaPinDefaultLooksConnected(Config))", failure)
        self.assertIn("continue;", body[failure:connected])

    def test_controller_dump_propagates_mandatory_register_read_failures(self):
        body = extract_c_function(self.source, "HdaDumpControllerFull")
        for register in ("HDA_REG_GCAP", "HDA_REG_VMIN", "HDA_REG_VMAJ", "HDA_REG_OUTPAY", "HDA_REG_INPAY"):
            self.assertNotIn(f"(VOID)HdaMemRead", body[body.index(register)-40:body.index(register)+len(register)+80])
            pos = body.index(register)
            window = body[max(0, pos-80):pos+180]
            self.assertIn("Status = HdaMemRead", window)
            self.assertIn("EFI_ERROR(Status)", window)

    def test_volume_knob_widget_type_is_defined_and_named(self):
        self.assertIn("#define HDA_WIDGET_TYPE_VOLUME_KNOB   0x6", self.header)
        body = extract_c_function(self.source, "HdaDumpWidgetTypeName")
        self.assertIn('case HDA_WIDGET_TYPE_VOLUME_KNOB: return "Volume Knob";', body)

    def test_widget_indexed_state_matches_direct_nid_domain(self):
        self.assertIn("#define HDAU_MAX_NID                  0x7FU", self.header)
        self.assertIn("#define HDAU_MAX_WIDGETS              (HDAU_MAX_NID + 1U)", self.header)
        self.assertNotIn("#define HDAU_MAX_WIDGETS              256", self.header)
        subnodes = extract_c_function(self.source, "HdaGetSubnodes")
        self.assertIn("First > HDAU_MAX_NID", subnodes)
        self.assertIn("Number > ((UINTN)HDAU_MAX_NID - First + 1U)", subnodes)

    def test_selected_route_classification_fails_closed_on_config_read_error(self):
        body = extract_c_function(self.source, "HdaClassifySelectedRoute")
        self.assertIn("if (EFI_ERROR(HdaImmediateCommand", body)
        self.assertIn("return FALSE;", body)
        self.assertIn("return TRUE;", body)
        for caller in ("HdaTryFullAlcIdRoute", "HdaTryAlcIdProfilePath", "HdaTryQuickPath", "HdaFindPlayablePath"):
            caller_body = extract_c_function(self.source, caller)
            self.assertIsNone(
                re.search(r"(?m)^\s*HdaClassifySelectedRoute\(Context\);\s*$", caller_body),
                msg=f"{caller} must not discard route-classification failure",
            )
            self.assertIn("HdaClassifySelectedRoute(Context)", caller_body)


    def test_route_candidate_reuses_primary_classification_and_fallback_fails_closed(self):
        body = extract_c_function(self.source, "HdaAddRouteCandidate")
        self.assertIn("Context->DigitalRoute", body)
        self.assertIn("Context->HdmiRoute", body)
        self.assertIn("Context->DisplayPortRoute", body)
        self.assertIn("HDA_VERB_GET_CONFIG_DEFAULT", body)
        self.assertRegex(body, r"if \(PrimaryRoute\)[\s\S]*?Context->DigitalRoute")
        self.assertRegex(body, r"Status\s*=\s*HdaImmediateCommand[\s\S]*?if \(EFI_ERROR\(Status\)\)[\s\S]*?return;")
        self.assertLess(body.index("HDA_VERB_GET_CONFIG_DEFAULT"), body.index("Context->RouteCandidateCount++"))

    def test_output_pin_programming_propagates_configdefault_read_failure(self):
        body = extract_c_function(self.source, "HdaProgramOutputPin")
        self.assertIn("HDA_VERB_GET_CONFIG_DEFAULT", body)
        self.assertRegex(body, r"Status\s*=\s*HdaImmediateCommand[\s\S]*?if \(EFI_ERROR\(Status\)\) return Status;")
        compute = extract_c_function(self.source, "HdaComputeOutputPinCtl")
        self.assertNotIn("HdaImmediateCommand", compute)
        self.assertIn("UINT32 Config", compute)

    def test_realtek_profile_fails_closed_when_configdefault_read_fails(self):
        body = extract_c_function(self.source, "HdaRealtekProgramPinDacProfile")
        self.assertRegex(body, r"if \(EFI_ERROR\(HdaImmediateCommand[\s\S]*?return FALSE;")

    def test_legacy_realtek_fanout_skips_pins_when_configdefault_read_fails(self):
        body = extract_c_function(self.source, "HdaEnableLegacyRealtekFanout")
        self.assertGreaterEqual(body.count("EFI_ERROR(HdaImmediateCommand"), 2)
        self.assertGreaterEqual(body.count("HDA_VERB_GET_CONFIG_DEFAULT"), 2)


    def test_full_applealc_route_does_not_bypass_digital_policy_on_config_read_error(self):
        body = extract_c_function(self.source, "HdaTryFullAlcIdRoute")
        self.assertRegex(body, r"if \(EFI_ERROR\(HdaImmediateCommand[\s\S]*?continue;")
        self.assertNotIn("(VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Route[0]", body)


    def test_path_search_validates_direct_nids_before_indexing_reduced_arrays(self):
        body = extract_c_function(self.source, "HdaPathSearch")
        guard = "StartPin > HDAU_MAX_NID || TargetDac > HDAU_MAX_NID"
        self.assertIn(guard, body)
        self.assertLess(body.index(guard), body.index("Visited[StartPin] = TRUE;"))
        self.assertNotIn("full 8-bit HDA node address space", body)


    def test_selector_claim_tables_validate_nids_before_128_entry_indexing(self):
        for name in ("HdaResetFanoutSelectorClaims", "HdaPathsSelectorCompatible", "HdaCommitFanoutSelectorClaims"):
            body = extract_c_function(self.source, name)
            self.assertIn("Nid > HDAU_MAX_NID", body, msg=name)
            self.assertLess(body.index("Nid > HDAU_MAX_NID"), body.index("mHdaFanoutSelectorNext[Nid]"), msg=name)


    def test_configdata_runtime_rejects_indirect_nids_before_snapshot_indexing(self):
        span = extract_c_function(self.source, "HdaConfigSpanIsRollbackSafe")
        self.assertIn("0x08000000U", span)
        self.assertRegex(span, r"0x08000000U\) != 0\) return FALSE;")
        coef = extract_c_function(self.source, "HdaSnapshotCoefficientSpan")
        self.assertIn("Nid > HDAU_MAX_NID", coef)
        self.assertLess(coef.index("Nid > HDAU_MAX_NID"), coef.index("TouchedIndex[Nid]"))


    def test_r32_release_metadata_is_synchronized_and_r31_history_is_preserved(self):
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text()
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text()
        release_ini = (ROOT / "HDAUChimeRelease.ini").read_text()
        debug_ini = (ROOT / "HDAUChimeDebug.ini").read_text()
        helper = (ROOT / "Enable-HDAUCodecDump-NextBoot.command").read_text()
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text()
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text()
        changelog = (ROOT / "CHANGELOG.md").read_text()
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text()
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', self.header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertIn("1.0.87-final-audit", release_ini)
        self.assertIn("1.0.87-final-audit", debug_ini)
        self.assertIn("1.0.87-final-audit", helper)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.77 — final audit R32", changelog)
        self.assertIn("1.0.76 — final audit R31", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))


class HdaSpecConformanceRound33Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_r33_release_metadata_is_synchronized_and_r32_history_is_preserved(self):
        header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        release_ini = (ROOT / "HDAUChimeRelease.ini").read_text(encoding="utf-8")
        debug_ini = (ROOT / "HDAUChimeDebug.ini").read_text(encoding="utf-8")
        helper = (ROOT / "Enable-HDAUCodecDump-NextBoot.command").read_text(encoding="utf-8")
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', header)
        self.assertIn('HDAU_CHIME_VERSION_ASCII      "1.0.87-final-audit"', header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', build)
        self.assertIn("1.0.87-final-audit", release_ini)
        self.assertIn("1.0.87-final-audit", debug_ini)
        self.assertIn("1.0.87-final-audit", helper)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.78 — final audit R33", changelog)
        self.assertIn("1.0.77 — final audit R32", changelog)
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))

    def test_quick_path_nonforced_configdefault_read_fails_closed(self):
        body = extract_c_function(self.source, "HdaTryQuickPath")
        pin_loop = body[body.index("for (PinIndex = 0; PinIndex < PinCount; PinIndex++)"):]
        read = pin_loop.index("HDA_VERB_GET_CONFIG_DEFAULT")
        dependent = pin_loop.index("HdaPinDefaultLooksConnected(Config)", read)
        window = pin_loop[max(0, read - 220):dependent]
        self.assertIn("if (!ForcedPin)", window)
        self.assertIn("Status = HdaImmediateCommand", window)
        self.assertIn("if (EFI_ERROR(Status)) continue;", window)

    def test_alcid_safe_fanout_configdefault_read_fails_closed(self):
        body = extract_c_function(self.source, "HdaAlcIdLayoutSafeFanout")
        read = body.index("HDA_VERB_GET_CONFIG_DEFAULT")
        dependent = body.index("HdaPinDefaultLooksConnected(Config)", read)
        window = body[max(0, read - 180):dependent]
        self.assertIn("EFI_ERROR(HdaImmediateCommand", window)
        self.assertIn("continue;", window)

    def test_realtek_applealc_combo_configdefault_read_fails_closed(self):
        body = extract_c_function(self.source, "HdaRealtekAppleAlcComboInit")
        read = body.index("HDA_VERB_GET_CONFIG_DEFAULT")
        dependent = body.index("HdaPinDefaultLooksConnected(Config)", read)
        window = body[max(0, read - 180):dependent]
        self.assertIn("EFI_ERROR(HdaImmediateCommand", window)
        self.assertIn("continue;", window)


    def test_codec_dump_reads_subsystem_id_from_function_group_not_root(self):
        body = extract_c_function(self.source, "HdaDumpCodecFull")
        self.assertNotIn(
            "HdaImmediateCommand(PciIo, Codec, 0, HDA_VERB12(HDA_VERB_GET_SUBSYSTEM_ID, 0)",
            body,
        )
        self.assertRegex(
            body,
            r"HdaImmediateCommand\(PciIo, Codec, Fg,\s*HDA_VERB12\(HDA_VERB_GET_SUBSYSTEM_ID, 0\)",
        )

    def test_codec_dump_subsystem_id_is_limited_to_spec_function_group_types(self):
        body = extract_c_function(self.source, "HdaDumpCodecFull")
        read = body.index("HDA_VERB_GET_SUBSYSTEM_ID")
        window = body[max(0, read - 760):read]
        self.assertIn("FunctionType & 0xFFU", window)
        self.assertIn("FunctionGroupType == 0x01U", window)
        self.assertIn("FunctionGroupType == 0x02U", window)

    def test_codec_dump_propagates_mandatory_identity_read_failures(self):
        body = extract_c_function(self.source, "HdaDumpCodecFull")
        declaration = self.source[self.source.rfind("STATIC", 0, self.source.index("HdaDumpCodecFull(")):self.source.index("HdaDumpCodecFull(") + len("HdaDumpCodecFull(")]
        self.assertIn("STATIC EFI_STATUS HdaDumpCodecFull(", declaration)
        for parameter in ("HDA_PARAM_VENDOR_ID", "HDA_PARAM_REV_ID"):
            pos = body.index(parameter)
            window = body[max(0, pos - 180):pos + 260]
            self.assertIn("Status = HdaImmediateCommand", window)
            self.assertIn("EFI_ERROR(Status)", window)
            self.assertIn("return Status;", window)
        controller = extract_c_function(self.source, "HdaDumpControllerFull")
        self.assertIn("Status = HdaDumpCodecFull(File, PciIo, Info, Codec, StateSts);", controller)
        self.assertRegex(controller, r"Status = HdaDumpCodecFull[\s\S]*?if \(EFI_ERROR\(Status\)\)[\s\S]*?goto Exit;")

    def test_embedded_efi_validator_accepts_boot_service_driver_and_rejects_wrong_subsystem(self):
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        marker = "<<'PY_EFI_VALIDATE'"
        marker_pos = build.index(marker)
        start = build.index("\n", marker_pos) + 1
        end = build.index("\nPY_EFI_VALIDATE", start)
        validator = build[start:end]
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "driver.efi"
            data = bytearray(0x400)
            data[0:2] = b"MZ"
            data[0x3C:0x40] = (0x80).to_bytes(4, "little")
            data[0x80:0x84] = b"PE\0\0"
            data[0x84:0x86] = (0x8664).to_bytes(2, "little")
            data[0x86:0x88] = (1).to_bytes(2, "little")
            data[0x94:0x96] = (0xF0).to_bytes(2, "little")
            data[0x96:0x98] = (0x0002).to_bytes(2, "little")
            optional = 0x98
            data[optional:optional + 2] = (0x20B).to_bytes(2, "little")
            data[optional + 16:optional + 20] = (0x1000).to_bytes(4, "little")
            data[optional + 32:optional + 36] = (0x1000).to_bytes(4, "little")
            data[optional + 36:optional + 40] = (0x200).to_bytes(4, "little")
            data[optional + 56:optional + 60] = (0x2000).to_bytes(4, "little")
            data[optional + 60:optional + 64] = (0x200).to_bytes(4, "little")
            data[optional + 68:optional + 70] = (11).to_bytes(2, "little")
            data[optional + 108:optional + 112] = (16).to_bytes(4, "little")
            section = optional + 0xF0
            data[section:section + 8] = b".text\0\0\0"
            data[section + 8:section + 12] = (0x100).to_bytes(4, "little")
            data[section + 12:section + 16] = (0x1000).to_bytes(4, "little")
            data[section + 16:section + 20] = (0x200).to_bytes(4, "little")
            data[section + 20:section + 24] = (0x200).to_bytes(4, "little")
            data[section + 36:section + 40] = (0x60000020).to_bytes(4, "little")
            image.write_bytes(data)
            good = subprocess.run(
                [sys.executable, "-", str(image), "RELEASE"],
                input=validator, text=True, capture_output=True, check=False,
            )
            self.assertEqual(good.returncode, 0, msg=good.stderr)
            self.assertIn("EFI Boot Service Driver", good.stdout)
            data[optional + 68:optional + 70] = (10).to_bytes(2, "little")
            image.write_bytes(data)
            bad = subprocess.run(
                [sys.executable, "-", str(image), "RELEASE"],
                input=validator, text=True, capture_output=True, check=False,
            )
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("subsystem != 11", bad.stderr)

    def test_build_validates_pe32plus_x64_efi_driver_before_copying_outputs(self):
        build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")
        self.assertIn("validate_efi_image()", build)
        fn_start = build.index("validate_efi_image()")
        fn_end = build.index("\n}\n", fn_start) + 3
        fn = build[fn_start:fn_end]
        for token in ("0x8664", "0x20b", "EFI Boot Service Driver", "subsystem != 11"):
            self.assertIn(token, fn)
        build_one = build[build.index("build_one()"):build.index('\nlog "===== ', build.index("build_one()"))]
        validate_pos = build_one.index('validate_efi_image "$efi" "$target"')
        copy_pos = build_one.index('cp "$efi" "$outdir/${APP}.efi"')
        self.assertLess(validate_pos, copy_pos)


class FinalAuditRound34Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")

    @staticmethod
    def _validator_script(build_text):
        marker = "<<'PY_EFI_VALIDATE'"
        marker_pos = build_text.index(marker)
        start = build_text.index("\n", marker_pos) + 1
        end = build_text.index("\nPY_EFI_VALIDATE", start)
        return build_text[start:end]

    @staticmethod
    def _minimal_efi_image():
        # Valid enough PE32+ EFI Boot Service Driver for the release gate.
        data = bytearray(0x400)
        data[0:2] = b"MZ"
        pe = 0x80
        data[0x3C:0x40] = pe.to_bytes(4, "little")
        data[pe:pe + 4] = b"PE\0\0"
        coff = pe + 4
        data[coff:coff + 2] = (0x8664).to_bytes(2, "little")
        data[coff + 2:coff + 4] = (1).to_bytes(2, "little")
        data[coff + 16:coff + 18] = (0xF0).to_bytes(2, "little")
        data[coff + 18:coff + 20] = (0x0002).to_bytes(2, "little")
        optional = pe + 24
        data[optional:optional + 2] = (0x20B).to_bytes(2, "little")
        data[optional + 16:optional + 20] = (0x1000).to_bytes(4, "little")  # entry RVA
        data[optional + 32:optional + 36] = (0x1000).to_bytes(4, "little")  # section alignment
        data[optional + 36:optional + 40] = (0x200).to_bytes(4, "little")   # file alignment
        data[optional + 56:optional + 60] = (0x2000).to_bytes(4, "little") # SizeOfImage
        data[optional + 60:optional + 64] = (0x200).to_bytes(4, "little")  # SizeOfHeaders
        data[optional + 68:optional + 70] = (11).to_bytes(2, "little")
        data[optional + 108:optional + 112] = (16).to_bytes(4, "little")   # NumberOfRvaAndSizes
        section = optional + 0xF0
        data[section:section + 8] = b".text\0\0\0"
        data[section + 8:section + 12] = (0x100).to_bytes(4, "little")      # VirtualSize
        data[section + 12:section + 16] = (0x1000).to_bytes(4, "little")   # VirtualAddress
        data[section + 16:section + 20] = (0x200).to_bytes(4, "little")    # SizeOfRawData
        data[section + 20:section + 24] = (0x200).to_bytes(4, "little")    # PointerToRawData
        data[section + 36:section + 40] = (0x60000020).to_bytes(4, "little")
        return data

    def _run_validator(self, data):
        validator = self._validator_script(self.build)
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "driver.efi"
            image.write_bytes(data)
            return subprocess.run(
                [sys.executable, "-", str(image), "RELEASE"],
                input=validator, text=True, capture_output=True, check=False,
            )

    def test_exact_profile_requires_successfully_read_revision_and_subsystem_identity(self):
        context_start = self.header.index("typedef struct {", self.header.index("HDAU_COEF_INDEX_SNAPSHOT"))
        context_end = self.header.index("} HDAU_CONTEXT;", context_start)
        context = self.header[context_start:context_end]
        self.assertIn("BOOLEAN             RevisionIdValid;", context)
        self.assertIn("BOOLEAN             SubsystemIdValid;", context)

        apply_exact = extract_c_function(self.source, "HdaApplyExactProfileVerbs")
        self.assertRegex(
            apply_exact,
            r"Exact->RevisionMask != 0\s*&&\s*\(!Context->RevisionIdValid\s*\|\|",
        )
        self.assertRegex(
            apply_exact,
            r"Exact->SubsystemMask != 0\s*&&\s*\(!Context->SubsystemIdValid\s*\|\|",
        )
        subsystem_read = apply_exact.index("HDA_VERB_GET_SUBSYSTEM_ID")
        valid_set = apply_exact.index("Context->SubsystemIdValid = !EFI_ERROR(IdentityStatus);", subsystem_read)
        self.assertLess(subsystem_read, valid_set)

        controller = extract_c_function(self.source, "HdaTryController")
        revision_read = controller.index("HDA_PARAM_REV_ID")
        revision_valid = controller.index("Context.RevisionIdValid", revision_read)
        self.assertLess(revision_read, revision_valid)

    def test_exact_profile_snapshot_uses_same_identity_validity_gate(self):
        snapshot = extract_c_function(self.source, "HdaSnapshotCodecState")
        self.assertRegex(
            snapshot,
            r"Exact->RevisionMask != 0\s*&&\s*\(!Context->RevisionIdValid\s*\|\|",
        )
        self.assertRegex(
            snapshot,
            r"Exact->SubsystemMask != 0\s*&&\s*\(!Context->SubsystemIdValid\s*\|\|",
        )

    def test_pin_priority_fails_closed_before_parsing_or_pin_sense_when_config_default_read_fails(self):
        body = extract_c_function(self.source, "HdaPinPriority")
        read = body.index("HDA_VERB_GET_CONFIG_DEFAULT")
        self.assertIn("Status = HdaImmediateCommand", body[max(0, read - 180):read + 120])
        failure = body.index("EFI_ERROR(Status)", read)
        reject = body.index("return -1;", failure)
        parse = body.index("Device = HdaDefaultDevice(Config)")
        sense = body.index("HdaReadPinSense", read)
        self.assertLess(read, failure)
        self.assertLess(failure, reject)
        self.assertLess(reject, parse)
        self.assertLess(reject, sense)

    def test_release_validator_rejects_zero_sections_and_out_of_file_section_raw_data(self):
        zero = self._minimal_efi_image()
        pe = int.from_bytes(zero[0x3C:0x40], "little")
        zero[pe + 6:pe + 8] = (0).to_bytes(2, "little")
        result = self._run_validator(zero)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("section", result.stderr.lower())

        outside = self._minimal_efi_image()
        optional = pe + 24
        section = optional + 0xF0
        outside[section + 20:section + 24] = (0x380).to_bytes(4, "little")
        outside[section + 16:section + 20] = (0x200).to_bytes(4, "little")
        result = self._run_validator(outside)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("raw", result.stderr.lower())

    def test_release_validator_requires_entry_point_in_section_and_directory_count_to_fit_optional_header(self):
        bad_entry = self._minimal_efi_image()
        pe = int.from_bytes(bad_entry[0x3C:0x40], "little")
        optional = pe + 24
        bad_entry[optional + 16:optional + 20] = (0x3000).to_bytes(4, "little")
        result = self._run_validator(bad_entry)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("entry", result.stderr.lower())

        bad_dirs = self._minimal_efi_image()
        bad_dirs[optional + 108:optional + 112] = (17).to_bytes(4, "little")
        result = self._run_validator(bad_dirs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("director", result.stderr.lower())

    def test_release_validator_walks_aligned_win_certificate_entries(self):
        data = self._minimal_efi_image()
        pe = int.from_bytes(data[0x3C:0x40], "little")
        optional = pe + 24
        cert_offset = 0x400
        cert = bytearray(16)
        cert[0:4] = (16).to_bytes(4, "little")
        cert[4:6] = (0x0200).to_bytes(2, "little")
        cert[6:8] = (0x0002).to_bytes(2, "little")
        cert[8:16] = b"12345678"
        data.extend(cert)
        data[optional + 144:optional + 148] = cert_offset.to_bytes(4, "little")
        data[optional + 148:optional + 152] = len(cert).to_bytes(4, "little")
        result = self._run_validator(data)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Authenticode=present", result.stdout)

        malformed = bytearray(data)
        malformed[cert_offset:cert_offset + 4] = (4).to_bytes(4, "little")
        result = self._run_validator(malformed)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("certificate", result.stderr.lower())


    def test_parameter_transport_failures_cannot_masquerade_as_zero_capabilities_in_dac_selection(self):
        helper = extract_c_function(self.source, "HdaGetParamChecked")
        self.assertIn("HdaImmediateCommand", helper)
        self.assertIn("HDA_VERB_GET_PARAMETER", helper)
        self.assertIn("return Status;", helper)

        for function_name in ("HdaIsAudioOutputDac", "HdaSelectPcmFormat"):
            body = extract_c_function_definition(self.source, function_name)
            self.assertGreaterEqual(body.count("HdaGetParamChecked"), 3, function_name)
            self.assertIn("EFI_ERROR(Status)", body, function_name)

    def test_mutating_path_programming_propagates_required_capability_read_errors(self):
        for function_name in (
            "HdaSetWidgetPowerD0",
            "HdaProgramSelectedInputAmp",
            "HdaProgramAmpSafeUnity",
            "HdaProgramConnectionPath",
            "HdaProgramOutputPin",
            "HdaProgramDigitalOutputPath",
        ):
            body = extract_c_function(self.source, function_name)
            self.assertIn("HdaGetParamChecked", body, function_name)
            read = body.index("HdaGetParamChecked")
            self.assertIn("EFI_ERROR(Status)", body[read:read + 700], function_name)

    def test_snapshot_does_not_classify_failed_widget_caps_as_audio_output(self):
        body = extract_c_function(self.source, "HdaSnapshotOneNode")
        checked = body.index("HdaGetParamChecked")
        failure = body.index("EFI_ERROR(Status)", checked)
        type_decode = body.index("HdaWidgetType", checked)
        self.assertLess(checked, failure)
        self.assertLess(failure, type_decode)

    def test_fanout_selector_safety_fails_closed_when_widget_caps_are_unreadable(self):
        path_guard = extract_c_function_definition(self.source, "HdaPathContainsPinIntermediate")
        self.assertIn("HdaGetParamChecked", path_guard)
        checked = path_guard.index("HdaGetParamChecked")
        self.assertRegex(path_guard[checked:checked + 500], r"EFI_ERROR\(Status\)[\s\S]*?return TRUE;")

        reset = extract_c_function_definition(self.source, "HdaResetFanoutSelectorClaims")
        self.assertIn("HdaGetParamChecked", reset)
        self.assertRegex(reset, r"EFI_ERROR\(Status\)[\s\S]*?mHdaFanoutSelectorClaimsValid = FALSE;[\s\S]*?return;")

        compatible = extract_c_function_definition(self.source, "HdaPathsSelectorCompatible")
        self.assertIn("HdaGetParamChecked", compatible)
        self.assertRegex(compatible, r"EFI_ERROR\(Status\)[\s\S]*?return FALSE;")

        commit = extract_c_function_definition(self.source, "HdaCommitFanoutSelectorClaims")
        self.assertIn("HdaGetParamChecked", commit)
        self.assertRegex(commit, r"EFI_ERROR\(Status\)[\s\S]*?mHdaFanoutSelectorClaimsValid = FALSE;[\s\S]*?return;")

    def test_amp_capability_transport_errors_are_propagated_before_gain_programming(self):
        helper = extract_c_function_definition(self.source, "HdaGetEffectiveAmpCapsChecked")
        self.assertIn("HdaGetParamChecked", helper)

        for function_name in (
            "HdaProgramSelectedInputAmp",
            "HdaProgramAmpSafeUnity",
            "HdaProgramStoredHardwareVolume",
            "HdaTryHardwareOutputVolume",
            "HdaTryHardwareInputVolume",
        ):
            body = extract_c_function_definition(self.source, function_name)
            self.assertIn("HdaGetEffectiveAmpCapsChecked", body, function_name)
            pos = body.index("HdaGetEffectiveAmpCapsChecked")
            self.assertIn("EFI_ERROR(Status)", body[pos:pos + 650], function_name)

    def test_route_classification_rejects_unreadable_pin_capabilities(self):
        body = extract_c_function_definition(self.source, "HdaClassifySelectedRoute")
        self.assertIn("HdaGetParamChecked", body)
        pos = body.index("HdaGetParamChecked")
        window = body[pos:pos + 700]
        self.assertIn("EFI_ERROR(Status)", window)
        self.assertIn("Context->DigitalRoute = FALSE", window)
        self.assertIn("return FALSE", window)

    def test_snapshot_node_is_published_only_after_complete_capture_and_pin_caps_are_known(self):
        body = extract_c_function_definition(self.source, "HdaSnapshotOneNode")
        self.assertIn("Snap = &Context->SnapshotNodes[Context->SnapshotNodeCount];", body)
        self.assertNotIn("SnapshotNodeCount++]", body)
        self.assertIn("HDA_PARAM_PIN_CAP", body)
        pin_cap = body.index("HDA_PARAM_PIN_CAP")
        self.assertIn("HdaGetParamChecked", body[max(0, pin_cap - 180):pin_cap + 120])
        increment = body.rfind("Context->SnapshotNodeCount++;")
        self.assertGreater(increment, 0)
        self.assertGreater(increment, body.rfind("HdaImmediateCommand"))
        self.assertLess(increment, body.rfind("return EFI_SUCCESS;"))

    def test_warm_reset_snapshot_aborts_on_unreadable_widget_caps(self):
        body = extract_c_function_definition(self.source, "HdaSnapshotWarmResetPower")
        self.assertIn("HdaGetParamChecked", body)
        pos = body.index("HdaGetParamChecked")
        self.assertIn("EFI_ERROR(Status)", body[pos:pos + 450])
        self.assertIn("return Status", body[pos:pos + 450])

    def test_transactional_snapshot_never_skips_required_eapd_or_input_amp_state_on_read_failure(self):
        one = extract_c_function_definition(self.source, "HdaSnapshotOneNode")
        self.assertIn("if (NextNid != 0 && !HaveInputIndex) return EFI_NOT_READY;", one)

        path = extract_c_function_definition(self.source, "HdaSnapshotPathNodes")
        pin = path.index("if (Path[Index] == Pin)")
        window = path[pin:pin + 650]
        self.assertIn("HdaGetParamChecked", window)
        self.assertIn("HDA_PARAM_PIN_CAP", window)
        self.assertIn("EFI_ERROR(Status)", window)
        self.assertIn("return Status", window)


class FinalAuditRound35Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")

    @staticmethod
    def _validator_script(build_text):
        marker = "<<'PY_EFI_VALIDATE'"
        marker_pos = build_text.index(marker)
        start = build_text.index("\n", marker_pos) + 1
        end = build_text.index("\nPY_EFI_VALIDATE", start)
        return build_text[start:end]

    @staticmethod
    def _minimal_efi_image():
        data = bytearray(0x400)
        data[0:2] = b"MZ"
        pe = 0x80
        data[0x3C:0x40] = pe.to_bytes(4, "little")
        data[pe:pe + 4] = b"PE\0\0"
        coff = pe + 4
        data[coff:coff + 2] = (0x8664).to_bytes(2, "little")
        data[coff + 2:coff + 4] = (1).to_bytes(2, "little")
        data[coff + 16:coff + 18] = (0xF0).to_bytes(2, "little")
        data[coff + 18:coff + 20] = (0x0002).to_bytes(2, "little")
        optional = pe + 24
        data[optional:optional + 2] = (0x20B).to_bytes(2, "little")
        data[optional + 16:optional + 20] = (0x1000).to_bytes(4, "little")
        data[optional + 32:optional + 36] = (0x1000).to_bytes(4, "little")
        data[optional + 36:optional + 40] = (0x200).to_bytes(4, "little")
        data[optional + 56:optional + 60] = (0x2000).to_bytes(4, "little")
        data[optional + 60:optional + 64] = (0x200).to_bytes(4, "little")
        data[optional + 68:optional + 70] = (11).to_bytes(2, "little")
        data[optional + 108:optional + 112] = (16).to_bytes(4, "little")
        section = optional + 0xF0
        data[section:section + 8] = b".text\0\0\0"
        data[section + 8:section + 12] = (0x100).to_bytes(4, "little")
        data[section + 12:section + 16] = (0x1000).to_bytes(4, "little")
        data[section + 16:section + 20] = (0x200).to_bytes(4, "little")
        data[section + 20:section + 24] = (0x200).to_bytes(4, "little")
        data[section + 36:section + 40] = (0x60000020).to_bytes(4, "little")
        return data

    def _run_validator(self, data):
        validator = self._validator_script(self.build)
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "driver.efi"
            image.write_bytes(data)
            return subprocess.run(
                [sys.executable, "-", str(image), "RELEASE"],
                input=validator, text=True, capture_output=True, check=False,
            )

    def test_release_validator_rejects_certificate_table_overlapping_section_raw_data(self):
        data = self._minimal_efi_image()
        pe = int.from_bytes(data[0x3C:0x40], "little")
        optional = pe + 24
        # The .text raw range is [0x200, 0x400). A certificate table is file data,
        # not an RVA, and must live outside the mapped image section data.
        cert_offset = 0x300
        cert_size = 0x10
        data[cert_offset:cert_offset + 4] = cert_size.to_bytes(4, "little")
        data[cert_offset + 4:cert_offset + 6] = (0x0200).to_bytes(2, "little")
        data[cert_offset + 6:cert_offset + 8] = (0x0002).to_bytes(2, "little")
        data[optional + 144:optional + 148] = cert_offset.to_bytes(4, "little")
        data[optional + 148:optional + 152] = cert_size.to_bytes(4, "little")

        result = self._run_validator(data)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertRegex(result.stderr.lower(), r"certificate|section|overlap")


    def test_release_validator_rejects_section_raw_data_overlapping_headers_or_another_section(self):
        header_overlap = self._minimal_efi_image()
        pe = int.from_bytes(header_overlap[0x3C:0x40], "little")
        optional = pe + 24
        section = optional + 0xF0
        header_overlap[section + 20:section + 24] = (0x100).to_bytes(4, "little")
        result = self._run_validator(header_overlap)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertRegex(result.stderr.lower(), r"section|header|overlap")

        overlap = self._minimal_efi_image()
        coff = pe + 4
        overlap[coff + 2:coff + 4] = (2).to_bytes(2, "little")
        overlap[optional + 56:optional + 60] = (0x3000).to_bytes(4, "little")
        second = section + 40
        overlap[second:second + 8] = b".data\0\0\0"
        overlap[second + 8:second + 12] = (0x100).to_bytes(4, "little")
        overlap[second + 12:second + 16] = (0x2000).to_bytes(4, "little")
        overlap[second + 16:second + 20] = (0x200).to_bytes(4, "little")
        overlap[second + 20:second + 24] = (0x200).to_bytes(4, "little")
        overlap[second + 36:second + 40] = (0xC0000040).to_bytes(4, "little")
        result = self._run_validator(overlap)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertRegex(result.stderr.lower(), r"section|overlap")


    def test_release_validator_requires_executable_image_characteristic(self):
        data = self._minimal_efi_image()
        pe = int.from_bytes(data[0x3C:0x40], "little")
        coff = pe + 4
        # IMAGE_FILE_EXECUTABLE_IMAGE (0x0002) is mandatory for a runnable image.
        data[coff + 18:coff + 20] = (0).to_bytes(2, "little")
        result = self._run_validator(data)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertRegex(result.stderr.lower(), r"executable|characteristic")

    def test_release_validator_rejects_invalid_file_and_section_alignment(self):
        bad_file_alignment = self._minimal_efi_image()
        pe = int.from_bytes(bad_file_alignment[0x3C:0x40], "little")
        optional = pe + 24
        # FileAlignment must be a power of two in the image-supported range.
        bad_file_alignment[optional + 36:optional + 40] = (0x300).to_bytes(4, "little")
        result = self._run_validator(bad_file_alignment)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertRegex(result.stderr.lower(), r"alignment")

        bad_section_offset = self._minimal_efi_image()
        section = optional + 0xF0
        bad_section_offset[section + 20:section + 24] = (0x280).to_bytes(4, "little")
        result = self._run_validator(bad_section_offset)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertRegex(result.stderr.lower(), r"alignment|section")

    def test_fallback_candidate_classification_rejects_unreadable_pin_capabilities(self):
        source = SOURCE_PATH.read_text(encoding="utf-8")
        body = extract_c_function_definition(source, "HdaAddRouteCandidate")
        self.assertIn("HdaGetParamChecked", body)
        pin = body.index("HDA_PARAM_PIN_CAP")
        checked = body.rfind("HdaGetParamChecked", 0, pin)
        self.assertGreaterEqual(checked, 0)
        failure = body.index("EFI_ERROR(Status)", pin)
        reject = body.index("return;", failure)
        classify = body.index("Hdmi =", pin)
        self.assertLess(pin, failure)
        self.assertLess(failure, reject)
        self.assertLess(reject, classify)

    def test_input_amp_route_prefilter_fails_closed_when_widget_caps_are_unreadable(self):
        source = SOURCE_PATH.read_text(encoding="utf-8")
        body = extract_c_function_definition(source, "HdaPathInputAmpsProgrammable")
        self.assertIn("HdaGetParamChecked", body)
        pos = body.index("HdaGetParamChecked")
        window = body[pos:pos + 500]
        self.assertIn("EFI_ERROR(Status)", window)
        self.assertIn("return FALSE", window)



    def test_selected_route_classification_enforces_digital_policy_from_live_pin_caps(self):
        source = SOURCE_PATH.read_text(encoding="utf-8")
        body = extract_c_function_definition(source, "HdaClassifySelectedRoute")
        self.assertIn("Context->DigitalRoute", body)
        self.assertIn("mHdaCfg.AllowDigitalPins", body)
        self.assertIn("mHdaCfg.ForcePin", body)
        classify = body.index("Context->DigitalRoute =")
        policy = body.index("!mHdaCfg.AllowDigitalPins", classify)
        reject = body.index("return FALSE", policy)
        success = body.rfind("return TRUE")
        self.assertLess(classify, policy)
        self.assertLess(policy, reject)
        self.assertLess(reject, success)


    def test_analog_path_programming_refuses_live_digital_pin_before_first_mutation(self):
        source = SOURCE_PATH.read_text(encoding="utf-8")
        body = extract_c_function_definition(source, "HdaProgramAnalogOutputPath")
        self.assertIn("HDA_PARAM_PIN_CAP", body)
        self.assertIn("HDA_VERB_GET_CONFIG_DEFAULT", body)
        self.assertIn("HDA_PIN_CAP_HDMI", body)
        self.assertIn("HDA_PIN_CAP_DISPLAYPORT", body)
        self.assertIn("mHdaCfg.AllowDigitalPins", body)
        cap = body.index("HDA_PARAM_PIN_CAP")
        config = body.index("HDA_VERB_GET_CONFIG_DEFAULT", cap)
        policy = body.index("!mHdaCfg.AllowDigitalPins", config)
        mutation = body.index("HdaSetWidgetPowerD0", policy)
        self.assertLess(cap, config)
        self.assertLess(config, policy)
        self.assertLess(policy, mutation)


class FinalAuditRound37Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")

    @staticmethod
    def _validator_script(build_text):
        marker = "<<'PY_EFI_VALIDATE'"
        marker_pos = build_text.index(marker)
        start = build_text.index("\n", marker_pos) + 1
        end = build_text.index("\nPY_EFI_VALIDATE", start)
        return build_text[start:end]

    def test_release_validator_accepts_valid_subpage_edk2_xcode5_alignment(self):
        # mtoc/GenFw on macOS can emit valid UEFI PE32+ images using 0x20
        # SectionAlignment/FileAlignment. For sub-page alignment PE requires the
        # on-disk section offset to equal its RVA. This models the real R36 log.
        data = bytearray(0x400)
        data[0:2] = b"MZ"
        pe = 0x80
        data[0x3C:0x40] = pe.to_bytes(4, "little")
        data[pe:pe + 4] = b"PE\0\0"
        coff = pe + 4
        data[coff:coff + 2] = (0x8664).to_bytes(2, "little")
        data[coff + 2:coff + 4] = (1).to_bytes(2, "little")
        data[coff + 16:coff + 18] = (0xF0).to_bytes(2, "little")
        data[coff + 18:coff + 20] = (0x0002).to_bytes(2, "little")
        optional = pe + 24
        data[optional:optional + 2] = (0x20B).to_bytes(2, "little")
        data[optional + 16:optional + 20] = (0x200).to_bytes(4, "little")
        data[optional + 32:optional + 36] = (0x20).to_bytes(4, "little")
        data[optional + 36:optional + 40] = (0x20).to_bytes(4, "little")
        data[optional + 56:optional + 60] = (0x400).to_bytes(4, "little")
        data[optional + 60:optional + 64] = (0x200).to_bytes(4, "little")
        data[optional + 68:optional + 70] = (11).to_bytes(2, "little")
        data[optional + 108:optional + 112] = (16).to_bytes(4, "little")
        section = optional + 0xF0
        data[section:section + 8] = b".text\0\0\0"
        data[section + 8:section + 12] = (0x100).to_bytes(4, "little")
        data[section + 12:section + 16] = (0x200).to_bytes(4, "little")
        data[section + 16:section + 20] = (0x200).to_bytes(4, "little")
        data[section + 20:section + 24] = (0x200).to_bytes(4, "little")
        data[section + 36:section + 40] = (0x60000020).to_bytes(4, "little")

        validator = self._validator_script(self.build)
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "driver.efi"
            image.write_bytes(data)
            result = subprocess.run(
                [sys.executable, "-", str(image), "DEBUG"],
                input=validator, text=True, capture_output=True, check=False,
            )
        self.assertEqual(result.returncode, 0, msg=result.stderr or result.stdout)
        self.assertIn("Validated DEBUG EFI: PE32+ X64", result.stdout)

    def test_release_validator_rejects_subpage_alignment_when_file_offset_differs_from_rva(self):
        # The sub-page exception is not a blanket relaxation: section raw offsets
        # must match their RVAs when SectionAlignment is below the page size.
        data = bytearray(0x600)
        data[0:2] = b"MZ"
        pe = 0x80
        data[0x3C:0x40] = pe.to_bytes(4, "little")
        data[pe:pe + 4] = b"PE\0\0"
        coff = pe + 4
        data[coff:coff + 2] = (0x8664).to_bytes(2, "little")
        data[coff + 2:coff + 4] = (1).to_bytes(2, "little")
        data[coff + 16:coff + 18] = (0xF0).to_bytes(2, "little")
        data[coff + 18:coff + 20] = (0x0002).to_bytes(2, "little")
        optional = pe + 24
        data[optional:optional + 2] = (0x20B).to_bytes(2, "little")
        data[optional + 16:optional + 20] = (0x200).to_bytes(4, "little")
        data[optional + 32:optional + 36] = (0x20).to_bytes(4, "little")
        data[optional + 36:optional + 40] = (0x20).to_bytes(4, "little")
        data[optional + 56:optional + 60] = (0x400).to_bytes(4, "little")
        data[optional + 60:optional + 64] = (0x200).to_bytes(4, "little")
        data[optional + 68:optional + 70] = (11).to_bytes(2, "little")
        data[optional + 108:optional + 112] = (16).to_bytes(4, "little")
        section = optional + 0xF0
        data[section:section + 8] = b".text\0\0\0"
        data[section + 8:section + 12] = (0x100).to_bytes(4, "little")
        data[section + 12:section + 16] = (0x200).to_bytes(4, "little")
        data[section + 16:section + 20] = (0x200).to_bytes(4, "little")
        data[section + 20:section + 24] = (0x400).to_bytes(4, "little")
        data[section + 36:section + 40] = (0x60000020).to_bytes(4, "little")

        validator = self._validator_script(self.build)
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "driver.efi"
            image.write_bytes(data)
            result = subprocess.run(
                [sys.executable, "-", str(image), "DEBUG"],
                input=validator, text=True, capture_output=True, check=False,
            )
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertRegex(result.stderr.lower(), r"offset|rva|sub-page|alignment")

    def test_r37_release_metadata_is_synchronized_and_r36_history_is_preserved(self):
        header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', self.build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))
        self.assertIn("## 1.0.82 — final audit R37", changelog)
        self.assertIn("## 1.0.81 — final audit R36", changelog)

if __name__ == "__main__":
    unittest.main(verbosity=2)

class FinalAuditRound36Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")

    def test_build_scrubs_transient_macos_metadata_before_hygiene_and_each_regression_round(self):
        self.assertIn("clean_transient_source_metadata()", self.build)
        start = self.build.index("clean_transient_source_metadata()")
        end = self.build.index("preflight_tools()", start)
        cleaner = self.build[start:end]
        self.assertIn(".DS_Store", cleaner)
        self.assertIn("._*", cleaner)
        self.assertIn("-path './edk2' -prune", cleaner)

        self.assertIn(
            "preflight_tools\nclean_transient_source_metadata\nverify_source_tree_hygiene\nrun_source_regressions",
            self.build,
        )
        regressions = self.build[self.build.index("run_source_regressions()"):]
        loop = regressions[regressions.index('while [ "$round" -le "$SOURCE_TEST_ROUNDS" ]'):]
        self.assertLess(loop.index("clean_transient_source_metadata"), loop.index('log "Regression/stress round'))

    def test_hygiene_gate_still_rejects_macos_metadata_if_cleanup_does_not_remove_it(self):
        start = self.build.index("verify_source_tree_hygiene()")
        end = self.build.index("preflight_tools()", start)
        hygiene = self.build[start:end]
        self.assertIn(".DS_Store", hygiene)
        self.assertIn("._*", hygiene)

    def test_r36_release_metadata_is_synchronized_and_r35_history_is_preserved(self):
        header = (ROOT / "HDAUniversalBootChimeDxe" / "HdaUefi.h").read_text(encoding="utf-8")
        inf = (ROOT / "HDAUniversalBootChimeDxe" / "HDAUniversalBootChimeDxe.inf").read_text(encoding="utf-8")
        dsc = (ROOT / "HDAUniversalBootChimeDxePkg.dsc").read_text(encoding="utf-8")
        readme = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        report = (ROOT / "docs" / "PROJECT-REPORT.txt").read_text(encoding="utf-8")
        self.assertIn('HDAU_CHIME_VERSION            L"1.0.87-final-audit"', header)
        self.assertIn("VERSION_STRING                 = 1.0.87", inf)
        self.assertIn("PLATFORM_VERSION               = 1.0.87", dsc)
        self.assertIn('VERSION="1.0.87-final-audit"', self.build)
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertTrue(report.startswith("HDAUniversalBootChimeDxe 1.0.82-final-audit R37"))
        self.assertIn("## 1.0.81 — final audit R36", changelog)
        self.assertIn("## 1.0.80 — final audit R35", changelog)


class ReleaseGateRound40Tests(unittest.TestCase):
    """Execute the production PE/COFF validator against malformed EFI images."""

    @classmethod
    def setUpClass(cls):
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")

    _validator_script = staticmethod(FinalAuditRound34Tests._validator_script)

    def _run_validator(self, data):
        return FinalAuditRound34Tests._run_validator(self, data)

    def _base(self):
        return bytearray(FinalAuditRound34Tests._minimal_efi_image())

    def test_rejects_entry_in_nonexecutable_section(self):
        data = self._base()
        section = 0x80 + 24 + 0xF0
        data[section + 36:section + 40] = (0xC0000040).to_bytes(4, "little")
        result = self._run_validator(data)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)

    def test_rejects_entry_in_zero_filled_unbacked_section_tail(self):
        data = self._base()
        optional = 0x80 + 24
        section = optional + 0xF0
        data[optional + 16:optional + 20] = (0x1250).to_bytes(4, "little")
        data[section + 8:section + 12] = (0x300).to_bytes(4, "little")
        result = self._run_validator(data)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)

    def test_rejects_overlapping_virtual_sections(self):
        data = self._base()
        data.extend(b"\x00" * 0x400)
        coff = 0x80 + 4
        optional = 0x80 + 24
        section = optional + 0xF0
        data[coff + 2:coff + 4] = (2).to_bytes(2, "little")
        data[optional + 56:optional + 60] = (0x3000).to_bytes(4, "little")
        data[section + 8:section + 12] = (0x1300).to_bytes(4, "little")
        section2 = section + 40
        data[section2:section2 + 8] = b".data\x00\x00\x00"
        for offset, value in ((8, 0x100), (12, 0x2000), (16, 0x200),
                              (20, 0x400), (36, 0xC0000040)):
            data[section2 + offset:section2 + offset + 4] = value.to_bytes(4, "little")
        result = self._run_validator(data)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)

    def test_rejects_descending_virtual_sections(self):
        data = self._base()
        data.extend(b"\x00" * 0x400)
        coff = 0x80 + 4
        optional = 0x80 + 24
        section = optional + 0xF0
        data[coff + 2:coff + 4] = (2).to_bytes(2, "little")
        data[optional + 56:optional + 60] = (0x4000).to_bytes(4, "little")
        data[optional + 16:optional + 20] = (0x3000).to_bytes(4, "little")
        data[section + 12:section + 16] = (0x3000).to_bytes(4, "little")
        section2 = section + 40
        data[section2:section2 + 8] = b".data\x00\x00\x00"
        for offset, value in ((8, 0x100), (12, 0x2000), (16, 0x200),
                              (20, 0x400), (36, 0xC0000040)):
            data[section2 + offset:section2 + offset + 4] = value.to_bytes(4, "little")
        result = self._run_validator(data)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)

    def test_rejects_reversed_raw_section_order(self):
        data = self._base()
        data.extend(b"\x00" * 0x200)
        coff = 0x80 + 4
        optional = 0x80 + 24
        section = optional + 0xF0
        data[coff + 2:coff + 4] = (2).to_bytes(2, "little")
        data[optional + 56:optional + 60] = (0x3000).to_bytes(4, "little")
        data[section + 20:section + 24] = (0x400).to_bytes(4, "little")
        section2 = section + 40
        data[section2:section2 + 8] = b".data\x00\x00\x00"
        for offset, value in ((8, 0x100), (12, 0x2000), (16, 0x200),
                              (20, 0x200), (36, 0xC0000040)):
            data[section2 + offset:section2 + offset + 4] = value.to_bytes(4, "little")
        result = self._run_validator(data)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)


class AppleAlcIdentityRound41Tests(unittest.TestCase):
    """Keep invalid optional codec identities from becoming active profiles."""

    def test_malformed_and_out_of_range_identity_fields_do_not_emit_verbs(self):
        # All four identity components currently pass through as_int() and a
        # 32-bit mask. Invalid strings may become "absent" and >32-bit values
        # alias real hardware identifiers; no verbs may be emitted for either.
        with tempfile.TemporaryDirectory() as directory:
            for field in ("RevisionID", "RevisionMask", "SubsystemID", "SubsystemMask"):
                for index, value in enumerate(("not-a-number", 0x100000000, -0x80000001)):
                    with self.subTest(field=field, value=value):
                        config = exact_entry(0x01471C10, 1, 0xFFFFFFFF, 1, 0xFFFFFFFF)
                        config[field] = value
                        archive = Path(directory) / f"{field}-{index}.zip"
                        make_archive(archive, [config])
                        _text, stats = GENERATOR.generate(archive)
                        self.assertEqual(stats["config_invalid"], 1)
                        self.assertEqual(stats["config_profiles"], 0)
                        self.assertEqual(stats["exact_profiles"], 0)

    def test_optional_signed_32bit_identity_values_still_work(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "signed-identity.zip"
            make_archive(archive, [exact_entry(0x01471C10, -1, -1, -0x80000000, -1)])
            text, stats = GENERATOR.generate(archive)
            self.assertEqual(stats["config_invalid"], 0)
            self.assertEqual(stats["exact_profiles"], 1)
            self.assertIn("0xFFFFFFFF, 0xFFFFFFFF, 0x80000000, 0xFFFFFFFF", text)


class PeSectionHeadersRound41Tests(unittest.TestCase):
    """Confirm that EFI image sections never overwrite the loaded PE headers."""

    @classmethod
    def setUpClass(cls):
        cls.build = (ROOT / "Build-HDAUniversalBootChimeDxe.command").read_text(encoding="utf-8")

    _validator_script = staticmethod(FinalAuditRound34Tests._validator_script)

    def _run_validator(self, data):
        return FinalAuditRound34Tests._run_validator(self, data)

    def test_section_virtual_span_must_not_overlap_headers(self):
        data = bytearray(FinalAuditRound34Tests._minimal_efi_image())
        optional = 0x80 + 24
        section = optional + 0xF0
        # PE headers load into RVA [0, 0x200).  A section mapped at RVA 0
        # would overwrite them even though its raw data comes from file 0x200.
        data[section + 12:section + 16] = (0).to_bytes(4, "little")
        data[optional + 16:optional + 20] = (0x100).to_bytes(4, "little")
        result = self._run_validator(data)
        self.assertNotEqual(result.returncode, 0, msg=result.stdout)


class AppleAlcAtomicPermissionsRound42Tests(unittest.TestCase):
    """Atomic header regeneration must not silently change existing file permissions."""

    def test_atomic_replacement_preserves_existing_header_mode(self):
        import stat

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "HdaAppleAlcLayouts.h"
            output.write_text("previous\n", encoding="utf-8")
            for mode in (0o644, 0o640, 0o444):
                with self.subTest(mode=oct(mode)):
                    output.chmod(mode)
                    GENERATOR.write_output_atomic(output, "updated")
                    self.assertEqual(output.read_bytes(), b"updated\n")
                    self.assertEqual(stat.S_IMODE(output.stat().st_mode), mode)
            self.assertEqual(list(Path(directory).glob(".HdaAppleAlcLayouts.h.*.tmp")), [])

    def test_permission_preservation_failure_keeps_previous_output_and_cleans_temp(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "HdaAppleAlcLayouts.h"
            output.write_text("previous\n", encoding="utf-8")
            output.chmod(0o644)
            with mock.patch.object(GENERATOR.os, "fchmod", side_effect=OSError("permissions denied")):
                with self.assertRaisesRegex(RuntimeError, "could not write generated output atomically"):
                    GENERATOR.write_output_atomic(output, "updated")
            self.assertEqual(output.read_bytes(), b"previous\n")
            self.assertEqual(list(Path(directory).glob(".HdaAppleAlcLayouts.h.*.tmp")), [])

class ProjectOverviewReadmeTests(unittest.TestCase):
    """The public-facing overview stays concise; audit history lives under docs."""

    def test_root_readme_is_a_short_project_description(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertTrue(readme.startswith("# HDAUniversalBootChimeDxe\n\n"))
        self.assertIn("UEFI DXE driver", readme)
        self.assertLessEqual(len(readme.splitlines()), 5)
        self.assertNotIn("\n## ", readme)

    def test_archived_technical_readme_keeps_historical_content(self):
        history = (ROOT / "docs" / "PROJECT-HISTORY.md").read_text(encoding="utf-8")
        self.assertTrue(history.startswith("# HDAUniversalBootChimeDxe 1.0.82-final-audit"))
        self.assertIn("1.0.61 final audit R16", history)


