#!/usr/bin/env bash
# S0.2 spike: build mbedTLS 2.28.10's libraries + ssl_server2 for Android arm64 with the project's NDK settings.
SDK=$(cygpath -m "$LOCALAPPDATA")/Android/Sdk
NDK=$SDK/ndk/27.0.12077973
B=C:/ob/mbedtls/build-2.28-android
cmake -S C:/ob/mbedtls/v2.28.10 -B "$B" -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF \
    -DENABLE_PROGRAMS=ON -DMBEDTLS_FATAL_WARNINGS=OFF -DCMAKE_POLICY_DEFAULT_CMP0057=NEW >C:/ob/logs/mbedtls-android-configure.log 2>&1 || { echo "configure failed"; exit 2; }
cmake --build "$B" --target mbedtls ssl_server2 > C:/ob/logs/mbedtls-android-build.log 2>&1
echo "android build rc=$?"
tail -2 C:/ob/logs/mbedtls-android-build.log
file "$B/programs/ssl/ssl_server2" 2>/dev/null || ls -la "$B/programs/ssl/ssl_server2"
