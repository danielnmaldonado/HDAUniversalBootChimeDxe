#!/bin/bash
# HDAUniversalBootChimeDxe
# Copyright © 2026 DNMTechLabs
# Contact: dnmtechlabs@gmail.com
# Boot chime DXE driver for Clover/OpenCore.

# Optional helper: installs EDK2 into ~/edk2 for HDAUniversalBootChimeDxe builds.
# macOS /bin/bash 3.2 compatible.

set -uo pipefail
cd "$(dirname "$0")" || exit 1

fail() { echo "ERROR: $*" >&2; exit 1; }
log() { echo "[Setup-EDK2] $*"; }
cpu_count() {
  local n=""

  if command -v getconf >/dev/null 2>&1; then
    n="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
  fi
  case "$n" in
    ''|*[!0-9]*) n="" ;;
    *) [ "$n" -ge 1 ] 2>/dev/null || n="" ;;
  esac
  if [ -z "$n" ] && command -v sysctl >/dev/null 2>&1; then
    n="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
  fi
  case "$n" in
    ''|*[!0-9]*) n=1 ;;
    *) [ "$n" -ge 1 ] 2>/dev/null || n=1 ;;
  esac
  printf '%s' "$n"
}

EDK2_DIR="${EDK2_DIR:-$HOME/edk2}"
EDK2_DEFAULT_REF="edk2-stable202608"
EDK2_REF="${EDK2_REF:-$EDK2_DEFAULT_REF}"
EDK2_EXPECTED_COMMIT="${EDK2_EXPECTED_COMMIT:-}"
if [ "$EDK2_REF" = "$EDK2_DEFAULT_REF" ] && [ -z "$EDK2_EXPECTED_COMMIT" ]; then
  EDK2_EXPECTED_COMMIT="2970e5699ba6267f3384ffab20f96647578aebc8"
fi

log "Target: $EDK2_DIR"
log "Reference: $EDK2_REF"
log "Expected commit: ${EDK2_EXPECTED_COMMIT:-<not pinned for custom ref>}"
command -v git >/dev/null 2>&1 || fail "git not found"
command -v python3 >/dev/null 2>&1 || fail "python3 not found"
python3 -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 8) else 1)' \
  || fail "Python 3.8 or newer is required by current EDK2 and the AppleALC data generator"
command -v make >/dev/null 2>&1 || fail "make not found"

if [ "$(uname -s)" = "Darwin" ]; then
  xcode-select -p >/dev/null 2>&1 || fail "Xcode Command Line Tools missing. Run: xcode-select --install"
fi

if [ -e "$EDK2_DIR" ] && [ ! -d "$EDK2_DIR/.git" ]; then
  fail "Target exists but is not an EDK2 git checkout: $EDK2_DIR"
fi
if [ ! -d "$EDK2_DIR/.git" ]; then
  mkdir -p "$(dirname "$EDK2_DIR")" || fail "Could not create parent directory"
  git clone --depth 1 --branch "$EDK2_REF" --single-branch https://github.com/tianocore/edk2.git "$EDK2_DIR" || fail "git clone failed for $EDK2_REF"
fi

[ -f "$EDK2_DIR/edksetup.sh" ] || fail "Existing git checkout is not a valid EDK2 workspace: missing edksetup.sh"
[ -d "$EDK2_DIR/MdePkg" ] || fail "Existing git checkout is not a valid EDK2 workspace: missing MdePkg"
[ -d "$EDK2_DIR/BaseTools" ] || fail "Existing git checkout is not a valid EDK2 workspace: missing BaseTools"

if ! git -C "$EDK2_DIR" rev-parse "$EDK2_REF^{commit}" >/dev/null 2>&1; then
  git -C "$EDK2_DIR" fetch --depth 1 origin "refs/tags/$EDK2_REF:refs/tags/$EDK2_REF" \
    || git -C "$EDK2_DIR" fetch --depth 1 origin "$EDK2_REF" \
    || fail "could not fetch EDK2_REF=$EDK2_REF"
fi
git -C "$EDK2_DIR" checkout --detach "$EDK2_REF" || fail "could not checkout EDK2_REF=$EDK2_REF"
head="$(git -C "$EDK2_DIR" rev-parse HEAD)"
expected="$(git -C "$EDK2_DIR" rev-parse "$EDK2_REF^{commit}")"
[ "$head" = "$expected" ] || fail "EDK2 checkout verification failed"
if [ -n "$EDK2_EXPECTED_COMMIT" ] && [ "$head" != "$EDK2_EXPECTED_COMMIT" ]; then
  fail "EDK2_REF=$EDK2_REF resolved to $head, but expected pinned commit is $EDK2_EXPECTED_COMMIT"
fi

git -C "$EDK2_DIR" submodule update --init --recursive --depth 1 || fail "submodule update failed"
make -C "$EDK2_DIR/BaseTools" -j"$(cpu_count)" || fail "BaseTools build failed"

log "Done. Use: export WORKSPACE=$EDK2_DIR"
log "Then run: ./Build-HDAUniversalBootChimeDxe.command"
