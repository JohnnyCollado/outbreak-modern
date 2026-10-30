#!/usr/bin/env bash
# build-all.bat's work: builds the PC runner and the Android APK from the net trees (tools/net/net-env.sh, which hold
# the current patches), then gathers what a player needs into dist/. Only named files are copied: the build folders'
# saves, settings, mods and logs never reach dist/, and no game data is included (the player supplies the disc).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIST="$ROOT/dist"
cd "$ROOT" || exit 1
source tools/net/net-env.sh

echo "== PC version"
# The build tree remembers the checkout it was configured from: point it here if it was another (a worktree, say).
HERE=$(pwd -W)
if ! grep -qxF "PS2X_EXTERNAL_GS_DIR:PATH=$HERE/native/pcsx2_gs" "$OB_R/CMakeCache.txt"; then
    VC="$HERE/tools/ob/vc.bat"
    MSYS_NO_PATHCONV=1 cmd /c "${VC//\//\\}" cmake -S "$OB_SRC" -B "$OB_R" "-DPS2X_EXTERNAL_GS_DIR=$HERE/native/pcsx2_gs" \
        "-DPS2X_EXTERNAL_VU1_DIR=$HERE/native/pcsx2_vu" > "$OB_LOGS/dist-configure.log" 2>&1 ||
        { echo "PC configure failed ($OB_LOGS/dist-configure.log)"; exit 1; }
fi
bash tools/net/net-runner.sh || { echo "PC build failed"; exit 1; }
echo "== Android version"
bash tools/net/net-android.sh configure || { echo "Android configure failed"; exit 1; }
bash tools/net/net-android.sh build || { echo "Android library build failed"; exit 1; }
bash tools/net/net-android.sh package || { echo "Android package failed"; exit 1; }

echo "== dist"
R="${OB_R:-C:/ob/net-r}/ps2xRuntime"
rm -rf "$DIST/windows" "$DIST/android"
mkdir -p "$DIST/windows" "$DIST/android" || exit 1
cp "$R/ps2EntryRunner.exe" "$DIST/windows/OutbreakModern.exe" &&
cp "$R"/*.dll "$R/outbreak-icon.png" "$DIST/windows/" &&
cp -r "$R/resources" "$DIST/windows/" &&
cp COPYING THIRD_PARTY_NOTICES.md "$DIST/windows/" &&
cp C:/ob/net-android-apk/outbreak-modern.apk "$DIST/android/outbreak-modern.apk" || { echo "Copying to dist failed"; exit 1; }

# The outputs exist, and nothing personal came along.
fail=0
for f in windows/OutbreakModern.exe windows/shaderc_shared.dll windows/resources/shaders android/outbreak-modern.apk; do
    [ -e "$DIST/$f" ] || { echo "FAIL: missing dist/$f"; fail=1; }
done
stray=$(find "$DIST" \( -iname '*.iso' -o -iname '*.ps2' -o -iname 'outbreak.ini' -o -iname 'input.ini' -o -iname 'net.env' -o -iname '*.log' -o -iname '*.keystore' \))
[ -z "$stray" ] || { echo "FAIL: should not be in dist: $stray"; fail=1; }
[ $fail = 0 ] || exit 1
du -sh "$DIST/windows" "$DIST/android"
