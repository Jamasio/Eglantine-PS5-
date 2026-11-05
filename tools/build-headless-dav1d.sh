#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# dav1d for the PS5: AV1 decoding for Switch 2 titles' movies (Zelda: Ocarina of Time configures
# "video/av1" 1920x1080). FFmpeg's libdav1d decoder (tools/build-headless-ffmpeg.sh) uses it; the
# core's desktop builds get the same decoder from a prebuilt FFmpeg (externals/ffmpeg/CMakeLists.txt).
# Installs a static libdav1d.a, its headers and dav1d.pc into FFmpeg's install prefix, where the
# Eden build finds it beside libavcodec.a (headless/CMakeLists.txt links it).
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(cat "$root/.local/headless-cache")
[[ "$(cat "$scratch/owner")" == "$root" ]]
export PS5_PAYLOAD_SDK="$scratch/sdk"
source="$root/.deps/dav1d-1.5.1"
test -f "$source/meson.build" || { echo 'Run make deps first (dav1d).' >&2; exit 1; }
build="$scratch/dav1d-native"
prefix="$scratch/ffmpeg-native/install"
mkdir -p "$build"
# The PS5 is a FreeBSD-like x86-64 target: same compiler wrapper as FFmpeg (it links configure
# probes against the console's libraries and never runs them).
cat > "$build/ps5.cross" <<EOF
[binaries]
c = ['bash', '$root/tools/ffmpeg-ps5-cc.sh']
ar = 'llvm-ar-18'
ranlib = 'llvm-ranlib-18'
strip = 'llvm-strip-18'
nasm = 'nasm'

[host_machine]
system = 'freebsd'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
needs_exe_wrapper = true
EOF
options=(--cross-file "$build/ps5.cross" --prefix="$prefix" --libdir=lib --buildtype=release
    --default-library=static -Denable_tools=false -Denable_tests=false -Denable_examples=false
    -Denable_docs=false -Dlogging=false -Denable_asm=true -Db_staticpic=true)
if [[ -f "$build/build/build.ninja" ]]; then
    meson setup --reconfigure "$build/build" "$source" "${options[@]}"
else
    meson setup "$build/build" "$source" "${options[@]}"
fi
ninja -C "$build/build" -j"${EDEN_BUILD_JOBS:-$(nproc)}"
meson install -C "$build/build" --quiet
# Meson puts .pc files in libdata/pkgconfig on a FreeBSD host; FFmpeg's configure looks in lib/pkgconfig.
find "$prefix" -name 'dav1d*' -o -name 'libdav1d*'
if [[ -f "$prefix/libdata/pkgconfig/dav1d.pc" ]]; then
    mkdir -p "$prefix/lib/pkgconfig"
    cp "$prefix/libdata/pkgconfig/dav1d.pc" "$prefix/lib/pkgconfig/dav1d.pc"
fi
test -f "$prefix/lib/libdav1d.a" && test -f "$prefix/lib/pkgconfig/dav1d.pc"
