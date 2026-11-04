#!/bin/bash
# HDAUniversalBootChimeDxe
# Copyright © 2026 DNMTechLabs
# Contact: dnmtechlabs@gmail.com
# Boot chime DXE driver for Clover/OpenCore.

# HDAUniversalBootChimeDxe robust EDK2 build command
# macOS /bin/bash 3.2 compatible. Always writes logs.

set -uo pipefail

cd "$(dirname "$0")" || exit 1

APP="HDAUniversalBootChimeDxe"
PKG="HDAUniversalBootChimeDxePkg"
DSC_REL="$PKG/$PKG.dsc"
VERSION="1.0.87-final-audit"
AUTO_FETCH_EDK2="${AUTO_FETCH_EDK2:-1}"
EDK2_DEFAULT_REF="edk2-stable202608"
EDK2_REF="${EDK2_REF:-$EDK2_DEFAULT_REF}"
EDK2_EXPECTED_COMMIT="${EDK2_EXPECTED_COMMIT:-}"
if [ "$EDK2_REF" = "$EDK2_DEFAULT_REF" ] && [ -z "$EDK2_EXPECTED_COMMIT" ]; then
  EDK2_EXPECTED_COMMIT="2970e5699ba6267f3384ffab20f96647578aebc8"
fi
EDK2_DEFAULT_DIR="${EDK2_DEFAULT_DIR:-$HOME/edk2}"
SOURCE_TEST_ROUNDS="${SOURCE_TEST_ROUNDS:-5}"

# Transactional build state. The source package is staged outside WORKSPACE and
# any temporary BaseTools compatibility edit is restored byte-for-byte at exit.
STAGING_ROOT=""
STAGED_PKG_DIR=""
ACTIVE_DSC=""
BASETOOLS_WRAPPER=""
BASETOOLS_WRAPPER_BACKUP=""

mkdir -p Release/Logs
MASTER_LOG="Release/Logs/BUILD-COMPILE.log"
: > "$MASTER_LOG"
# Capture everything from this point, including environment/setup failures,
# into the single consolidated build log requested for release artifacts.
exec > >(tee -a "$MASTER_LOG") 2>&1

# Never mix a new build with stale artifacts from a prior run.
rm -rf Release/Debug Release/Release Release/EFI-Test-Pack
rm -f Release/"${APP}"-*-DEBUG-RELEASE.zip
mkdir -p Release/Debug Release/Release

log()  { printf '[%s] %s\n' "$APP" "$*"; }
warn() { printf '[%s] WARNING: %s\n' "$APP" "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; printf 'Master log: %s\n' "$MASTER_LOG" >&2; exit 1; }

restore_basetools_wrapper() {
  if [ -n "$BASETOOLS_WRAPPER" ] && [ -n "$BASETOOLS_WRAPPER_BACKUP" ] &&
     [ -f "$BASETOOLS_WRAPPER_BACKUP" ]; then
    if [ -e "$BASETOOLS_WRAPPER" ]; then
      cp -p "$BASETOOLS_WRAPPER_BACKUP" "$BASETOOLS_WRAPPER" 2>/dev/null || true
    fi
    rm -f "$BASETOOLS_WRAPPER_BACKUP" 2>/dev/null || true
    BASETOOLS_WRAPPER_BACKUP=""
  fi
}

cleanup_build_transaction() {
  local status=$?
  trap - EXIT INT TERM HUP

  restore_basetools_wrapper

  if [ -n "$STAGING_ROOT" ] && [ -d "$STAGING_ROOT" ]; then
    rm -rf "$STAGING_ROOT" 2>/dev/null || true
    STAGING_ROOT=""
  fi

  if [ "$status" -ne 0 ]; then
    # Never leave a mixed/stale Release package after a failed gate/build.
    rm -rf Release/Debug Release/Release Release/EFI-Test-Pack 2>/dev/null || true
    rm -f Release/"${APP}"-*-DEBUG-RELEASE.zip 2>/dev/null || true
  fi

  exit "$status"
}

trap cleanup_build_transaction EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

lowercase() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

cpu_count() {
  local n=""

  if command -v getconf >/dev/null 2>&1; then
    n="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
  fi
  if { [ -z "$n" ] || [ "$n" -lt 1 ] 2>/dev/null; } && command -v sysctl >/dev/null 2>&1; then
    n="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
  fi
  case "$n" in
    ''|*[!0-9]*) n=1 ;;
    *) [ "$n" -ge 1 ] 2>/dev/null || n=1 ;;
  esac
  printf '%s' "$n"
}

require_command() {
  local c="$1"
  command -v "$c" >/dev/null 2>&1 || fail "Required command not found: $c"
}

clean_transient_source_metadata() {
  local metadata=""
  local artifact=""

  metadata="$(find . \
    -path './edk2' -prune -o \
    -type f \( -name '.DS_Store' -o -name '._*' \) \
    -print 2>/dev/null | LC_ALL=C sort || true)"

  [ -n "$metadata" ] || return 0

  while IFS= read -r artifact; do
    [ -n "$artifact" ] || continue
    rm -f "$artifact" || fail "Could not remove transient macOS metadata: $artifact"
  done <<EOF_HDAU_MACOS_METADATA
$metadata
EOF_HDAU_MACOS_METADATA
}

verify_source_tree_hygiene() {
  local forbidden=""

  forbidden="$(find . \
    -path './edk2' -prune -o \
    \( -type d -name '__pycache__' -o \
       -type f \( -name '*.pyc' -o -name '.DS_Store' -o -name '._*' -o -name '*.tmp.*' \) \) \
    -print 2>/dev/null | LC_ALL=C sort || true)"

  if [ -n "$forbidden" ]; then
    printf '%s\n' "$forbidden" >&2
    fail "Source tree contains cache/temporary artifacts; remove them before the release gate"
  fi
}

preflight_tools() {
  log "Preflight tools..."
  require_command uname
  require_command make
  require_command python3
  require_command cmp
  require_command find
  require_command sort
  require_command zip
  require_command unzip
  python3 -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 8) else 1)' \
    || fail "Python 3.8 or newer is required by current EDK2 and the AppleALC data generator"

  if [ "$(uname -s)" = "Darwin" ]; then
    command -v xcodebuild >/dev/null 2>&1 || warn "xcodebuild not found in PATH. Install Xcode or Command Line Tools if the build fails."
    xcode-select -p >/dev/null 2>&1 || warn "xcode-select path is not configured. Run: xcode-select --install"
  fi
}

run_source_regressions() {
  local round

  case "$SOURCE_TEST_ROUNDS" in
    ''|*[!0-9]*) fail "SOURCE_TEST_ROUNDS must be an integer between 1 and 100" ;;
    *)
      [ "$SOURCE_TEST_ROUNDS" -ge 1 ] 2>/dev/null && [ "$SOURCE_TEST_ROUNDS" -le 100 ] 2>/dev/null \
        || fail "SOURCE_TEST_ROUNDS must be between 1 and 100"
      ;;
  esac

  log "Source syntax/regression gate: shell scripts"
  bash -n Build-HDAUniversalBootChimeDxe.command Setup-EDK2.command Enable-HDAUCodecDump-NextBoot.command \
    || fail "Shell syntax gate failed"

  log "Source syntax/regression gate: Python AST"
  python3 - <<'PY_SOURCE_AST' || fail "Python syntax gate failed"
import ast
from pathlib import Path
for name in ("Tools/generate_applealc_profiles.py", "Tests/test_audit_regressions.py"):
    ast.parse(Path(name).read_text(encoding="utf-8"), filename=name)
PY_SOURCE_AST

  round=1
  while [ "$round" -le "$SOURCE_TEST_ROUNDS" ]; do
    clean_transient_source_metadata
    log "Regression/stress round $round/$SOURCE_TEST_ROUNDS"
    PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s Tests -v \
      || fail "Source regression/stress round $round failed"
    round=$((round + 1))
  done
}

ensure_basetools_c_binaries() {
  local cbin="$WORKSPACE/BaseTools/Source/C/bin"

  if [ ! -x "$cbin/GenFw" ]; then
    log "Building/refreshing EDK2 BaseTools C binaries..."
    make -C "$WORKSPACE/BaseTools" -j"$(cpu_count)" || fail "BaseTools build failed"
  fi

  if [ -d "$cbin" ]; then
    export PATH="$cbin:$PATH"
    log "BaseTools C bin path: $cbin"
  fi
}

patch_basetools_shell_wrappers() {
  local wrapper="$WORKSPACE/BaseTools/BinWrappers/PosixLike/GenericShellWrapper"
  local backup=""

  [ -f "$wrapper" ] || return 0

  # Some EDK2 wrapper revisions use Bash negative array indexes such as
  # ${BASH_SOURCE[-1]}. macOS /bin/bash 3.2 rejects them. Apply the compatibility
  # edit transactionally and restore the user's checkout at script exit.
  if grep -q 'BASH_SOURCE\[-1\]' "$wrapper" 2>/dev/null; then
    backup="$(mktemp "${TMPDIR:-/tmp}/hdau-basetools-wrapper.XXXXXX")" \
      || fail "Could not create temporary GenericShellWrapper backup"
    cp -p "$wrapper" "$backup" || {
      rm -f "$backup"
      fail "Could not backup GenericShellWrapper"
    }
    BASETOOLS_WRAPPER="$wrapper"
    BASETOOLS_WRAPPER_BACKUP="$backup"

    log "Applying temporary BaseTools GenericShellWrapper compatibility patch"
    python3 - "$wrapper" <<'PY_WRAPPER_PATCH'
from pathlib import Path
import sys

p = Path(sys.argv[1])
text = p.read_text()
text = text.replace('${BASH_SOURCE[-1]}', '${BASH_SOURCE[${#BASH_SOURCE[@]}-1]}')
text = text.replace('BASH_SOURCE[-1]', 'BASH_SOURCE[${#BASH_SOURCE[@]}-1]')
p.write_text(text)
PY_WRAPPER_PATCH
    chmod +x "$wrapper" || fail "Could not chmod GenericShellWrapper"
  fi
}

find_edk2_workspace() {
  if [ -n "${WORKSPACE:-}" ] && [ -f "$WORKSPACE/edksetup.sh" ] && [ -d "$WORKSPACE/MdePkg" ]; then
    (cd "$WORKSPACE" && pwd)
    return 0
  fi

  local p
  for p in \
    "$EDK2_DEFAULT_DIR" \
    "$HOME/edk2" \
    "$HOME/Desktop/edk2" \
    "$HOME/Downloads/edk2" \
    "$PWD/edk2" \
    "$PWD/../edk2" \
    "$PWD/../../edk2"; do
    if [ -f "$p/edksetup.sh" ] && [ -d "$p/MdePkg" ]; then
      (cd "$p" && pwd)
      return 0
    fi
  done
  return 1
}

bootstrap_edk2_if_needed() {
  local ws
  if ws="$(find_edk2_workspace)"; then
    printf '%s' "$ws"
    return 0
  fi

  [ "$AUTO_FETCH_EDK2" = "1" ] || return 1

  require_command git

  log "EDK2 WORKSPACE was not found. AUTO_FETCH_EDK2=1; attempting pinned EDK2 bootstrap." >&2
  log "Target: $EDK2_DEFAULT_DIR" >&2
  log "This first-time setup needs internet access and may take several minutes." >&2

  if [ -e "$EDK2_DEFAULT_DIR" ] && [ ! -f "$EDK2_DEFAULT_DIR/edksetup.sh" ]; then
    fail "EDK2 target exists but is not a valid edk2 checkout: $EDK2_DEFAULT_DIR. Remove it or set WORKSPACE=/path/to/edk2."
  fi

  if [ ! -d "$EDK2_DEFAULT_DIR/.git" ]; then
    mkdir -p "$(dirname "$EDK2_DEFAULT_DIR")" || fail "Could not create EDK2 parent directory"
    git clone --depth 1 --branch "$EDK2_REF" --single-branch https://github.com/tianocore/edk2.git "$EDK2_DEFAULT_DIR" >&2 || fail "Could not clone EDK2 ref $EDK2_REF. Check internet access or install edk2 manually in ~/edk2."
  fi

  # Verify the cloned/default root before allowing any submodule checkout.
  # This makes the pinned root commit the trust/reproducibility boundary.
  verify_edk2_ref "$EDK2_DEFAULT_DIR" >&2

  log "Updating EDK2 submodules..." >&2
  git -C "$EDK2_DEFAULT_DIR" submodule update --init --recursive --depth 1 >&2 || fail "Could not initialize EDK2 submodules."

  [ -f "$EDK2_DEFAULT_DIR/edksetup.sh" ] || fail "EDK2 clone completed but edksetup.sh was not found."
  [ -d "$EDK2_DEFAULT_DIR/MdePkg" ] || fail "EDK2 clone completed but MdePkg was not found."

  (cd "$EDK2_DEFAULT_DIR" && pwd)
  return 0
}

verify_edk2_ref() {
  local ws="$1"
  local head=""
  local expected=""

  if [ ! -d "$ws/.git" ]; then
    if [ -n "$EDK2_EXPECTED_COMMIT" ]; then
      fail "WORKSPACE is not a git checkout; cannot verify pinned EDK2 commit $EDK2_EXPECTED_COMMIT for EDK2_REF=$EDK2_REF. Use a verified git checkout, or explicitly choose a custom EDK2_REF without EDK2_EXPECTED_COMMIT if an unverified workspace is intentional."
    fi
    warn "WORKSPACE is not a git checkout; custom EDK2_REF=$EDK2_REF cannot be provenance-verified."
    return 0
  fi

  head="$(git -C "$ws" rev-parse HEAD 2>/dev/null || true)"
  expected="$(git -C "$ws" rev-parse "$EDK2_REF^{commit}" 2>/dev/null || true)"
  [ -n "$head" ] || fail "Could not resolve EDK2 WORKSPACE HEAD: $ws"
  [ -n "$expected" ] || fail "EDK2 reference is unavailable in WORKSPACE: $EDK2_REF"
  [ "$head" = "$expected" ] || fail "EDK2 WORKSPACE is at $head, but EDK2_REF=$EDK2_REF resolves to $expected. Checkout the requested ref or override EDK2_REF explicitly."
  if [ -n "$EDK2_EXPECTED_COMMIT" ] && [ "$head" != "$EDK2_EXPECTED_COMMIT" ]; then
    fail "EDK2 WORKSPACE resolved $EDK2_REF to $head, but the pinned expected commit is $EDK2_EXPECTED_COMMIT. Refusing a non-reproducible toolchain."
  fi
  log "Verified EDK2 reference: $EDK2_REF ($head)"
  if [ -n "$EDK2_EXPECTED_COMMIT" ]; then
    log "Verified EDK2 expected commit: $EDK2_EXPECTED_COMMIT"
  fi
}

prepare_edk2() {
  local ws
  if ! ws="$(bootstrap_edk2_if_needed)"; then
    if [ "$AUTO_FETCH_EDK2" = "0" ]; then
      fail "EDK2 WORKSPACE not found and AUTO_FETCH_EDK2=0 disables automatic bootstrap. Put edk2 in ~/edk2, export WORKSPACE=/path/to/edk2, or run Setup-EDK2.command."
    fi
    fail "EDK2 WORKSPACE bootstrap failed. Check the preceding error, internet access, EDK2_REF, or prepare edk2 manually with Setup-EDK2.command."
  fi

  export WORKSPACE="$ws"
  verify_edk2_ref "$WORKSPACE"
  export EDK_TOOLS_PATH="${EDK_TOOLS_PATH:-$WORKSPACE/BaseTools}"
  export PACKAGES_PATH="${PACKAGES_PATH:-$WORKSPACE}"

  [ -f "$WORKSPACE/edksetup.sh" ] || fail "edksetup.sh not found in WORKSPACE: $WORKSPACE"
  [ -d "$WORKSPACE/MdePkg" ] || fail "MdePkg not found in WORKSPACE: $WORKSPACE"

  log "Sourcing edksetup.sh from: $WORKSPACE"
  export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
  # EDK2 edksetup.sh references optional variables such as PYTHON_COMMAND.
  # Keep this build script strict, but disable nounset only while sourcing EDK2.
  set +u
  # shellcheck source=/dev/null
  . "$WORKSPACE/edksetup.sh" >/dev/null
  local edksetup_status=$?
  set -u
  [ "$edksetup_status" -eq 0 ] || fail "edksetup.sh failed"

  ensure_basetools_c_binaries
  patch_basetools_shell_wrappers

  if ! command -v build >/dev/null 2>&1; then
    if [ -d "$WORKSPACE/BaseTools" ]; then
      log "EDK2 build command not found; building BaseTools..."
      make -C "$WORKSPACE/BaseTools" -j"$(cpu_count)" || fail "BaseTools build failed"
      export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
      set +u
      # shellcheck source=/dev/null
      . "$WORKSPACE/edksetup.sh" >/dev/null
      local edksetup_status_after=$?
      set -u
      [ "$edksetup_status_after" -eq 0 ] || fail "edksetup.sh failed after BaseTools build"
      ensure_basetools_c_binaries
      patch_basetools_shell_wrappers
    fi
  fi

  command -v build >/dev/null 2>&1 || fail "EDK2 build command still not found after setup."
}

find_unique_file() {
  local root="$1"
  local pattern="$2"
  local description="$3"
  local matches=""
  local count=0

  matches="$(find "$root" -name "$pattern" -type f -print 2>/dev/null | LC_ALL=C sort || true)"
  if [ -n "$matches" ]; then
    count="$(printf '%s\n' "$matches" | sed '/^[[:space:]]*$/d' | wc -l | tr -d '[:space:]')"
  fi
  case "$count" in
    0) return 1 ;;
    1) printf '%s' "$matches"; return 0 ;;
    *)
      warn "Ambiguous $description candidates; refusing first-match selection:" >&2
      printf '%s\n' "$matches" >&2
      return 2
      ;;
  esac
}

stage_package_for_build() {
  local build_dir="$WORKSPACE/Build/$PKG"

  STAGING_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/hdau-edk2-package.XXXXXX")" \
    || fail "Could not create temporary package staging directory"
  STAGED_PKG_DIR="$STAGING_ROOT/$PKG"
  ACTIVE_DSC="$STAGED_PKG_DIR/$PKG.dsc"

  mkdir -p "$STAGED_PKG_DIR" || fail "Could not create staged package directory"
  cp -R HDAUniversalBootChimeDxe "$STAGED_PKG_DIR/" || fail "Could not stage module folder"
  cp HDAUniversalBootChimeDxePkg.dsc "$ACTIVE_DSC" || fail "Could not stage DSC"

  # Let EDK2 resolve the project package from staging instead of copying it into
  # or deleting anything under the user's WORKSPACE source tree.
  export PACKAGES_PATH="$STAGING_ROOT${PACKAGES_PATH:+:$PACKAGES_PATH}"
  log "Staged package outside WORKSPACE: $STAGED_PKG_DIR"

  # This is only EDK2 generated output for this package, never source. Removing
  # it prevents packaging a stale EFI after an incremental/failed build.
  if [ -e "$build_dir" ]; then
    log "Removing stale generated package output: $build_dir"
    rm -rf "$build_dir" || fail "Could not remove stale generated build output: $build_dir"
  fi
}

resolve_edk2_library_paths() {
  local dsc="$ACTIVE_DSC"
  local regfilter=""
  local reg_rel=""
  local stackcheck=""
  local stack_rel=""

  [ -f "$dsc" ] || fail "DSC not found for library patching: $dsc"

  # Recent EDK2 trees moved/renamed the null RegisterFilterLib instance.
  # Detect the real path in the user's EDK2 checkout instead of assuming one fixed name.
  if [ -f "$WORKSPACE/MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf" ]; then
    regfilter="$WORKSPACE/MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf"
  elif [ -f "$WORKSPACE/MdePkg/Library/BaseRegisterFilterLibNull/BaseRegisterFilterLibNull.inf" ]; then
    regfilter="$WORKSPACE/MdePkg/Library/BaseRegisterFilterLibNull/BaseRegisterFilterLibNull.inf"
  else
    regfilter="$(find_unique_file "$WORKSPACE/MdePkg/Library" '*RegisterFilterLib*Null*.inf' 'RegisterFilterLib null instance')" || {
      [ "$?" -eq 1 ] || fail "Multiple RegisterFilterLib fallback instances were found; set a supported EDK2 tree or update the explicit resolver."
      regfilter=""
    }
  fi

  if [ -n "$regfilter" ] && [ -f "$regfilter" ]; then
    reg_rel="${regfilter#"$WORKSPACE"/}"
    log "Using RegisterFilterLib instance: $reg_rel"
  else
    warn "RegisterFilterLibNull instance was not found in this EDK2 tree. Build may fail if BaseLib requires it."
  fi

  # Newer EDK2 UefiDriverEntryPoint consumes StackCheckLib. Some trees place it
  # under BaseStackCheckLib, while others may rename/move it. Detect dynamically.
  if [ -f "$WORKSPACE/MdePkg/Library/BaseStackCheckLib/BaseStackCheckLib.inf" ]; then
    stackcheck="$WORKSPACE/MdePkg/Library/BaseStackCheckLib/BaseStackCheckLib.inf"
  elif [ -f "$WORKSPACE/MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf" ]; then
    stackcheck="$WORKSPACE/MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf"
  else
    stackcheck="$(find_unique_file "$WORKSPACE/MdePkg/Library" '*StackCheckLib*.inf' 'StackCheckLib instance')" || {
      [ "$?" -eq 1 ] || fail "Multiple StackCheckLib fallback instances were found; set a supported EDK2 tree or update the explicit resolver."
      stackcheck=""
    }
  fi

  if [ -n "$stackcheck" ] && [ -f "$stackcheck" ]; then
    stack_rel="${stackcheck#"$WORKSPACE"/}"
    log "Using StackCheckLib instance: $stack_rel"
  else
    warn "StackCheckLib instance was not found in this EDK2 tree. Build may fail if UefiDriverEntryPoint requires it."
  fi

  python3 - "$dsc" "$reg_rel" "$stack_rel" <<'PY_DSC_PATCH'
from pathlib import Path
import sys

p = Path(sys.argv[1])
classes = {}
if len(sys.argv) > 2 and sys.argv[2]:
    classes['RegisterFilterLib'] = sys.argv[2]
if len(sys.argv) > 3 and sys.argv[3]:
    classes['StackCheckLib'] = sys.argv[3]

if not classes:
    raise SystemExit(0)

lines = p.read_text().splitlines()
out = []
replaced = {name: False for name in classes}
in_library = False
inserted_missing = False

for line in lines:
    stripped = line.strip()
    if stripped == '[LibraryClasses]':
        in_library = True
        out.append(line)
        continue

    if in_library and stripped.startswith('[') and stripped != '[LibraryClasses]':
        for name, rel in classes.items():
            if not replaced[name]:
                out.append(f'  {name}|{rel}')
                replaced[name] = True
        inserted_missing = True
        in_library = False
        out.append(line)
        continue

    matched = False
    for name, rel in classes.items():
        if stripped.startswith(name + '|'):
            out.append(f'  {name}|{rel}')
            replaced[name] = True
            matched = True
            break
    if matched:
        continue

    out.append(line)

if in_library and not inserted_missing:
    for name, rel in classes.items():
        if not replaced[name]:
            out.append(f'  {name}|{rel}')
            replaced[name] = True

p.write_text('\n'.join(out) + '\n')
PY_DSC_PATCH
}

select_toolchain() {
  if [ -n "${TOOL_CHAIN_TAG:-}" ]; then
    printf '%s' "$TOOL_CHAIN_TAG"
    return 0
  fi

  case "$(uname -s)" in
    Darwin) printf 'XCODE5' ;;
    *)      printf 'GCC5' ;;
  esac
}

files_are_identical() {
  local reference="$1"
  local candidates="$2"
  local candidate=""

  [ -f "$reference" ] || return 1

  while IFS= read -r candidate; do
    [ -n "$candidate" ] || continue
    [ -f "$candidate" ] || return 1
    cmp -s "$reference" "$candidate" || return 1
  done <<EOF_IDENTICAL_FILES
$candidates
EOF_IDENTICAL_FILES

  return 0
}

find_output() {
  local target="$1"
  local toolchain="$2"
  local build_root="$WORKSPACE/Build/$PKG/${target}_${toolchain}/X64"
  local canonical="$build_root/${APP}.efi"
  local matches=""
  local output_matches=""
  local count=0
  local output_count=0
  local selected=""

  matches="$(find "$build_root" -name "${APP}.efi" -type f -print 2>/dev/null | LC_ALL=C sort || true)"
  if [ -n "$matches" ]; then
    count="$(printf '%s\n' "$matches" | sed '/^[[:space:]]*$/d' | wc -l | tr -d '[:space:]')"
  fi

  case "$count" in
    0)
      return 1
      ;;
    1)
      [ -f "$matches" ] || return 1
      printf '%s' "$matches"
      return 0
      ;;
  esac

  # Current EDK2 emits a canonical architecture-level EFI and may also keep
  # byte-identical module OUTPUT/DEBUG copies. These are not ambiguous builds.
  # Prefer the canonical copy, but verify every discovered copy before use so a
  # divergent or stale artifact can never be packaged silently.
  if [ -f "$canonical" ]; then
    selected="$canonical"
  else
    output_matches="$(printf '%s\n' "$matches" | grep '/OUTPUT/' || true)"
    if [ -n "$output_matches" ]; then
      output_count="$(printf '%s\n' "$output_matches" | sed '/^[[:space:]]*$/d' | wc -l | tr -d '[:space:]')"
    fi
    if [ "$output_count" -eq 1 ] 2>/dev/null; then
      selected="$output_matches"
    else
      selected="$(printf '%s\n' "$matches" | sed -n '1p')"
    fi
  fi

  if files_are_identical "$selected" "$matches"; then
    log "Found $count byte-identical ${APP}.efi copies for ${target}_${toolchain}; using: $selected" >&2
    printf '%s' "$selected"
    return 0
  fi

  warn "Divergent ${APP}.efi outputs for ${target}_${toolchain}; refusing to package any candidate:" >&2
  printf '%s\n' "$matches" >&2
  return 2
}

validate_efi_image() {
  local image="$1"
  local target="$2"

  python3 - "$image" "$target" <<'PY_EFI_VALIDATE' || fail "Invalid EFI PE/COFF image produced for $target: $image"
from pathlib import Path
import struct
import sys

path = Path(sys.argv[1])
target = sys.argv[2]
data = path.read_bytes()

def reject(message):
    raise SystemExit(f"{path}: {message}")

def u16(offset):
    if offset < 0 or offset > len(data) - 2:
        reject(f"truncated 16-bit field at file offset 0x{offset:x}")
    return struct.unpack_from("<H", data, offset)[0]

def u32(offset):
    if offset < 0 or offset > len(data) - 4:
        reject(f"truncated 32-bit field at file offset 0x{offset:x}")
    return struct.unpack_from("<I", data, offset)[0]

if len(data) < 0x40 or data[:2] != b"MZ":
    reject("missing DOS MZ header")
pe_offset = u32(0x3C)
if pe_offset < 0x40 or pe_offset > len(data) - 24 or data[pe_offset:pe_offset + 4] != b"PE\x00\x00":
    reject("missing or out-of-range PE signature")
coff_offset = pe_offset + 4
machine = u16(coff_offset)
if machine != 0x8664:
    reject(f"machine 0x{machine:04x} is not X64 (0x8664)")
number_of_sections = u16(coff_offset + 2)
if number_of_sections == 0:
    reject("PE image has zero sections")
optional_size = u16(coff_offset + 16)
characteristics = u16(coff_offset + 18)
if (characteristics & 0x0002) == 0:
    reject("COFF Characteristics does not mark an executable image")
optional_offset = pe_offset + 24
if optional_size < 112 or optional_offset > len(data) - optional_size:
    reject("truncated PE32+ optional header")
magic = u16(optional_offset)
if magic != 0x20B:
    reject(f"optional-header magic 0x{magic:04x} is not PE32+ (0x20b)")
address_of_entry_point = u32(optional_offset + 16)
section_alignment = u32(optional_offset + 32)
file_alignment = u32(optional_offset + 36)
# PE/COFF normally uses FileAlignment >= 0x200.  There is a documented
# sub-page image exception: when SectionAlignment is below the architecture
# page size, FileAlignment must exactly match SectionAlignment.  EDK2 XCODE5
# (mtoc -align 0x20) uses this form for UEFI drivers on macOS.
if file_alignment == 0 or file_alignment > 0x10000 or (file_alignment & (file_alignment - 1)) != 0:
    reject(f"invalid FileAlignment: 0x{file_alignment:x}")
if section_alignment == 0 or (section_alignment & (section_alignment - 1)) != 0:
    reject(f"invalid SectionAlignment: 0x{section_alignment:x}")
if section_alignment < file_alignment:
    reject(f"invalid SectionAlignment/FileAlignment relationship: 0x{section_alignment:x}/0x{file_alignment:x}")
subpage_alignment = section_alignment < 0x1000
if subpage_alignment:
    if file_alignment != section_alignment:
        reject(
            f"sub-page PE image requires FileAlignment == SectionAlignment: "
            f"0x{file_alignment:x}/0x{section_alignment:x}"
        )
else:
    if file_alignment < 0x200:
        reject(f"invalid FileAlignment for page-aligned PE image: 0x{file_alignment:x}")
subsystem = u16(optional_offset + 68)
if subsystem != 11:
    reject(f"subsystem != 11 (EFI Boot Service Driver): {subsystem}")
size_of_image = u32(optional_offset + 56)
size_of_headers = u32(optional_offset + 60)
if size_of_image == 0 or size_of_headers == 0 or size_of_headers > len(data):
    reject("invalid PE image/header size fields")
if size_of_image < size_of_headers:
    reject("SizeOfImage is smaller than SizeOfHeaders")
if (size_of_image % section_alignment) != 0:
    reject("SizeOfImage is not aligned to SectionAlignment")
if (size_of_headers % file_alignment) != 0:
    reject("SizeOfHeaders is not aligned to FileAlignment")
number_of_rva_and_sizes = u32(optional_offset + 108)
directory_capacity = (optional_size - 112) // 8
if number_of_rva_and_sizes > directory_capacity:
    reject(
        f"NumberOfRvaAndSizes={number_of_rva_and_sizes} exceeds optional-header directory capacity={directory_capacity}"
    )

section_table = optional_offset + optional_size
section_table_size = number_of_sections * 40
if section_table > len(data) - section_table_size:
    reject("truncated PE section table")
if section_table + section_table_size > size_of_headers:
    reject("PE section table extends beyond SizeOfHeaders")

entry_mapped = False
max_raw_end = size_of_headers
raw_ranges = []
previous_raw_offset = -1
previous_virtual_end = 0
for index in range(number_of_sections):
    section = section_table + index * 40
    raw_name = data[section:section + 8].split(b"\x00", 1)[0]
    name = raw_name.decode("ascii", "replace") or f"#{index + 1}"
    virtual_size = u32(section + 8)
    virtual_address = u32(section + 12)
    raw_size = u32(section + 16)
    raw_offset = u32(section + 20)
    virtual_span = max(virtual_size, raw_size)
    section_characteristics = u32(section + 36)

    if raw_size != 0:
        if raw_offset == 0:
            reject(f"section {name} has raw data with zero file offset")
        if (raw_offset % file_alignment) != 0 or (raw_size % file_alignment) != 0:
            reject(f"section {name} raw data is not aligned to FileAlignment")
        if raw_offset < size_of_headers:
            reject(f"section {name} raw data overlaps PE headers")
        if raw_offset < previous_raw_offset:
            reject(f"section {name} raw data is not ordered by section RVA")
        previous_raw_offset = raw_offset
        if subpage_alignment and raw_offset != virtual_address:
            reject(
                f"section {name} sub-page file offset 0x{raw_offset:x} "
                f"does not match RVA 0x{virtual_address:x}"
            )
        if raw_offset > len(data) or raw_size > len(data) - raw_offset:
            reject(f"section {name} raw data extends beyond end of file")
        raw_end = raw_offset + raw_size
        for other_start, other_end, other_name in raw_ranges:
            if raw_offset < other_end and other_start < raw_end:
                reject(f"section {name} raw data overlaps section {other_name}")
        raw_ranges.append((raw_offset, raw_end, name))
        max_raw_end = max(max_raw_end, raw_end)
    if virtual_span != 0:
        if virtual_address < size_of_headers:
            reject(f"section {name} virtual span overlaps PE headers")
        if virtual_address < previous_virtual_end:
            reject(f"section {name} virtual span overlaps or precedes a previous section")
        previous_virtual_end = virtual_address + virtual_span
        if (virtual_address % section_alignment) != 0:
            reject(f"section {name} virtual address is not aligned to SectionAlignment")
        if virtual_address >= size_of_image or virtual_span > size_of_image - virtual_address:
            reject(f"section {name} virtual span extends beyond SizeOfImage")
        # The entry point must name actual executable bytes, not .bss,
        # zero-filled virtual tail, or a non-executable data section.
        if (section_characteristics & 0x20000000) != 0 and raw_size != 0 and (
            virtual_address <= address_of_entry_point < virtual_address + raw_size
        ):
            entry_mapped = True

if address_of_entry_point == 0:
    reject("EFI driver has a zero AddressOfEntryPoint")
if address_of_entry_point >= size_of_image or not entry_mapped:
    reject("EFI driver entry point is not backed by an executable section")

security = "absent"
if number_of_rva_and_sizes > 4:
    cert_directory = optional_offset + 112 + 4 * 8
    cert_offset = u32(cert_directory)
    cert_size = u32(cert_directory + 4)
    if cert_offset != 0 or cert_size != 0:
        if cert_offset == 0 or cert_size == 0:
            reject("malformed Authenticode certificate table")
        if (cert_offset & 7) != 0:
            reject("Authenticode certificate table is not 8-byte aligned")
        if cert_offset < max_raw_end:
            reject("Authenticode certificate table overlaps PE headers or section raw data")
        if cert_offset > len(data) or cert_size > len(data) - cert_offset:
            reject("malformed Authenticode certificate table")
        cert_end = cert_offset + cert_size
        cursor = cert_offset
        entries = 0
        while cursor < cert_end:
            if cert_end - cursor < 8:
                reject("truncated WIN_CERTIFICATE header")
            cert_length = u32(cursor)
            if cert_length < 8:
                reject("malformed WIN_CERTIFICATE length")
            if cert_length > cert_end - cursor:
                reject("WIN_CERTIFICATE extends beyond certificate table")
            next_cursor = cursor + ((cert_length + 7) & ~7)
            if next_cursor > cert_end:
                reject("WIN_CERTIFICATE alignment padding exceeds certificate table")
            entries += 1
            cursor = next_cursor
        if cursor != cert_end or entries == 0:
            reject("malformed Authenticode certificate table")
        security = f"present ({cert_size} bytes, {entries} certificate entr{'y' if entries == 1 else 'ies'})"

print(
    f"Validated {target} EFI: PE32+ X64, sections={number_of_sections}, "
    f"subsystem=EFI Boot Service Driver, entry=0x{address_of_entry_point:x}, "
    f"Authenticode={security}, bytes={len(data)}"
)
PY_EFI_VALIDATE
}

build_one() {
  local target="$1"
  local outdir="$2"
  local status
  local efi

  mkdir -p "$outdir"

  log "Building $target..."
  build -a X64 -b "$target" -t "$TOOL_CHAIN" -p "$DSC_REL"
  status=$?

  if [ "$status" -ne 0 ]; then
    log "FAILED: $target build with status $status"
    log "Build log: $MASTER_LOG"
    exit "$status"
  fi

  efi="$(find_output "$target" "$TOOL_CHAIN")"
  status=$?
  case "$status" in
    0) ;;
    1) fail "Build succeeded but ${APP}.efi was not found for $target. Check $MASTER_LOG" ;;
    2) fail "Build produced divergent ${APP}.efi candidates for $target. Check $MASTER_LOG" ;;
    *) fail "Unexpected output-resolution failure for $target (status $status). Check $MASTER_LOG" ;;
  esac
  validate_efi_image "$efi" "$target"
  cp "$efi" "$outdir/${APP}.efi" || fail "Could not copy $efi to $outdir"
  if [ "$target" = "DEBUG" ]; then
    cp HDAUChimeDebug.ini "$outdir/HDAUChimeDebug.ini" || fail "Could not copy debug config to $outdir"
  else
    cp HDAUChimeRelease.ini "$outdir/HDAUChimeRelease.ini" || fail "Could not copy release config to $outdir"
  fi
  log "$target output: $outdir/${APP}.efi"
}

log "===== $APP $VERSION build ====="
log "Date: $(date)"
log "Source dir: $PWD"
log "Shell: ${SHELL:-unknown}"
log "uname: $(uname -a)"
log "AUTO_FETCH_EDK2: $AUTO_FETCH_EDK2"
log "EDK2_DEFAULT_DIR: $EDK2_DEFAULT_DIR"
log "EDK2_REF: $EDK2_REF"
log "EDK2_EXPECTED_COMMIT: ${EDK2_EXPECTED_COMMIT:-<none>}"
log "SOURCE_TEST_ROUNDS: $SOURCE_TEST_ROUNDS"

preflight_tools
clean_transient_source_metadata
verify_source_tree_hygiene
run_source_regressions
prepare_edk2
stage_package_for_build
resolve_edk2_library_paths
TOOL_CHAIN="$(select_toolchain)"

log "WORKSPACE: $WORKSPACE"
log "EDK_TOOLS_PATH: ${EDK_TOOLS_PATH:-}"
log "PACKAGES_PATH: ${PACKAGES_PATH:-}"
log "TOOL_CHAIN_TAG: $TOOL_CHAIN"
log "DSC: $DSC_REL"

build_one DEBUG Release/Debug
build_one RELEASE Release/Release

# Build an EFI copy-test pack so the driver can be tested without UEFI Shell.
TEST_EFI="Release/EFI-Test-Pack"
rm -rf "$TEST_EFI"
mkdir -p "$TEST_EFI/EFI/CLOVER/drivers/UEFI" \
         "$TEST_EFI/EFI/OC/Drivers" \
         "$TEST_EFI/EFI/HDAUniversalBootChimeDxe/Logs"
cp "Release/Debug/${APP}.efi" "$TEST_EFI/EFI/CLOVER/drivers/UEFI/${APP}.efi" || fail "Could not stage Clover EFI test driver"
cp "Release/Debug/${APP}.efi" "$TEST_EFI/EFI/OC/Drivers/${APP}.efi" || fail "Could not stage OpenCore EFI test driver"
cp HDAUChimeDebug.ini "$TEST_EFI/EFI/HDAUniversalBootChimeDxe/HDAUChimeDebug.ini" || fail "Could not stage debug config"
cp docs/NO-SHELL-EFI-TEST.md "$TEST_EFI/README-NO-SHELL-EFI-TEST.md" || fail "Could not stage EFI test README"

ZIP="Release/${APP}-${VERSION}-DEBUG-RELEASE.zip"
rm -f "$ZIP"
(
  cd Release || exit 1
  zip -qry "$(basename "$ZIP")" Debug Release EFI-Test-Pack
) || fail "Could not create release zip"
unzip -tq "$ZIP" >/dev/null || fail "Release zip integrity test failed: $ZIP"

log "Done. Package: $ZIP"
log "No-shell EFI test pack: $TEST_EFI"
find Release -maxdepth 7 -type f -print
log "Master log: $MASTER_LOG"
exit 0
