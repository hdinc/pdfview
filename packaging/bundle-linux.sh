#!/usr/bin/env bash
# Packs pdfview-app and the shared libraries it needs into <out>/<name>.zip:
#   <name>/pdfview-app, <name>/lib/*.so*
# The program finds lib/ through its $ORIGIN runpath. Libraries that every
# desktop has, or that must match the system (glibc, libstdc++, GL, X11,
# fontconfig and freetype, which read the system's font config), are left out.
# Needs ldd, patchelf and zip.
# usage: packaging/bundle-linux.sh <build dir> <name> <out dir>
set -euo pipefail
build=$1 name=$2 out=$3

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
dir=$stage/$name
mkdir -p "$dir/lib" "$out"
out=$(cd "$out" && pwd)
cp "$build/bin/pdfview-app" "$dir/"
strip "$dir/pdfview-app"

skip='^(linux-vdso|ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libstdc\+\+|libgcc_s|libGL|libGLX|libEGL|libOpenGL|libGLdispatch|libX11|libXext|libXrender|libXau|libXdmcp|libxcb.*|libfontconfig|libfreetype|libexpat|libz)\.so'
ldd "$build/bin/pdfview-app" | awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }' |
while read -r soname path; do
    soname=${soname##*/}
    [[ $soname =~ $skip ]] && continue
    cp -L "$path" "$dir/lib/$soname"
done
if ldd "$build/bin/pdfview-app" | grep -q 'not found'; then
    echo "unresolved libraries:" >&2
    ldd "$build/bin/pdfview-app" | grep 'not found' >&2
    exit 1
fi

patchelf --set-rpath '$ORIGIN/lib' "$dir/pdfview-app"
for f in "$dir"/lib/*; do
    chmod u+w "$f"
    patchelf --set-rpath '$ORIGIN' "$f"
    strip --strip-unneeded "$f"
done
cp "$(dirname "$0")/../README.md" "$(dirname "$0")/../LICENSE" "$dir/"

rm -f "$out/$name.zip"
(cd "$stage" && zip -qr9 "$out/$name.zip" "$name")
echo "$out/$name.zip"
