#!/bin/bash
# Enable HDAUniversalBootChimeDxe codec dump for the next boot
# Copyright © 2026 DNMTechLabs
# Contact: dnmtechlabs@gmail.com
# macOS Mavericks through current macOS; /bin/bash 3.2 compatible.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)" || exit 1
cd "$SCRIPT_DIR" || exit 1

printf '\nHDAUniversalBootChimeDxe codec dump trigger\n'
printf 'Copyright © 2026 DNMTechLabs | dnmtechlabs@gmail.com\n\n'

is_boot_efi_root() {
  [ -d "$1/EFI" ] || return 1
  [ -d "$1/EFI/OC" ] || [ -d "$1/EFI/CLOVER" ] || return 1
}

has_installed_driver() {
  [ -f "$1/EFI/OC/Drivers/HDAUniversalBootChimeDxe.efi" ] ||
  [ -f "$1/EFI/CLOVER/drivers/UEFI/HDAUniversalBootChimeDxe.efi" ] ||
  [ -f "$1/EFI/CLOVER/drivers64UEFI/HDAUniversalBootChimeDxe.efi" ]
}

find_efi_root() {
  local candidate
  local current
  local found=""
  local count=0

  # Explicit override is authoritative and prevents selecting the wrong EFI.
  if [ -n "${EFI_VOL:-}" ]; then
    is_boot_efi_root "$EFI_VOL" || return 2
    printf '%s' "$EFI_VOL"
    return 0
  fi

  # When the command itself is stored on the EFI, prefer that exact volume.
  current="$SCRIPT_DIR"
  while [ "$current" != "/" ] && [ -n "$current" ]; do
    if is_boot_efi_root "$current"; then
      printf '%s' "$current"
      return 0
    fi
    current="$(dirname "$current")"
  done

  # Discover all mounted bootloader EFIs. Do not silently choose when several
  # are mounted because writing the wrong EFI is worse than failing clearly.
  for candidate in /Volumes/*; do
    [ -d "$candidate" ] || continue
    if is_boot_efi_root "$candidate"; then
      found="$candidate"
      count=$((count + 1))
    fi
  done

  if [ "$count" -eq 1 ]; then
    printf '%s' "$found"
    return 0
  fi
  if [ "$count" -gt 1 ]; then
    return 3
  fi
  return 1
}

EFI_ROOT=""
find_status=0
EFI_ROOT="$(find_efi_root)" || find_status=$?
case "$find_status" in
  0) ;;
  2)
    echo "EFI_VOL does not point to a mounted OpenCore/Clover EFI: ${EFI_VOL:-}"
    exit 1
    ;;
  3)
    echo "More than one mounted OpenCore/Clover EFI was found."
    echo "Run again with the exact volume, for example:"
    echo "  EFI_VOL=/Volumes/EFI ./Enable-HDAUCodecDump-NextBoot.command"
    exit 1
    ;;
  *)
    echo "No mounted OpenCore/Clover EFI volume was found."
    echo "Mount the EFI first, or run with EFI_VOL=/Volumes/<name>."
    echo
    diskutil list 2>/dev/null || true
    exit 1
    ;;
esac

if ! has_installed_driver "$EFI_ROOT"; then
  echo "HDAUniversalBootChimeDxe.efi was not found in the selected OpenCore/Clover drivers directory:"
  echo "$EFI_ROOT"
  echo "Install the current driver first; no configuration was written."
  exit 1
fi

CFG_DIR="$EFI_ROOT/EFI/HDAUniversalBootChimeDxe"
CFG="$CFG_DIR/HDAUChimeDebug.ini"
RELEASE_CFG="$CFG_DIR/HDAUChimeRelease.ini"
LOG_DIR="$CFG_DIR/Logs"
LOG="$LOG_DIR/HDAUChime.log"
CFG_BACKUP="$CFG.backup"
RELEASE_CFG_BACKUP="$RELEASE_CFG.backup"
TMP_CFG=""
CFG_STAGE=""

cleanup_tmp() {
  if [ -n "$TMP_CFG" ]; then
    rm -f "$TMP_CFG" 2>/dev/null || true
    TMP_CFG=""
  fi
  if [ -n "$CFG_STAGE" ]; then
    rm -f "$CFG_STAGE" 2>/dev/null || true
    CFG_STAGE=""
  fi
}
trap cleanup_tmp EXIT
trap 'cleanup_tmp; exit 129' HUP
trap 'cleanup_tmp; exit 130' INT
trap 'cleanup_tmp; exit 143' TERM

mkdir -p "$CFG_DIR" "$LOG_DIR" || exit 1

set_key_file() {
  local file="$1"
  local key="$2"
  local value="$3"
  local tmp="$file.tmp.$$"

  TMP_CFG="$tmp"
  awk -v wanted="$key" -v replacement="$value" '
    BEGIN { done = 0 }
    {
      line = $0
      eq = index(line, "=")
      if (eq > 0) {
        lhs = substr(line, 1, eq - 1)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", lhs)
        if (tolower(lhs) == tolower(wanted)) {
          if (!done) print wanted "=" replacement
          done = 1
          next
        }
      }
      print line
    }
    END { if (!done) print wanted "=" replacement }
  ' "$file" > "$tmp" || { rm -f "$tmp"; TMP_CFG=""; return 1; }
  mv -f "$tmp" "$file" || { rm -f "$tmp"; TMP_CFG=""; return 1; }
  TMP_CFG=""
}

set_key() {
  set_key_file "$CFG_STAGE" "$1" "$2"
}


# Keep one deterministic backup instead of accumulating timestamped copies on
# the EFI volume. Build the entire debug profile in a sibling staging file and
# publish it with one same-directory rename, so even a Release profile that
# already opts into debug configuration never observes a partially edited INI.
if [ -f "$CFG" ]; then
  if [ ! -f "$CFG_BACKUP" ]; then
    cp "$CFG" "$CFG_BACKUP" || {
      echo "Could not preserve the existing configuration backup: $CFG_BACKUP" >&2
      exit 1
    }
  fi
  CFG_STAGE="$CFG.stage.$$"
  cp "$CFG" "$CFG_STAGE" || {
    echo "Could not stage the existing debug configuration: $CFG_STAGE" >&2
    exit 1
  }
else
  CFG_STAGE="$CFG.stage.$$"
  cat > "$CFG_STAGE" <<'CFGEOF'
# HDAUniversalBootChimeDxe Debug Config - 1.0.87-final-audit
# Copyright © 2026 DNMTechLabs
# Contact: dnmtechlabs@gmail.com
CFGEOF
fi

# The release profile is parsed before the debug profile and normally ships
# with ReadDebugConfig=0. Preserve its original state now, but publish the
# enabling bit only after the debug profile has been fully prepared below.
# This ordering is fail-safe: an interrupted helper run cannot expose a
# partially-written debug profile through the normal release configuration.
if [ -f "$RELEASE_CFG" ] && [ ! -f "$RELEASE_CFG_BACKUP" ]; then
  cp "$RELEASE_CFG" "$RELEASE_CFG_BACKUP" || {
    echo "Could not preserve the existing release configuration backup: $RELEASE_CFG_BACKUP" >&2
    exit 1
  }
fi

set_key Debug 1
set_key FileLog 1
set_key NvramStatus 1
set_key ReadConfig 1
set_key ReadDebugConfig 1
set_key CodecDumpAlways 1
set_key CodecDumpSkipsPlayback 1
set_key CodecDumpOnD 1
set_key BootScreenD 1
set_key DPollMs 0
set_key DumpCodec 1
set_key NoPlayback 0
set_key ProbeOnly 0
set_key FullPathMap 1
set_key RouteFallback 1
set_key PinSensePolicy 1
set_key TransactionalRestore 1
set_key CorbRirbFallback 1
set_key PcmFormatFallback 1
set_key DigitalPipeline 1
set_key DigitalInfoFrame 0
set_key ExactProfileInit 1
set_key Volume 30

# Publish the complete Debug profile in one same-directory rename. Nothing in
# the live debug INI is modified before this point.
mv -f "$CFG_STAGE" "$CFG" || {
  echo "Could not publish the staged debug configuration: $CFG" >&2
  exit 1
}
CFG_STAGE=""

# Commit the release-profile opt-in last. If no release profile is installed,
# the driver's compiled ReadDebugConfig default remains TRUE and the prepared
# debug profile is still usable. Do not create a synthetic release profile.
if [ -f "$RELEASE_CFG" ]; then
  set_key_file "$RELEASE_CFG" ReadDebugConfig 1
fi

rm -f "$LOG" 2>/dev/null || true

echo "Done. EFI selected: $EFI_ROOT"
echo "Reboot to collect the dump."
echo "The complete codec dump will be appended to the single consolidated log:"
echo "$LOG"
echo
echo "This setting is persistent. After collecting it, restore the .backup configuration files when present, or reset the dump keys and ReadDebugConfig manually."
