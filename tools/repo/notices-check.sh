#!/usr/bin/env bash
# Distribution notices are present (security queue 2, licensing): the project's licence, the third-party list naming
# every bundled library, and, given an APK, the licence texts inside it. Usage: notices-check.sh [<apk>]. Exit 0 = all.
set -u
cd "$(dirname "$0")/../.." || exit 2
fail=0
if head -1 COPYING 2>/dev/null | grep -q "GNU GENERAL PUBLIC LICENSE"; then echo "ok: COPYING is the GPL"; else echo "FAIL: no COPYING"; fail=1; fi
for name in PCSX2 PS2Recomp ARMSX2 mbedTLS libadrenotools raylib libzip "Dear ImGui" zstd shaderc sse2neon vixl zlib libpng AndroidX Kotlin; do
    if grep -q "$name" THIRD_PARTY_NOTICES.md 2>/dev/null; then echo "ok: notices name $name"; else echo "FAIL: notices miss $name"; fail=1; fi
done
if [ -n "${1:-}" ]; then
    listing=$(python -I -c "import zipfile,sys; print('\n'.join(zipfile.ZipFile(sys.argv[1]).namelist()))" "$1") || { echo "FAIL: cannot read $1"; exit 2; }
    for f in THIRD_PARTY_NOTICES.md COPYING LICENSE-mbedtls LICENSE-adrenotools LICENSE-raylib LICENSE-libzip LICENSE-imgui; do
        if echo "$listing" | tr -d '\r' | grep -q "^assets/licenses/$f$"; then echo "ok: APK holds $f"; else echo "FAIL: APK misses $f"; fail=1; fi
    done
fi
exit $fail
