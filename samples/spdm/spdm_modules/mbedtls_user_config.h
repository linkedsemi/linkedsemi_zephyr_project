/**
 * Extra mbedtls options required by libspdm cryptlib_mbedtls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBSPDM_MBEDTLS_USER_CONFIG_H
#define LIBSPDM_MBEDTLS_USER_CONFIG_H

/* cryptlib_mbedtls reads mbedtls_ecdh_context.grp / d / Q directly.
 * mbedtls 3 only exposes that layout when restartable ECP is enabled.
 */
#define MBEDTLS_ECP_RESTARTABLE

#endif /* LIBSPDM_MBEDTLS_USER_CONFIG_H */
