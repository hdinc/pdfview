#!/usr/bin/env bash
# Packs pdfview-app.exe and the MSYS2 DLLs it needs into <out>/<name>.zip,
# run from an MSYS2 UCRT64 shell:
#   <name>/pdfview-app.exe, *.dll, etc/fonts (fontconfig's config),
#   share/poppler (poppler-data, for CJK and other encodings)
# fontconfig and poppler look for etc/ and share/ next to their DLLs.
# usage: packaging/bundle-windows.sh <build dir> <name> <out dir>
set -euo pipefail
build=$1 name=$2 out=$3
prefix=${MINGW_PREFIX:-/ucrt64}

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
dir=$stage/$name
mkdir -p "$dir/etc" "$dir/share" "$out"
out=$(cd "$out" && pwd)
cp "$build/bin/pdfview-app.exe" "$dir/"
strip "$dir/pdfview-app.exe"

# ldd lists the DLLs recursively; the system's are under /c/Windows
ldd "$build/bin/pdfview-app.exe" | awk -v p="$prefix/" '$2 == "=>" && index($3, p) == 1 { print $3 }' |
    sort -u | while read -r dll; do cp "$dll" "$dir/"; done
if ldd "$build/bin/pdfview-app.exe" | grep -q 'not found'; then
    echo "unresolved DLLs:" >&2
    ldd "$build/bin/pdfview-app.exe" | grep 'not found' >&2
    exit 1
fi

cp -rL "$prefix/etc/fonts" "$dir/etc/"
if [ -d "$prefix/share/poppler" ]; then cp -rL "$prefix/share/poppler" "$dir/share/"; fi
cp "$(dirname "$0")/../README.md" "$(dirname "$0")/../LICENSE" "$dir/"

rm -f "$out/$name.zip"
(cd "$stage" && zip -qr9 "$out/$name.zip" "$name")
echo "$out/$name.zip"
