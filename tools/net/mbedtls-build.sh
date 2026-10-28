#!/usr/bin/env bash
# S0.2 spike: MSVC build of mbedTLS 2.28.10 test programs.
MSYS_NO_PATHCONV=1 cmd /c "$(cygpath -w "$(dirname "$0")/mbedtls-build.bat")"
echo "mbedtls build rc=$?"
tail -2 C:/ob/logs/mbedtls-build.log
