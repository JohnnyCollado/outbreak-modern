# The mbedTLS source must be the pinned release, unmodified (docs/ops/pins.md; security queues H1, 2-H8): a git checkout
# at the pinned commit with no changed files. Usable from CMakeLists (include) or alone (cmake -P, the
# test in tools/net/check-pins.sh). In: OB_MBEDTLS, OB_MBEDTLS_COMMIT. Stops with an error when it does not match.
set(_ob_version_h "${OB_MBEDTLS}/include/mbedtls/version.h")
if(NOT EXISTS "${_ob_version_h}")
    message(FATAL_ERROR "mbedTLS not found at ${OB_MBEDTLS}")
endif()
find_package(Git QUIET)
if(NOT GIT_FOUND OR NOT EXISTS "${OB_MBEDTLS}/.git")
    message(FATAL_ERROR "mbedTLS at ${OB_MBEDTLS} is not a git checkout, so its commit can't be checked")
endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${OB_MBEDTLS}" rev-parse HEAD
        OUTPUT_VARIABLE _ob_head OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _ob_rc)
    if(NOT _ob_rc EQUAL 0 OR NOT _ob_head STREQUAL OB_MBEDTLS_COMMIT)
        message(FATAL_ERROR "mbedTLS at ${OB_MBEDTLS} is commit '${_ob_head}', not the pinned ${OB_MBEDTLS_COMMIT}")
    endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${OB_MBEDTLS}" status --porcelain --untracked-files=no
    OUTPUT_VARIABLE _ob_changed OUTPUT_STRIP_TRAILING_WHITESPACE)
if(_ob_changed)
    message(FATAL_ERROR "mbedTLS at ${OB_MBEDTLS} has changed files: ${_ob_changed}")
endif()
