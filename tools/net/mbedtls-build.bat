@echo off
rem S0.2 spike: build mbedTLS 2.28.10's ssl_server2 / ssl_client2 with MSVC (C:\ob\mbedtls\build-2.28).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cmake -S C:\ob\mbedtls\v2.28.10 -B C:\ob\mbedtls\build-2.28 -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=ON -DMBEDTLS_FATAL_WARNINGS=OFF > C:\ob\logs\mbedtls-configure.log 2>&1 || exit /b 2
cmake --build C:\ob\mbedtls\build-2.28 --target ssl_server2 ssl_client2 > C:\ob\logs\mbedtls-build.log 2>&1 || exit /b 3
exit /b 0
