#!/usr/bin/env bash
# Optional UEFI PE/COFF Authenticode signing with owner-supplied Secure Boot keys.
# This is NOT macOS ad-hoc codesign; a signed image still requires firmware trust.
set -euo pipefail

usage() {
  printf 'Usage: %s <db-private-key.pem> <db-certificate.pem> <input.efi> <output.efi>\n' "$0" >&2
  printf 'The certificate must be trusted by your firmware Secure Boot db.\n' >&2
}

if [ "$#" -ne 4 ]; then
  usage
  exit 64
fi

key=$1
cert=$2
input=$3
output=$4

for tool in sbsign sbverify mktemp python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'ERROR: required tool not found: %s\n' "$tool" >&2
    exit 69
  fi
done

if [ ! -f "$key" ] || [ ! -f "$cert" ] || [ ! -s "$input" ]; then
  printf 'ERROR: key, certificate, or nonempty input EFI is missing.\n' >&2
  exit 66
fi
if [ "$input" = "$output" ] || [ -e "$output" ] || [ -L "$output" ]; then
  printf 'ERROR: output must be a new file distinct from the input (no overwrite or symlink).\n' >&2
  exit 73
fi
outdir=$(dirname "$output")
if [ ! -d "$outdir" ]; then
  printf 'ERROR: output directory does not exist: %s\n' "$outdir" >&2
  exit 73
fi

tmp=$(mktemp "$outdir/.hdau-sign.XXXXXXXX")
cleanup() { rm -f "$tmp"; }
trap cleanup EXIT HUP INT TERM

# Never copy or emit key bytes; tools read the user-controlled key in place.
sbsign --key "$key" --cert "$cert" --output "$tmp" "$input"
if [ ! -s "$tmp" ]; then
  printf 'ERROR: signing produced an empty image.\n' >&2
  exit 1
fi
sbverify --cert "$cert" "$tmp"
chmod 644 "$tmp"
# Publish with linkat() relative to a single, opened directory FD. Unlike
# `ln`, this never treats a concurrently created directory or symlink-to-dir
# as a destination directory. O_EXCL-like behavior is supplied by linkat:
# any existing destination entry makes the publish fail without overwriting it.
# The output directory must remain operator-controlled; another process with
# write access could still replace an image *after* publication.
python3 - "$tmp" "$output" <<'PYTHON_PUBLISH'
import os
import sys

staged, destination = sys.argv[1:]
staged_directory = os.path.abspath(os.path.dirname(staged))
destination_directory = os.path.abspath(os.path.dirname(destination))
if staged_directory != destination_directory:
    raise SystemExit("ERROR: signing staging and destination directories differ")
fd = os.open(destination_directory,
             os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
try:
    os.link(os.path.basename(staged), os.path.basename(destination),
            src_dir_fd=fd, dst_dir_fd=fd, follow_symlinks=False)
finally:
    os.close(fd)
PYTHON_PUBLISH
rm -f "$tmp"
trap - EXIT HUP INT TERM
printf 'Cryptographic signature checked with the supplied certificate: %s\n' "$output"
printf 'Firmware db enrollment and Secure Boot acceptance must be tested separately.\n'
