#!/usr/bin/env python3
"""Generate compact pre-boot HDA data from an AppleALC source archive.

Copyright © 2026 DNMTechLabs <dnmtechlabs@gmail.com>

The generator performs two exact joins:
1. Resources/<codec>/Info.plist layout -> platform -> PathMapID, producing
   pin/DAC hints that are always validated against the live codec graph.
2. PinConfigs.kext HDAConfigDefault entries -> exact codec/layout, producing
   AppleALC ConfigData verbs without hand-authored board or codec constants.

Only ConfigData entries that also have a generated runtime path profile are
emitted. Duplicate exact keys are accepted only when their verb streams are
identical; conflicting duplicates are rejected instead of selecting one
arbitrarily.
"""
from __future__ import annotations

import argparse
import os
import plistlib
import re
import stat
import tempfile
import zipfile
import zlib
from xml.parsers.expat import ExpatError
from collections import Counter, OrderedDict
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

PAIR_COUNT = 12
ROUTE_COUNT = 32
MAX_ARCHIVE_MEMBERS = 20_000
MAX_PLIST_BYTES = 16 * 1024 * 1024
MAX_ARCHIVE_UNCOMPRESSED_BYTES = 256 * 1024 * 1024
MAX_ARCHIVE_FILE_BYTES = 512 * 1024 * 1024
MAX_PLIST_OBJECTS = 250_000
MAX_PLIST_DEPTH = 256
MAX_CONFIG_VERBS_PER_PROFILE = 0xFFFF
MAX_EXACT_PROFILES_PER_CODEC_LAYOUT = 2048


def validate_plist_tree(value: Any) -> None:
    """Bound object count/depth without recursive traversal."""
    stack: list[tuple[Any, int]] = [(value, 0)]
    objects = 0
    while stack:
        current, depth = stack.pop()
        objects += 1
        if objects > MAX_PLIST_OBJECTS:
            raise RuntimeError(f"plist object count exceeds safety limit {MAX_PLIST_OBJECTS}")
        if depth > MAX_PLIST_DEPTH:
            raise RuntimeError(f"plist nesting exceeds safety limit {MAX_PLIST_DEPTH}")
        if isinstance(current, dict):
            stack.extend((item, depth + 1) for item in current.values())
        elif isinstance(current, list):
            stack.extend((item, depth + 1) for item in current)


class PlistParseError(RuntimeError):
    """Malformed plist input, distinct from structural safety-limit failures."""


PLIST_PARSE_ERRORS = (
    AttributeError,
    LookupError,
    OverflowError,
    RecursionError,
    TypeError,
    UnicodeError,
    ValueError,
    ExpatError,
    plistlib.InvalidFileException,
)


def parse_plist_bytes(data: bytes) -> Any:
    """Normalize malformed plist parser failures into the generator contract."""
    try:
        return plistlib.loads(data)
    except PLIST_PARSE_ERRORS as error:
        raise PlistParseError(f"invalid AppleALC plist: {error}") from None


def load_plist_fragment(data: bytes) -> Any:
    stripped = data.lstrip()
    if stripped.startswith(b"<?xml") or stripped.startswith(b"bplist"):
        value = parse_plist_bytes(stripped)
    else:
        wrapper = (
            b'<?xml version="1.0" encoding="UTF-8"?>\n'
            b'<plist version="1.0">\n' + data + b'\n</plist>\n'
        )
        value = parse_plist_bytes(wrapper)
    validate_plist_tree(value)
    return value


def load_plist(data: bytes) -> Any:
    value = parse_plist_bytes(data)
    validate_plist_tree(value)
    return value

def as_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return None
        try:
            return int(text, 0)
        except ValueError:
            return None
    return None


def leaf_node_chains(value: Any) -> Iterable[list[int]]:
    # Iterative traversal prevents Python recursion exhaustion on hostile or
    # simply pathological plist nesting while preserving source order.
    stack: list[Any] = [value]
    while stack:
        current = stack.pop()
        if isinstance(current, list):
            if current and all(isinstance(item, dict) and "NodeID" in item for item in current):
                chain: list[int] = []
                for item in current:
                    node = as_int(item.get("NodeID"))
                    if node is None or not 0 < node <= 0xFF:
                        chain = []
                        break
                    chain.append(node)
                if len(chain) >= 2:
                    yield chain
                continue
            stack.extend(reversed(current))
        elif isinstance(current, dict):
            stack.extend(reversed(list(current.values())))

def c_string(text: str) -> str:
    printable = "".join(" " if ord(char) < 0x20 or ord(char) == 0x7F else char for char in text)
    normalized = " ".join(printable.split())[:150]
    return normalized.replace("\\", "\\\\").replace('"', '\\"')


def resource_member(folder: str, path: str) -> str:
    normalized = path[:-5] if path.endswith(".zlib") else path
    relative = PurePosixPath(normalized)
    if (
        not normalized
        or "\\" in normalized
        or relative.is_absolute()
        or relative.as_posix() != normalized
        or any(part in ("", ".", "..") for part in relative.parts)
    ):
        raise RuntimeError(f"unsafe AppleALC resource path: {path!r}")
    return f"{folder}/{relative.as_posix()}"


def read_plist_member(zf: zipfile.ZipFile, name: str) -> bytes:
    try:
        info = zf.getinfo(name)
    except KeyError as error:
        raise RuntimeError(f"AppleALC plist member is missing: {name}") from error
    if info.flag_bits & 0x1:
        raise RuntimeError(f"encrypted AppleALC plist is unsupported: {name}")
    if info.file_size > MAX_PLIST_BYTES:
        raise RuntimeError(
            f"AppleALC plist exceeds the {MAX_PLIST_BYTES}-byte safety limit: {name}"
        )
    try:
        return zf.read(info)
    except (OSError, EOFError, NotImplementedError, RuntimeError, zipfile.BadZipFile, zlib.error) as error:
        raise RuntimeError(f"could not read AppleALC plist {name}: {error}") from None


def resource_root(names: list[str]) -> str:
    roots = {
        match.group(1)
        for name in names
        if (match := re.match(r"(.*/Resources/)[^/]+/Info\.plist$", name)) is not None
    }
    if not roots:
        raise RuntimeError("AppleALC Resources/*/Info.plist entries were not found")
    if len(roots) != 1:
        joined = ", ".join(sorted(roots))
        raise RuntimeError(f"ambiguous AppleALC resource roots in archive: {joined}")
    return next(iter(roots))


def pinconfigs_member(names: list[str], root: str) -> str:
    expected = f"{root}PinConfigs.kext/Contents/Info.plist"
    matches = sorted(name for name in names if name == expected)
    if not matches:
        raise RuntimeError(f"AppleALC PinConfigs plist was not found under selected root: {expected}")
    if len(matches) != 1:
        raise RuntimeError(f"ambiguous AppleALC PinConfigs plist under selected root: {expected}")
    return matches[0]


def config_name(entry: dict[str, Any], codec_id: int, layout_id: int) -> str:
    for key in ("Codec", "CodecName", "Comment"):
        value = entry.get(key)
        if isinstance(value, str) and value.strip():
            return value
    return f"AppleALC ConfigData codec 0x{codec_id:08X} layout {layout_id}"


def masked_constraints_overlap(
    left_id: int, left_mask: int, right_id: int, right_mask: int
) -> bool:
    return ((left_id ^ right_id) & (left_mask & right_mask)) == 0


def exact_profile_is_subset(left: dict[str, Any], right: dict[str, Any]) -> bool:
    """Return true when every device matched by left also matches right."""
    for prefix in ("revision", "subsystem"):
        left_id = int(left[f"{prefix}_id"])
        left_mask = int(left[f"{prefix}_mask"])
        right_id = int(right[f"{prefix}_id"])
        right_mask = int(right[f"{prefix}_mask"])
        if (left_mask & right_mask) != right_mask:
            return False
        if ((left_id ^ right_id) & right_mask) != 0:
            return False
    return True


def exact_profile_specificity(entry: dict[str, Any]) -> int:
    return bin(int(entry["revision_mask"])).count("1") + bin(
        int(entry["subsystem_mask"])
    ).count("1")


def generate(archive: Path) -> tuple[str, dict[str, int]]:
    archive_size = archive.stat().st_size
    if archive_size > MAX_ARCHIVE_FILE_BYTES:
        raise RuntimeError(
            f"AppleALC archive exceeds the {MAX_ARCHIVE_FILE_BYTES}-byte input safety limit"
        )

    with zipfile.ZipFile(archive) as zf:
        names = zf.namelist()
        total_uncompressed = 0
        for info in zf.infolist():
            total_uncompressed += info.file_size
            if total_uncompressed > MAX_ARCHIVE_UNCOMPRESSED_BYTES:
                raise RuntimeError(
                    f"AppleALC archive expands to more than {MAX_ARCHIVE_UNCOMPRESSED_BYTES} bytes"
                )
        if len(names) > MAX_ARCHIVE_MEMBERS:
            raise RuntimeError(
                f"AppleALC archive has {len(names)} members; safety limit is {MAX_ARCHIVE_MEMBERS}"
            )
        name_set = set(names)
        if len(name_set) != len(names):
            member_counts = Counter(names)
            duplicates = sorted(name for name, count in member_counts.items() if count > 1)
            preview = ", ".join(duplicates[:8])
            raise RuntimeError(f"duplicate archive members make AppleALC input ambiguous: {preview}")
        root = resource_root(names)
        info_names = sorted(
            name for name in names
            if name.startswith(root)
            and re.fullmatch(re.escape(root) + r"[^/]+/Info\.plist", name)
        )

        profiles: "OrderedDict[tuple[int, int], dict[str, Any]]" = OrderedDict()
        skipped_missing_platform = 0
        skipped_missing_pathmap = 0
        skipped_no_pairs = 0

        for info_name in info_names:
            folder = info_name.rsplit("/", 1)[0]
            info = load_plist(read_plist_member(zf, info_name))
            if not isinstance(info, dict):
                raise RuntimeError(f"invalid resource Info.plist root in {info_name}")
            files = info.get("Files", {})
            if not isinstance(files, dict):
                raise RuntimeError(f"invalid Files dictionary in {info_name}")
            layouts = files.get("Layouts", []) or []
            platforms = files.get("Platforms", []) or []
            if not isinstance(layouts, list) or not isinstance(platforms, list):
                raise RuntimeError(f"invalid layout/platform arrays in {info_name}")
            platform_by_id: dict[int, str] = {}
            for item in platforms:
                if not isinstance(item, dict):
                    raise RuntimeError(f"invalid platform entry in {info_name}")
                pid = as_int(item.get("Id"))
                path = item.get("Path")
                if pid is not None and 0 <= pid <= 0xFFFFFFFF and isinstance(path, str):
                    normalized_path = resource_member(folder, path)
                    existing_path = platform_by_id.get(pid)
                    if existing_path is not None and existing_path != normalized_path:
                        raise RuntimeError(
                            f"conflicting platform paths for id {pid} in {info_name}: "
                            f"{existing_path!r} vs {normalized_path!r}"
                        )
                    platform_by_id[pid] = normalized_path

            codec_name = str(info.get("CodecName") or folder.rsplit("/", 1)[-1])
            for layout in layouts:
                if not isinstance(layout, dict):
                    raise RuntimeError(f"invalid layout entry in {info_name}")
                layout_id = as_int(layout.get("Id"))
                layout_path = layout.get("Path")
                if layout_id is None or not 0 <= layout_id <= 0xFFFFFFFF or not isinstance(layout_path, str):
                    continue
                platform_path = platform_by_id.get(layout_id)
                if platform_path is None:
                    skipped_missing_platform += 1
                    continue

                layout_member = resource_member(folder, layout_path)
                platform_member = platform_path
                if layout_member not in name_set or platform_member not in name_set:
                    skipped_missing_platform += 1
                    continue

                try:
                    layout_data = load_plist_fragment(read_plist_member(zf, layout_member))
                    platform_data = load_plist_fragment(read_plist_member(zf, platform_member))
                except PlistParseError:
                    skipped_missing_platform += 1
                    continue

                if not isinstance(layout_data, dict) or not isinstance(platform_data, dict):
                    raise RuntimeError(f"invalid layout/platform plist root for {layout_member} / {platform_member}")

                # AppleALC may intentionally provide more than one PathMap
                # object for the same PathMapID. They are cumulative alternatives,
                # not last-wins replacements. Preserve every map deterministically.
                pathmaps: dict[int, list[Any]] = {}
                raw_pathmaps = platform_data.get("PathMaps", []) or []
                if not isinstance(raw_pathmaps, list):
                    raise RuntimeError(f"invalid PathMaps array in {platform_member}")
                for pathmap in raw_pathmaps:
                    if not isinstance(pathmap, dict):
                        raise RuntimeError(f"invalid PathMap entry in {platform_member}")
                    pathmap_id = as_int(pathmap.get("PathMapID"))
                    if pathmap_id is not None and 0 <= pathmap_id <= 0xFFFFFFFF:
                        pathmaps.setdefault(pathmap_id, []).append(pathmap.get("PathMap"))

                comment = str(layout.get("Comment") or "")
                raw_refs = layout_data.get("PathMapRef", []) or []
                if not isinstance(raw_refs, list):
                    raise RuntimeError(f"invalid PathMapRef array in {layout_member}")
                for ref in raw_refs:
                    if not isinstance(ref, dict):
                        raise RuntimeError(f"invalid PathMapRef entry in {layout_member}")
                    pathmap_id = as_int(ref.get("PathMapID"))
                    pathmap_variants = pathmaps.get(pathmap_id) if pathmap_id is not None else None
                    if not pathmap_variants:
                        skipped_missing_pathmap += 1
                        continue

                    pairs: list[tuple[int, int]] = []
                    routes: list[tuple[int, ...]] = []
                    for pathmap in pathmap_variants:
                        for chain in leaf_node_chains(pathmap):
                            route = tuple(chain)
                            if route not in routes:
                                routes.append(route)
                            pair = (chain[0], chain[-1])
                            if pair not in pairs:
                                pairs.append(pair)
                    if not pairs:
                        skipped_no_pairs += 1
                        continue

                    codec_ids = ref.get("CodecID", [])
                    if not isinstance(codec_ids, list):
                        codec_ids = [codec_ids]
                    for codec_value in codec_ids:
                        codec_id = as_int(codec_value)
                        if codec_id is None or codec_id <= 0 or codec_id > 0xFFFFFFFF:
                            continue
                        key = (codec_id & 0xFFFFFFFF, layout_id)
                        entry = profiles.setdefault(
                            key,
                            {
                                "codec": key[0],
                                "layout": layout_id,
                                "name": f"{codec_name} layout {layout_id} {comment}",
                                "pairs": [],
                                "routes": [],
                            },
                        )
                        for route in routes:
                            if route in entry["routes"]:
                                continue
                            if len(entry["routes"]) >= ROUTE_COUNT:
                                raise RuntimeError(
                                    f"profile codec 0x{key[0]:08X} layout {layout_id} exceeds "
                                    f"the C full-route capacity ({ROUTE_COUNT}); refusing silent truncation"
                                )
                            entry["routes"].append(route)
                        for pair in pairs:
                            if pair in entry["pairs"]:
                                continue
                            if len(entry["pairs"]) >= PAIR_COUNT:
                                raise RuntimeError(
                                    f"profile codec 0x{key[0]:08X} layout {layout_id} exceeds "
                                    f"the C pair capacity ({PAIR_COUNT}); refusing silent truncation"
                                )
                            entry["pairs"].append(pair)

        pinconfigs = load_plist(read_plist_member(zf, pinconfigs_member(names, root)))
        if not isinstance(pinconfigs, dict):
            raise RuntimeError("invalid PinConfigs plist root")
        personalities = pinconfigs.get("IOKitPersonalities", {})
        if not isinstance(personalities, dict):
            raise RuntimeError("invalid IOKitPersonalities dictionary in PinConfigs")
        applealc_personality = personalities.get("as.vit9696.AppleALC", {})
        if not isinstance(applealc_personality, dict):
            raise RuntimeError("invalid AppleALC personality dictionary in PinConfigs")
        config_defaults = applealc_personality.get("HDAConfigDefault", []) or []
        if not isinstance(config_defaults, list):
            raise RuntimeError("invalid HDAConfigDefault array in PinConfigs")

        generic_config_profiles: "OrderedDict[tuple[int, int], dict[str, Any]]" = OrderedDict()
        exact_config_profiles: "OrderedDict[tuple[int, int, int, int, int, int], dict[str, Any]]" = OrderedDict()
        conflicted_generic_keys: set[tuple[int, int]] = set()
        conflicted_exact_keys: set[tuple[int, int, int, int, int, int]] = set()
        config_duplicates = 0
        exact_duplicates = 0
        config_conflicts = 0
        exact_conflicts = 0
        config_skipped_without_path = 0
        config_invalid = 0

        for raw_entry in config_defaults:
            if not isinstance(raw_entry, dict):
                config_invalid += 1
                continue
            codec_id = as_int(raw_entry.get("CodecID"))
            layout_id = as_int(raw_entry.get("LayoutID"))
            config_data = raw_entry.get("ConfigData")
            revision_id_raw = as_int(raw_entry.get("RevisionID"))
            revision_mask_raw = as_int(raw_entry.get("RevisionMask"))
            subsystem_id_raw = as_int(raw_entry.get("SubsystemID"))
            subsystem_mask_raw = as_int(raw_entry.get("SubsystemMask"))
            # Optional identifiers must be absent or representable as 32-bit
            # HDA fields. Never reinterpret a malformed value as a wildcard or
            # silently wrap a wider integer into a different hardware identity.
            invalid_identity = any(
                field in raw_entry and (value is None or value < -0x80000000 or value > 0xFFFFFFFF)
                for field, value in (
                    ("RevisionID", revision_id_raw), ("RevisionMask", revision_mask_raw),
                    ("SubsystemID", subsystem_id_raw), ("SubsystemMask", subsystem_mask_raw),
                )
            )
            if (
                invalid_identity
                or
                codec_id is None
                or layout_id is None
                or codec_id <= 0
                or codec_id > 0xFFFFFFFF
                or not 0 <= layout_id <= 0xFFFFFFFF
                or not isinstance(config_data, bytes)
                or len(config_data) == 0
                or len(config_data) > (MAX_CONFIG_VERBS_PER_PROFILE * 4)
                or (len(config_data) % 4) != 0
                or (revision_mask_raw is not None and revision_id_raw is None)
                or (subsystem_mask_raw is not None and subsystem_id_raw is None)
            ):
                config_invalid += 1
                continue

            profile_key = (codec_id & 0xFFFFFFFF, layout_id)
            if profile_key not in profiles:
                config_skipped_without_path += 1
                continue

            verbs = tuple(
                int.from_bytes(config_data[index:index + 4], "big")
                for index in range(0, len(config_data), 4)
            )
            # Intel HDA direct verbs encode a 7-bit NID in bits 26:20.
            # Bit 27 selects an indirect addressing mechanism that this driver
            # intentionally does not implement, so reject such ConfigData now
            # instead of emitting a profile that cannot be executed safely.
            if any((verb & 0x08000000) != 0 for verb in verbs):
                config_invalid += 1
                continue
            revision_id = 0 if revision_id_raw is None else revision_id_raw & 0xFFFFFFFF
            revision_mask = 0 if revision_id_raw is None else (
                (0xFFFFFFFF if revision_mask_raw is None else revision_mask_raw) & 0xFFFFFFFF
            )
            subsystem_id = 0 if subsystem_id_raw is None else subsystem_id_raw & 0xFFFFFFFF
            subsystem_mask = 0 if subsystem_id_raw is None else (
                (0xFFFFFFFF if subsystem_mask_raw is None else subsystem_mask_raw) & 0xFFFFFFFF
            )
            entry = {
                "codec": profile_key[0],
                "layout": layout_id,
                "name": config_name(raw_entry, profile_key[0], layout_id),
                "verbs": verbs,
                "revision_id": revision_id,
                "revision_mask": revision_mask,
                "subsystem_id": subsystem_id,
                "subsystem_mask": subsystem_mask,
            }

            if revision_mask == 0 and subsystem_mask == 0:
                if profile_key in conflicted_generic_keys:
                    continue
                existing = generic_config_profiles.get(profile_key)
                if existing is not None:
                    if existing["verbs"] == verbs:
                        config_duplicates += 1
                    else:
                        config_conflicts += 1
                        conflicted_generic_keys.add(profile_key)
                        del generic_config_profiles[profile_key]
                    continue
                generic_config_profiles[profile_key] = entry
                continue

            exact_key = (
                profile_key[0], layout_id, revision_id, revision_mask,
                subsystem_id, subsystem_mask,
            )
            if exact_key in conflicted_exact_keys:
                continue
            existing = exact_config_profiles.get(exact_key)
            if existing is not None:
                if existing["verbs"] == verbs:
                    exact_duplicates += 1
                else:
                    exact_conflicts += 1
                    conflicted_exact_keys.add(exact_key)
                    del exact_config_profiles[exact_key]
                continue
            exact_config_profiles[exact_key] = entry

        exact_entries = list(exact_config_profiles.values())
        # Exact-profile ambiguity is meaningful only inside one codec/layout
        # domain. Group first so unrelated profiles do not incur O(N^2) pair
        # checks when processing large AppleALC archives.
        exact_groups: dict[tuple[int, int], list[dict[str, Any]]] = {}
        for entry in exact_entries:
            exact_groups.setdefault((entry["codec"], entry["layout"]), []).append(entry)

        for exact_group in exact_groups.values():
            if len(exact_group) > MAX_EXACT_PROFILES_PER_CODEC_LAYOUT:
                first = exact_group[0]
                raise RuntimeError(
                    "exact ConfigData profile count exceeds safety limit "
                    f"{MAX_EXACT_PROFILES_PER_CODEC_LAYOUT} for codec "
                    f"0x{first['codec']:08X} layout {first['layout']}"
                )
            for left_index, left in enumerate(exact_group):
                for right in exact_group[left_index + 1:]:
                    if not masked_constraints_overlap(
                        left["revision_id"], left["revision_mask"],
                        right["revision_id"], right["revision_mask"],
                    ):
                        continue
                    if not masked_constraints_overlap(
                        left["subsystem_id"], left["subsystem_mask"],
                        right["subsystem_id"], right["subsystem_mask"],
                    ):
                        continue
                    if left["verbs"] == right["verbs"]:
                        continue
                    left_subset = exact_profile_is_subset(left, right)
                    right_subset = exact_profile_is_subset(right, left)
                    # One strict subset is deterministic because exact profiles are
                    # emitted most-specific-first. If both directions are true, the
                    # masks describe the same match domain even when ignored ID bits
                    # differ; conflicting verbs would otherwise be order-dependent.
                    if left_subset != right_subset:
                        continue
                    raise RuntimeError(
                        "ambiguous overlapping exact ConfigData profiles for "
                        f"codec 0x{left['codec']:08X} layout {left['layout']}"
                    )
        exact_entries.sort(
            key=lambda entry: (
                entry["codec"], entry["layout"], -exact_profile_specificity(entry),
                entry["revision_mask"], entry["revision_id"],
                entry["subsystem_mask"], entry["subsystem_id"], entry["verbs"],
            )
        )

        lines = [
            "/* Auto-generated compact AppleALC pre-boot data. */",
            "/* Source: exact Resources layout/platform PathMapID joins and PinConfigs ConfigData. */",
            "/* Runtime validates PIN_CAP and the live HDA graph before programming a path. */",
            "STATIC CONST HDAU_ALCID_PROFILE mHdaAlcIdProfiles[] = {",
        ]
        for key in sorted(profiles):
            entry = profiles[key]
            pairs = entry["pairs"][:PAIR_COUNT]
            pins = [pair[0] for pair in pairs] + [0] * (PAIR_COUNT - len(pairs))
            dacs = [pair[1] for pair in pairs] + [0] * (PAIR_COUNT - len(pairs))
            pin_text = ", ".join(f"0x{x:02X}" for x in pins)
            dac_text = ", ".join(f"0x{x:02X}" for x in dacs)
            lines.append(
                f'  {{ 0x{entry["codec"]:08X}, {entry["layout"]}, "{c_string(entry["name"])}", '
                f'{{ {pin_text} }}, {{ {dac_text} }} }},'
            )
        lines.extend(["};", ""])

        flat_route_nodes: list[int] = []
        route_rows: list[tuple[dict[str, Any], int, int, int, int]] = []
        for key in sorted(profiles):
            entry = profiles[key]
            for route in entry["routes"]:
                offset = len(flat_route_nodes)
                if (
                    len(route) < 2
                    or len(route) > 0xFFFF
                    or offset > 0xFFFFFFFF
                    or offset + len(route) > 0x100000000
                ):
                    raise RuntimeError(f"invalid full route for codec 0x{entry['codec']:08X} layout {entry['layout']}")
                flat_route_nodes.extend(route)
                route_rows.append((entry, offset, len(route), route[0], route[-1]))

        lines.append("STATIC CONST UINT8 mHdaAlcRouteNodes[] = {")
        if flat_route_nodes:
            for index in range(0, len(flat_route_nodes), 16):
                chunk = ", ".join(f"0x{node:02X}" for node in flat_route_nodes[index:index + 16])
                lines.append(f"  {chunk},")
        else:
            lines.append("  0,")
        lines.extend(["};", "", "STATIC CONST HDAU_ALCID_ROUTE_PROFILE mHdaAlcRouteProfiles[] = {"])
        if route_rows:
            for entry, offset, count, pin, dac in route_rows:
                lines.append(
                    f'  {{ 0x{entry["codec"]:08X}, {entry["layout"]}, "{c_string(entry["name"])}", '
                    f"{offset}U, {count}U, 0x{pin:02X}, 0x{dac:02X} }},"
                )
        else:
            lines.append('  { 0, 0, "none", 0, 0, 0, 0 },')
        lines.extend(["};", ""])

        flat_verbs: list[int] = []
        verb_spans: dict[tuple[int, ...], tuple[int, int]] = {}

        def verb_span(entry: dict[str, Any]) -> tuple[int, int]:
            verbs = tuple(entry["verbs"])
            existing = verb_spans.get(verbs)
            if existing is not None:
                return existing
            offset = len(flat_verbs)
            if offset > 0xFFFFFFFF or len(verbs) > 0xFFFF or offset + len(verbs) > 0x100000000:
                raise RuntimeError("generated ConfigData table exceeds C field limits")
            flat_verbs.extend(verbs)
            span = (offset, len(verbs))
            verb_spans[verbs] = span
            return span

        generic_rows: list[tuple[dict[str, Any], int, int]] = []
        for key in sorted(generic_config_profiles):
            entry = generic_config_profiles[key]
            offset, count = verb_span(entry)
            generic_rows.append((entry, offset, count))

        exact_rows: list[tuple[dict[str, Any], int, int]] = []
        for entry in exact_entries:
            offset, count = verb_span(entry)
            exact_rows.append((entry, offset, count))

        lines.append("STATIC CONST UINT32 mHdaAlcConfigVerbs[] = {")
        if flat_verbs:
            for index in range(0, len(flat_verbs), 8):
                chunk = ", ".join(f"0x{verb:08X}" for verb in flat_verbs[index:index + 8])
                lines.append(f"  {chunk},")
        else:
            lines.append("  0,")
        lines.extend(["};", "", "STATIC CONST HDAU_ALCID_CONFIG_PROFILE mHdaAlcConfigProfiles[] = {"])
        if generic_rows:
            for entry, offset, count in generic_rows:
                lines.append(
                    f'  {{ 0x{entry["codec"]:08X}, {entry["layout"]}, "{c_string(entry["name"])}", '
                    f"{offset}U, {count}U }},"
                )
        else:
            lines.append('  { 0, 0, "none", 0, 0 },')
        lines.extend(["};", ""])

        lines.append("STATIC CONST HDAU_ALCID_EXACT_PROFILE mHdaAlcExactProfiles[] = {")
        if exact_rows:
            for entry, offset, count in exact_rows:
                lines.append(
                    f'  {{ 0x{entry["codec"]:08X}, {entry["layout"]}, '
                    f'0x{entry["revision_id"]:08X}, 0x{entry["revision_mask"]:08X}, '
                    f'0x{entry["subsystem_id"]:08X}, 0x{entry["subsystem_mask"]:08X}, '
                    f'{offset}U, {count}U }},'
                )
        else:
            lines.append("  { 0, 0, 0, 0, 0, 0, 0, 0 },")
        lines.extend(["};", ""])

        stats = {
            "profiles": len(profiles),
            "full_routes": len(route_rows),
            "route_nodes": len(flat_route_nodes),
            "exact_profiles": len(exact_rows),
            "resources": len(info_names),
            "config_profiles": len(generic_rows),
            "config_verbs": len(flat_verbs),
            "config_duplicates": config_duplicates,
            "exact_duplicates": exact_duplicates,
            "config_conflicts": config_conflicts,
            "exact_conflicts": exact_conflicts,
            "config_skipped_without_path": config_skipped_without_path,
            "config_invalid": config_invalid,
            "skipped_missing_platform": skipped_missing_platform,
            "skipped_missing_pathmap": skipped_missing_pathmap,
            "skipped_no_pairs": skipped_no_pairs,
        }
        return "\n".join(lines), stats


def write_output_atomic(output: Path, text: str) -> None:
    """Write generated C text atomically and normalize host I/O failures."""
    temporary_name: str | None = None
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        try:
            original_mode = stat.S_IMODE(output.stat().st_mode)
        except FileNotFoundError:
            original_mode = None
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", newline="\n",
            dir=output.parent, prefix=f".{output.name}.", suffix=".tmp", delete=False
        ) as temporary:
            temporary_name = temporary.name
            temporary.write(text)
            temporary.write("\n")
            # NamedTemporaryFile defaults to 0600. Preserve the permissions of
            # an existing generated header before replacing it atomically.
            if original_mode is not None:
                os.fchmod(temporary.fileno(), original_mode)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_name, output)
        temporary_name = None
    except OSError as error:
        cleanup_error: OSError | None = None
        if temporary_name is not None:
            try:
                Path(temporary_name).unlink()
            except FileNotFoundError:
                pass
            except OSError as cleanup:
                cleanup_error = cleanup
        message = f"could not write generated output atomically: {error}"
        if cleanup_error is not None:
            message += f"; temporary cleanup also failed: {cleanup_error}"
        raise RuntimeError(message) from None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("applealc_zip", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    if not args.applealc_zip.is_file():
        parser.error(f"AppleALC archive does not exist or is not a file: {args.applealc_zip}")

    try:
        text, stats = generate(args.applealc_zip)
    except (OSError, RuntimeError, RecursionError, plistlib.InvalidFileException, zipfile.BadZipFile) as error:
        parser.error(str(error))

    try:
        write_output_atomic(args.output, text)
    except RuntimeError as error:
        parser.error(str(error))

    print(" ".join(f"{key}={value}" for key, value in stats.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
