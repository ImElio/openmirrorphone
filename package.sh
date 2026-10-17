#!/usr/bin/env bash
# Build a GUI (no console) release, collect it with its DLLs in dist/ (runs on a
# PC without MSYS2) and zip it as release/OpenMirrorPhone-<version>-win64.zip,
# the file to attach to a GitHub release. Usage: ./package.sh
set -euo pipefail
cd "$(dirname "$0")"
export MSYSTEM=UCRT64
/c/msys64/usr/bin/bash -lc "cd '$(pwd -W)' && cmake -S . -B build-gui -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENMIRRORPHONE_GUI=ON >/dev/null && cmake --build build-gui"

rm -rf dist && mkdir -p dist
cp build-gui/openmirrorphone.exe dist/OpenMirrorPhone.exe

# Copy every DLL the exe needs, directly or through other DLLs, that comes from
# MSYS2. This reads the import tables instead of asking Windows where it would
# load each DLL from: on the build PC a DLL can also sit in System32 (installed by
# some other app) and would then be missed, breaking the app on every other PC.
UCRT=/c/msys64/ucrt64/bin
imports() { "$UCRT/objdump" -p "$1" | sed -n 's/^\s*DLL Name: //p' | tr -d '\r' | tr '[:upper:]' '[:lower:]'; }
declare -A msys     # lower-case DLL name -> file in MSYS2
for f in "$UCRT"/*.dll; do b=${f##*/}; msys[${b,,}]=$f; done
declare -A copied
queue=(dist/OpenMirrorPhone.exe)
while ((${#queue[@]})); do
    file=${queue[0]}; queue=("${queue[@]:1}")
    while read -r dll; do
        [[ -n ${msys[$dll]:-} && -z ${copied[$dll]:-} ]] || continue
        cp "${msys[$dll]}" dist/
        copied[$dll]=1
        queue+=("dist/${msys[$dll]##*/}")
    done < <(imports "$file")
done
cp third_party/airplay/LICENSE.UxPlay dist/LICENSE.txt

# Safety check: every import that MSYS2 provides must now be in dist/.
missing=0
for f in dist/*.exe dist/*.dll; do
    while read -r dll; do
        if [[ -n ${msys[$dll]:-} && -z ${copied[$dll]:-} ]]; then echo "MISSING: $dll (needed by $f)"; missing=1; fi
    done < <(imports "$f")
done
((missing == 0)) || { echo "package.sh: dist/ is incomplete" >&2; exit 1; }
echo "dist/: $(ls dist | wc -l) files, $(du -sh dist | cut -f1)"

# The version comes from project(... VERSION x.y.z) in CMakeLists.txt.
version=$(sed -n 's/^project(OpenMirrorPhone VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
zip="OpenMirrorPhone-${version}-win64.zip"
mkdir -p release && rm -f "release/$zip"
# Files at the root of the zip, as the auto-updater expects. Windows' tar writes zips (-a).
(cd dist && /c/Windows/System32/tar.exe -a -c -f "../release/$zip" *)
echo "release/$zip: $(du -h "release/$zip" | cut -f1)"

# The installer people download (the zip above is what the auto-updater uses).
setup="OpenMirrorPhone-${version}-setup.exe"
rm -f "release/$setup"
"$UCRT/rsvg-convert" installer/welcome.svg -o build-gui/welcome-bg.png
powershell -NoProfile -ExecutionPolicy Bypass -File installer/make-welcome-bmp.ps1 \
    -Background "$(cygpath -w build-gui/welcome-bg.png)" -Icon "$(cygpath -w assets/icons/icon_128.png)" \
    -Out "$(cygpath -w build-gui/welcome.bmp)"
/c/msys64/usr/bin/bash -lc "cd '$(pwd -W)/installer' && makensis -V2 -DVERSION=$version -DDIST='$(pwd -W)/dist' -DOUTFILE='$(pwd -W)/release/$setup' OpenMirrorPhone.nsi"
echo "release/$setup: $(du -h "release/$setup" | cut -f1)"
