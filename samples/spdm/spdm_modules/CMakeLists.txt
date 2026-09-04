# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LIBSPDM)

if(NOT ZEPHYR_LIBSPDM_MODULE_DIR)
  set(ZEPHYR_LIBSPDM_MODULE_DIR ${ZEPHYR_BASE}/../modules/lib/libspdm)
endif()

if(NOT EXISTS ${ZEPHYR_LIBSPDM_MODULE_DIR}/include/library/spdm_common_lib.h)
  message(FATAL_ERROR
    "libspdm not found at ${ZEPHYR_LIBSPDM_MODULE_DIR}. Run: west update libspdm")
endif()

zephyr_library_named(libspdm)

# cryptlib_mbedtls puts mbedtls_gcm_context on the stack. That struct's size
# depends on MBEDTLS_BLOCK_CIPHER_C vs MBEDTLS_CIPHER_C. Zephyr's libmbedTLS
# is built with MBEDTLS_CONFIG_FILE=config-tls-generic.h (BLOCK_CIPHER_C,
# 616-byte context). Without the same -D, this library includes upstream
# mbedtls_config.h (CIPHER_C, smaller context). mbedtls_gcm_init() then
# memsets 616 bytes into a ~388-byte stack object, zeros the saved ra, and
# the CPU fetches from address 0 (Instruction Access fault after FINISH).
zephyr_library_link_libraries(mbedTLS)
if(CONFIG_MBEDTLS_CFG_FILE)
  zephyr_library_compile_definitions(
    MBEDTLS_CONFIG_FILE="${CONFIG_MBEDTLS_CFG_FILE}"
  )
  zephyr_library_include_directories(${ZEPHYR_BASE}/modules/mbedtls/configs)
endif()

zephyr_include_directories(
  ${CMAKE_CURRENT_LIST_DIR}
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/include
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/include/hal
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub/include
)

zephyr_library_include_directories(
  ${CMAKE_CURRENT_LIST_DIR}
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub/cryptlib_mbedtls
)

zephyr_compile_definitions(LIBSPDM_CONFIG="spdm_lib_config.h")

zephyr_library_compile_options(
  -Wno-unused-parameter
  -Wno-unused-variable
  -Wno-unused-but-set-variable
  -Wno-maybe-uninitialized
  -Wno-sign-compare
  -Wno-type-limits
  -Wno-incompatible-pointer-types
)

file(GLOB_RECURSE libspdm_common_srcs
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/library/spdm_common_lib/*.c)
file(GLOB_RECURSE libspdm_crypt_srcs
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/library/spdm_crypt_lib/*.c)
file(GLOB_RECURSE libspdm_req_srcs
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/library/spdm_requester_lib/*.c)
file(GLOB_RECURSE libspdm_rsp_srcs
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/library/spdm_responder_lib/*.c)
file(GLOB_RECURSE libspdm_sec_srcs
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/library/spdm_secured_message_lib/*.c)
file(GLOB_RECURSE libspdm_mctp_srcs
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/library/spdm_transport_mctp_lib/*.c)
file(GLOB_RECURSE libspdm_cryptmbed_srcs
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub/cryptlib_mbedtls/*.c)
file(GLOB_RECURSE libspdm_mem_srcs
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub/memlib/*.c)
file(GLOB_RECURSE libspdm_dbg_srcs
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub/debuglib_null/*.c)

# Host CRT wrappers clash with Zephyr mbedtls (mbedtls_platform_zeroize).
# Unused algorithm files pull headers that are not enabled in config-tls-generic.h.
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "sys_call/")
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "aead_chacha20")
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "aead_sm4")
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "hmac_sm3")
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "hmac_sha3")
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "hkdf_sm3")
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "hkdf_sha3")
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "hash/sha3")
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "hash/sm3")
list(FILTER libspdm_cryptmbed_srcs EXCLUDE REGEX "pk/sm2")

zephyr_library_sources(
  ${libspdm_common_srcs}
  ${libspdm_crypt_srcs}
  ${libspdm_req_srcs}
  ${libspdm_rsp_srcs}
  ${libspdm_sec_srcs}
  ${libspdm_mctp_srcs}
  ${libspdm_cryptmbed_srcs}
  ${libspdm_mem_srcs}
  ${libspdm_dbg_srcs}
  ${CMAKE_CURRENT_LIST_DIR}/os_stub.c
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub/spdm_crypt_ext_lib/spdm_crypt_ext.c
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub/platform_lib_null/time_linux.c
  ${ZEPHYR_LIBSPDM_MODULE_DIR}/os_stub/platform_lib_null/watchdog.c
)

endif()
