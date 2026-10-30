#!/bin/bash
# arm64 VU1 compare tool for the device run (docs/ops/android.md "VU1 on arm64"): builds native/pcsx2_vu/a64
# standalone with the NDK into $OB_A64VU_B (libob_a64vu.so + ob_vucompare_a64, which links the Android runner
# tree's libps2_runtime.a for the interpreter), strips both into $OB_A64VU_B/stage, and checks them with
# tools/ob/android_symcheck.py. Nothing here touches a device: the adb commands are in docs/ops/android.md.
#   bash tools/ob/vucompare_a64.sh          configure (first time), build, stage, symcheck
# Needs: $OB_ANDROID_R/ps2xRuntime/libps2_runtime.a (ob.sh android build), $OB_ARMSX2 at the pinned commit.
# First build ~45 s (ARMSX2's microVU TU and vixl); later builds only what changed.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd -W) || exit 1
: "${OB_ROOT:=C:/ob}" "${OB_LOGS:=$OB_ROOT/logs}" "${OB_ANDROID_R:=$OB_ROOT/android-r}" "${OB_ARMSX2:=$OB_ROOT/armsx2}"
: "${OB_A64VU_B:=$OB_ROOT/android-vu-build}"
: "${OB_ANDROID_SDK:=$(cygpath -m "$LOCALAPPDATA")/Android/Sdk}"
: "${OB_ANDROID_NDK:=$OB_ANDROID_SDK/ndk/27.0.12077973}"
PY="${OB_PY:-python}"
LLVM="$OB_ANDROID_NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
RELFLAGS="-O3 -DNDEBUG -g0" # as tools/ob/android.sh
log="$OB_LOGS/ob-vucompare-a64.log" start=$(date +%s)
mkdir -p "$OB_LOGS"
{
    [ -f "$OB_A64VU_B/build.ninja" ] ||
        cmake -S "$REPO/native/pcsx2_vu/a64" -B "$OB_A64VU_B" -G Ninja \
            "-DCMAKE_TOOLCHAIN_FILE=$OB_ANDROID_NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
            -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=$RELFLAGS" "-DCMAKE_C_FLAGS_RELWITHDEBINFO=$RELFLAGS" \
            "-DOB_ARMSX2=$OB_ARMSX2" "-DOB_ANDROID_R=$OB_ANDROID_R"
} > "$log" 2>&1 || { echo "vucompare_a64: configure failed ($log)"; tail -15 "$log"; exit 1; }
cmake --build "$OB_A64VU_B" -j 16 >> "$log" 2>&1 || { echo "vucompare_a64: build failed ($log)"; grep -E "error:|undefined" "$log" | head -20; exit 1; }
[ -f "$OB_A64VU_B/ob_vucompare_a64" ] || { echo "vucompare_a64: ob_vucompare_a64 not built ($OB_ANDROID_R/ps2xRuntime/libps2_runtime.a missing?)"; exit 1; }
mkdir -p "$OB_A64VU_B/stage" || exit 1
"$LLVM/llvm-strip.exe" --strip-unneeded -o "$OB_A64VU_B/stage/libob_a64vu.so" "$OB_A64VU_B/libob_a64vu.so" || exit 1
"$LLVM/llvm-strip.exe" --strip-unneeded -o "$OB_A64VU_B/stage/ob_vucompare_a64" "$OB_A64VU_B/ob_vucompare_a64" || exit 1
echo "vucompare_a64: built in $(( $(date +%s) - start )) s: $(stat -c %s "$OB_A64VU_B/stage/ob_vucompare_a64") + $(stat -c %s "$OB_A64VU_B/stage/libob_a64vu.so") bytes in $OB_A64VU_B/stage ($log)"
echo "exports of libob_a64vu.so:"
"$LLVM/llvm-nm.exe" -D -C --defined-only "$OB_A64VU_B/stage/libob_a64vu.so" | sed 's/^/  /'
"$PY" -I "$REPO/tools/ob/android_symcheck.py" "$LLVM" \
    "$OB_ANDROID_NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/29" \
    "$OB_A64VU_B/stage/ob_vucompare_a64" "$OB_A64VU_B/stage/libob_a64vu.so"
