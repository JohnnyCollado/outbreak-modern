# The mbedTLS source must be the pinned release (docs/ops/pins.md; security queue H1): the commit when OB_MBEDTLS is
# a git checkout, else the version string in version.h. Usable from CMakeLists (include) or alone (cmake -P, the
# test in tools/net/check-pins.sh). In: OB_MBEDTLS, OB_MBEDTLS_COMMIT. Stops with an error when it does not match.
set(_ob_version_h "${OB_MBEDTLS}/include/mbedtls/version.h")
if(NOT EXISTS "${_ob_version_h}")
    message(FATAL_ERROR "mbedTLS not found at ${OB_MBEDTLS}")
endif()
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${OB_MBEDTLS}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${OB_MBEDTLS}" rev-parse HEAD
        OUTPUT_VARIABLE _ob_head OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _ob_rc)
    if(NOT _ob_rc EQUAL 0 OR NOT _ob_head STREQUAL OB_MBEDTLS_COMMIT)
        message(FATAL_ERROR "mbedTLS at ${OB_MBEDTLS} is commit '${_ob_head}', not the pinned ${OB_MBEDTLS_COMMIT}")
    endif()
else()
    file(STRINGS "${_ob_version_h}" _ob_line REGEX "#define MBEDTLS_VERSION_STRING +\"2\\.28\\.10\"")
    if(NOT _ob_line)
        message(FATAL_ERROR "mbedTLS at ${OB_MBEDTLS} is not 2.28.10 (version.h)")
    endif()
endif()
