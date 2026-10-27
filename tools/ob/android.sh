#!/bin/bash
# Android arm64 build and APK, Stage A1 (docs/ops/android.md). Called as `bash tools/ob.sh android [step]`.
#   shaderc    libshaderc_shared.so for arm64 from the pinned sources tools/ob/pcsx2-deps.bat unpacked and patched
#              ($OB_ROOT/pcsx2-deps-build/shaderc-2026.2; the four archives' SHA-256 are re-checked here)
#   configure  NDK toolchain tree $OB_ANDROID_R (arm64-v8a, API 29, Release, c++_static), external GS = native/pcsx2_gs,
#              external VU1 = native/pcsx2_vu (ARMSX2's ARM64 microVU as libob_a64vu.so, sources $OB_ARMSX2 at its pin);
#              clones libadrenotools (custom GPU driver loader, BSD-2-Clause) into $OB_ADRENOTOOLS at its pin if missing
#   build      libps2EntryRunner.so (the first build compiles every game unit once; later builds are incremental)
#   package    APK with Gradle (native/android, wrapper Gradle 9.7.1 + AGP 9.4.0, JDK 17): stages stripped
#              lib/arm64-v8a/*.so (runner, shaderc, libadrenotools' two hooks, libob_a64vu.so: the runner dlopens it at
#              start-up) and assets/resources (PCSX2 shaders) in $APK_DIR/stage, then gradlew assembleDebug compiles the
#              Java bridge (OutbreakActivity), packs and signs with the debug key in $OB_ROOT/android-keys (made by
#              keytool on first use, never in git); build tree $APK_DIR/gradle-build; log ob-android-gradle.log
#   verify     NEEDED libraries and undefined dynamic symbols of the .so files against the NDK's API-29 system libs
#   all        (default) every step above in order; a step that is up to date costs seconds
# Env: OB_ROOT=C:/ob, OB_SRC, OB_ANDROID_R=$OB_ROOT/android-r, OB_ANDROID_NDK=<sdk>/ndk/27.0.12077973,
#      OB_ANDROID_SDK=%LOCALAPPDATA%/Android/Sdk, OB_ANDROID_BT=<sdk>/build-tools/35.0.0, OB_JDK=Android Studio jbr.
# Nothing here touches a device: install/run commands are in docs/ops/android.md.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd -W) || exit 1
: "${OB_ROOT:=C:/ob}" "${OB_SRC:=$OB_ROOT/src}" "${OB_T:=$OB_ROOT/t}" "${OB_LOGS:=$OB_ROOT/logs}"
: "${OB_PCSX2:=$OB_ROOT/pcsx2}" "${OB_PCSX2_DEPS:=$OB_ROOT/pcsx2-deps}"
: "${OB_ANDROID_R:=$OB_ROOT/android-r}"
: "${OB_ADRENOTOOLS:=$OB_ROOT/adrenotools}"
: "${OB_ARMSX2:=$OB_ROOT/armsx2}" # ARMSX2 sparse clone, pinned in native/pcsx2_vu/a64/CMakeLists.txt (docs/ops/pins.md)
# libadrenotools pin (docs/ops/pins.md): master 8fae8ce (2024-09-10), submodule liblinkernsbypass aa39758.
ADRENOTOOLS_URL=https://github.com/bylaws/libadrenotools.git
ADRENOTOOLS_PIN=8fae8ce254dfc1344527e05301e43f37dea2df80
HOOKS="libhook_impl.so libmain_hook.so"
: "${OB_ANDROID_SDK:=$(cygpath -m "$LOCALAPPDATA")/Android/Sdk}"
: "${OB_ANDROID_NDK:=$OB_ANDROID_SDK/ndk/27.0.12077973}"
: "${OB_ANDROID_BT:=$OB_ANDROID_SDK/build-tools/35.0.0}"
: "${OB_JDK:=C:/Program Files/Android/Android Studio/jbr}"
PY="${OB_PY:-C:/Users/johnn/AppData/Local/Python/pythoncore-3.14-64/python.exe}"
PKG=com.outbreakmodern.app
API=29 TARGET_SDK=35
JAR="$OB_ANDROID_SDK/platforms/android-$TARGET_SDK/android.jar"
TC="$OB_ANDROID_NDK/build/cmake/android.toolchain.cmake"
LLVM="$OB_ANDROID_NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
SHADERC_SRC="$OB_ROOT/pcsx2-deps-build/shaderc-2026.2"
SHADERC_B="$OB_ROOT/android-shaderc-build"
APK_DIR="$OB_ROOT/android-apk"
KEYS="$OB_ROOT/android-keys"
JOBS=16
mkdir -p "$OB_LOGS"
NDKCMAKE=(-G Ninja "-DCMAKE_TOOLCHAIN_FILE=$TC" -DANDROID_ABI=arm64-v8a "-DANDROID_PLATFORM=android-$API"
    -DANDROID_STL=c++_static)
# RelWithDebInfo with our flags, as ob.sh recompile on Windows: no IPO (ReleaseMode.cmake turns ThinLTO on for
# Release: a 140 s link per change) and no debug info: the NDK toolchain adds -g to every compile, which made the .so 604 MB before strip and its link
# 237 s after a one-file change (report 2026-10-24 entry 5); -g0 after it wins. As Windows (no /Zi by default).
RELFLAGS="-O3 -DNDEBUG -g0"
# Calls between functions inside libps2EntryRunner.so go direct, not through its PLT (developer's OnePlus 13 profile,
# 2026-10-27: PLT stubs were 4 % of the game thread); what the library exports is unchanged.
SOLDFLAGS="-Wl,-Bsymbolic-functions"

step_shaderc() {
    local log="$OB_LOGS/ob-android-shaderc.log" start rc z sum want
    start=$(date +%s)
    [ -f "$SHADERC_B/libshaderc/libshaderc_shared.so" ] && { echo "shaderc: up to date ($SHADERC_B/libshaderc/libshaderc_shared.so)"; return 0; }
    [ -f "$SHADERC_SRC/CMakeLists.txt.orig" ] || { echo "shaderc: $SHADERC_SRC missing or unpatched: run ob.sh pcsx2-gs (tools/ob/pcsx2-deps.bat) first"; return 1; }
    # Same pins as tools/ob/pcsx2-deps.bat (PCSX2 v2.9.134 build-dependencies.bat).
    while read -r z want; do
        sum=$(sha256sum "$OB_ROOT/pcsx2-deps-build/$z" | cut -d' ' -f1)
        [ "$sum" = "$want" ] || { echo "shaderc: $z SHA-256 $sum, want $want: stop"; return 1; }
    done <<'EOF'
shaderc-2026.2.zip f9401cc5cb36c276cd1e072b6595dbd728148e8dba389e50f7339e2d388dbc08
shaderc-glslang-275822a6261ee689aadb1da5f09a0ec2f058685c.zip 2b63189efad0348d88d410a5e12ec550a612e0b6ceef64624b8f45491269fb9c
shaderc-spirv-headers-58006c901d1d5c37dece6b6610e9af87fa951375.zip d2f071e94c081f5a4606559770ebf1f7d1eac92a1def0c3e10609844aa8b69b2
shaderc-spirv-tools-6337eb62cadd7d124ac6789bf39c0f71148f0a73.zip 4011be89aa73e3461c9deef73936a62c79a3097590c5135d058041cc9fb99c6f
EOF
    {
        cmake -S "$SHADERC_SRC" -B "$SHADERC_B" "${NDKCMAKE[@]}" -DCMAKE_BUILD_TYPE=Release -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON \
            -DSHADERC_SKIP_COPYRIGHT_CHECK=ON -DSHADERC_SKIP_INSTALL=ON -DSPIRV_SKIP_EXECUTABLES=ON -DSPIRV_SKIP_TESTS=ON \
            -DENABLE_GLSLANG_BINARIES=OFF -DGLSLANG_TESTS=OFF -DSPIRV_TOOLS_BUILD_STATIC=ON &&
        cmake --build "$SHADERC_B" --target shaderc_shared -j $JOBS
    } > "$log" 2>&1
    rc=$?
    echo "shaderc: exit $rc in $(( $(date +%s) - start )) s ($log)"
    [ $rc = 0 ] || tail -15 "$log"
    return $rc
}

step_adrenotools() {
    local have
    have=$(git -C "$OB_ADRENOTOOLS" rev-parse HEAD 2>/dev/null)
    [ "$have" = "$ADRENOTOOLS_PIN" ] && [ -f "$OB_ADRENOTOOLS/lib/linkernsbypass/CMakeLists.txt" ] && return 0
    { [ -d "$OB_ADRENOTOOLS/.git" ] || git clone -q "$ADRENOTOOLS_URL" "$OB_ADRENOTOOLS"; } &&
        git -C "$OB_ADRENOTOOLS" -c advice.detachedHead=false checkout -q "$ADRENOTOOLS_PIN" &&
        git -C "$OB_ADRENOTOOLS" submodule update -q --init &&
        echo "adrenotools: $OB_ADRENOTOOLS at $(git -C "$OB_ADRENOTOOLS" rev-parse --short HEAD)" ||
        { echo "adrenotools: clone/checkout of $ADRENOTOOLS_PIN failed"; return 1; }
}

step_configure() {
    local log="$OB_LOGS/ob-android-configure.log" egs="$REPO/native/pcsx2_gs" evu="$REPO/native/pcsx2_vu" rc
    step_adrenotools || return 1
    if [ -f "$OB_ANDROID_R/build.ninja" ] && grep -qxF "PS2X_EXTERNAL_GS_DIR:PATH=$egs" "$OB_ANDROID_R/CMakeCache.txt" &&
        grep -qxF "CMAKE_CXX_FLAGS_RELWITHDEBINFO:STRING=$RELFLAGS" "$OB_ANDROID_R/CMakeCache.txt" &&
        grep -qxF "OB_ADRENOTOOLS:PATH=$OB_ADRENOTOOLS" "$OB_ANDROID_R/CMakeCache.txt" &&
        grep -qxF "PS2X_EXTERNAL_VU1_DIR:PATH=$evu" "$OB_ANDROID_R/CMakeCache.txt" &&
        grep -qxF "CMAKE_SHARED_LINKER_FLAGS:STRING=$SOLDFLAGS" "$OB_ANDROID_R/CMakeCache.txt"; then
        echo "configure: $OB_ANDROID_R already configured"; return 0
    fi
    # raylib 5.5: the source the Windows tools tree already fetched (no second download); the Android build of it
    # lives in this tree. sse2neon (PS2Recomp CMakeLists.txt:47-54, GitHub DLTcollab/sse2neon tag v1.9.1) is fetched
    # once into $OB_ROOT/android-fetch, then reused. PS2X_EXTERNAL_VU1_DIR: native/pcsx2_vu's Android branch builds
    # libob_a64vu.so (ARMSX2's ARM64 microVU, a dependency of ps2EntryRunner, not linked: dlopened at start-up).
    cmake -S "$OB_SRC" -B "$OB_ANDROID_R" "${NDKCMAKE[@]}" -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=$RELFLAGS" \
        "-DCMAKE_C_FLAGS_RELWITHDEBINFO=$RELFLAGS" "-DCMAKE_SHARED_LINKER_FLAGS=$SOLDFLAGS" -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF \
        -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_RECOMP=OFF -DPS2X_ENABLE_FFMPEG=OFF -DPS2X_ENABLE_DEBUG_UI=OFF \
        -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF "-DFETCHCONTENT_BASE_DIR=$OB_ROOT/android-fetch" \
        "-DFETCHCONTENT_SOURCE_DIR_RAYLIB=$OB_T/_deps/raylib-src" \
        "-DPS2X_EXTERNAL_GS_DIR=$egs" "-DOB_PCSX2=$OB_PCSX2" "-DOB_PCSX2_DEPS=$OB_PCSX2_DEPS" "-DOB_ADRENOTOOLS=$OB_ADRENOTOOLS"         "-DPS2X_EXTERNAL_VU1_DIR=$evu" "-DOB_ARMSX2=$OB_ARMSX2" > "$log" 2>&1
    rc=$?
    echo "configure: exit $rc ($log)"
    [ $rc = 0 ] || tail -20 "$log"
    return $rc
}

step_build() {
    local log="$OB_LOGS/ob-android-build.log" start rc
    start=$(date +%s)
    cmake --build "$OB_ANDROID_R" --target ps2EntryRunner -j $JOBS -- -k 0 > "$log" 2>&1
    rc=$?
    echo "build: exit $rc in $(( $(date +%s) - start )) s, $(grep -cE '^\[[0-9]+/[0-9]+\]' "$log") steps ($(grep -E '^\[[0-9]+/[0-9]+\]' "$log" | grep -cE 'Unity[/\\]') game units) ($log)"
    [ $rc = 0 ] || grep -E "error:|FAILED:" "$log" | head -30
    return $rc
}

# OutbreakActivity (native/android/java): javac --release 11 with android.jar on the class path (the JDK refuses
# -bootclasspath with -target 9+; Java 11 class files, which d8 reads whatever the JDK), then d8 -> classes.dex, which
# the package step adds at the APK root. Skipped while classes.dex is newer than every .java file.
JAVA_SRC="$REPO/native/android/java"
DEX_DIR="$APK_DIR/dex"
step_dex() {
    # Gradle builds the Java side now (native/android/build.gradle.kts), inside the package step.
    echo "dex: built by Gradle in the package step"
}

step_package() {
    local log="$OB_LOGS/ob-android-gradle.log" st="$APK_DIR/stage" so="$OB_ANDROID_R/ps2xRuntime/libps2EntryRunner.so" v
    [ -f "$so" ] || { echo "package: $so missing (build first)"; return 1; }
    [ -f "$SHADERC_B/libshaderc/libshaderc_shared.so" ] || { echo "package: libshaderc_shared.so missing (shaderc first)"; return 1; }
    rm -rf "$st" && mkdir -p "$st/lib/arm64-v8a" "$st/assets/resources" "$KEYS" || return 1
    "$LLVM/llvm-strip.exe" --strip-unneeded -o "$st/lib/arm64-v8a/libps2EntryRunner.so" "$so" || return 1
    "$LLVM/llvm-strip.exe" --strip-unneeded -o "$st/lib/arm64-v8a/libshaderc_shared.so" "$SHADERC_B/libshaderc/libshaderc_shared.so" || return 1
    # libadrenotools' hooks: adrenotools_open_libvulkan loads them from nativeLibraryDir (gpu_driver_android.cpp).
    for v in $HOOKS; do
        [ -f "$OB_ANDROID_R/adrenotools/src/hook/$v" ] || { echo "package: $OB_ANDROID_R/adrenotools/src/hook/$v missing (build first)"; return 1; }
        "$LLVM/llvm-strip.exe" --strip-unneeded -o "$st/lib/arm64-v8a/$v" "$OB_ANDROID_R/adrenotools/src/hook/$v" || return 1
    done
    # ARMSX2's ARM64 microVU: not NEEDED by the runner; adapter/hook.cpp dlopens it (a load failure = interp + reason).
    v="$OB_ANDROID_R/ps2xRuntime/external_vu1/a64/libob_a64vu.so"
    [ -f "$v" ] || { echo "package: $v missing (build first)"; return 1; }
    "$LLVM/llvm-strip.exe" --strip-unneeded -o "$st/lib/arm64-v8a/libob_a64vu.so" "$v" || return 1
    cp -r "$OB_PCSX2/bin/resources/shaders" "$st/assets/resources/" || return 1
    (cd "$st/assets/resources" && find . -type f ! -name files.txt | sed 's|^\./||' | sort > files.txt)
    # Gradle (native/android: AGP 9.4.0, wrapper Gradle 9.7.1, JDK 17) compiles the Java bridge, packs the staged
    # libraries (lib/) and assets, and signs with the debug key; the build tree stays outside the repo.
    local gb="$APK_DIR/gradle-build" jdk="${OB_GRADLE_JDK:-C:/Program Files/Microsoft/jdk-17.0.19.10-hotspot}"
    {
        [ -f "$KEYS/debug.keystore" ] || "$OB_JDK/bin/keytool.exe" -genkeypair -keystore "$(cygpath -w "$KEYS/debug.keystore")" \
            -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
            -dname "CN=Android Debug,O=Android,C=US"
        (cd "$REPO/native/android" && JAVA_HOME="$jdk" ANDROID_HOME="$OB_ANDROID_SDK" ./gradlew --console=plain \
            "-PobStage=$(cygpath -m "$st")" "-PobBuildDir=$(cygpath -m "$gb")" "-PobKeystore=$(cygpath -m "$KEYS/debug.keystore")" \
            testDebugUnitTest assembleDebug) &&
        cp "$gb/outputs/apk/debug/outbreak-modern-debug.apk" "$APK_DIR/outbreak-modern.apk" &&
        PATH="$OB_JDK/bin:$PATH" cmd //c "$(cygpath -w "$OB_ANDROID_BT/apksigner.bat")" verify -v "$(cygpath -w "$APK_DIR/outbreak-modern.apk")" &&
        "$OB_ANDROID_BT/aapt2.exe" dump badging "$(cygpath -w "$APK_DIR/outbreak-modern.apk")"
    } > "$log" 2>&1 || { echo "package: failed ($log)"; tail -15 "$log"; return 1; }
    v=$(grep -E "Verified using v[0-9.]+ scheme .*: true" "$log" | sed 's/Verified using \(v[0-9.]*\) scheme.*/\1/' | paste -sd,)
    echo "package: $APK_DIR/outbreak-modern.apk $(stat -c %s "$APK_DIR/outbreak-modern.apk") bytes, signature verified: ${v:-NONE} ($log)"
    # The Kotlin unit tests (native/android/test, run above before assembleDebug) and the Kotlin compiler's warnings.
    local tests=0 fails=0 f
    for f in "$gb"/test-results/testDebugUnitTest/TEST-*.xml; do
        [ -f "$f" ] || continue
        tests=$((tests + $(sed -n 's/.*<testsuite [^>]*tests="\([0-9]*\)".*/\1/p' "$f" | head -1)))
        fails=$((fails + $(sed -n 's/.*<testsuite [^>]*failures="\([0-9]*\)".*/\1/p' "$f" | head -1)))
    done
    echo "kotlin tests: $((tests - fails))/$tests, kotlin warnings: $(grep -c '^w: ' "$log")"
    "$PY" -I -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); [print('  %10d %10d %s' % (i.file_size, i.compress_size, i.filename)) for i in z.infolist() if not i.filename.startswith('assets/resources/shaders/')]; print('  + %d shader files' % sum(1 for i in z.infolist() if i.filename.startswith('assets/resources/shaders/')))" "$APK_DIR/outbreak-modern.apk"
}

step_verify() {
    "$PY" -I "$REPO/tools/ob/android_symcheck.py" "$LLVM" "$OB_ANDROID_NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/$API" \
        "$APK_DIR/stage/lib/arm64-v8a/libps2EntryRunner.so" "$APK_DIR/stage/lib/arm64-v8a/libshaderc_shared.so"         "$APK_DIR/stage/lib/arm64-v8a/libhook_impl.so" "$APK_DIR/stage/lib/arm64-v8a/libmain_hook.so"         "$APK_DIR/stage/lib/arm64-v8a/libob_a64vu.so"
}

case ${1:-all} in
    shaderc) step_shaderc ;;
    configure) step_configure ;;
    build) step_build ;;
    dex) step_dex ;;
    package) step_package ;;
    verify) step_verify ;;
    all) step_shaderc && step_configure && step_build && step_package && step_verify ;;
    *) sed -n '2,15p' "$0"; exit 2 ;;
esac
