#!/usr/bin/env bash
# Tests the source pins (docs/plans/2026-10-29-security-queue.md H1-H2): the mbedTLS check refuses another commit and
# accepts the pinned one; the PCSX2 tree is based on its pinned commit. One line per check; exit 0 when all pass.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd -W)
CHECK="$REPO/native/ob_server/cmake/check_mbedtls.cmake"
MBEDTLS=${OB_MBEDTLS:-C:/ob/mbedtls/v2.28.10}
PIN=2fc8413bfcb51354c8e679141b17b3f1a5942561
fail=0
if cmake -DOB_MBEDTLS="$MBEDTLS" -DOB_MBEDTLS_COMMIT=$PIN -P "$CHECK" > /dev/null 2>&1; then
    echo "ok: mbedTLS at the pinned commit is accepted"
else
    echo "FAIL: mbedTLS at the pinned commit is refused"; fail=1
fi
if cmake -DOB_MBEDTLS="$MBEDTLS" -DOB_MBEDTLS_COMMIT=0000000000000000000000000000000000000000 -P "$CHECK" > /dev/null 2>&1; then
    echo "FAIL: mbedTLS at another commit is accepted"; fail=1
else
    echo "ok: mbedTLS at another commit is refused"
fi
PCSX2=${OB_PCSX2:-C:/ob/pcsx2}
if git -C "$PCSX2" merge-base --is-ancestor a81f1aa39fc56ef60dbc913637c74c49c1792033 HEAD 2>/dev/null; then
    echo "ok: PCSX2 is based on v2.9.134 (a81f1aa)"
else
    echo "FAIL: PCSX2 is not based on a81f1aa"; fail=1
fi
exit $fail
