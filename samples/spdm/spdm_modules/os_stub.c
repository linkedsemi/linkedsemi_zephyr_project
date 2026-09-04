/*
 * Zephyr HAL stubs for DMTF libspdm (malloc + RNG).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <mbedtls/gcm.h>

#include <base.h>

/*
 * cryptlib_mbedtls and Zephyr libmbedTLS must agree on mbedtls_gcm_context.
 * Missing MBEDTLS_CONFIG_FILE used to pick upstream mbedtls_config.h here
 * while gcm.c used config-tls-generic.h; mbedtls_gcm_init() then overflowed
 * the stack (mcause=1, mepc=0 after the first secured-session decode).
 */
#if !defined(MBEDTLS_BLOCK_CIPHER_C)
#error "libspdm must be built with Zephyr mbedtls config (MBEDTLS_BLOCK_CIPHER_C)"
#endif

void *allocate_pool(size_t allocation_size)
{
	return k_malloc(allocation_size);
}

void *allocate_zero_pool(size_t allocation_size)
{
	void *buf = k_malloc(allocation_size);

	if (buf != NULL) {
		memset(buf, 0, allocation_size);
	}

	return buf;
}

void free_pool(void *buffer)
{
	k_free(buffer);
}

bool libspdm_get_random_number_64(uint64_t *rand_data)
{
	if (rand_data == NULL) {
		return false;
	}

	sys_rand_get(rand_data, sizeof(*rand_data));
	return true;
}
