/*
 * Copyright 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * libspdm 响应端 secret HAL。
 *
 * libspdm 协议栈本身不持有设备私钥、也不知道本设备有哪些事件/密钥对。
 * 它在需要这些信息时回调本文件里的函数。请求方例程也会链接本文件：
 * 共享的 libspdm 库同时编了 requester_lib 和 responder_lib，链接时必须
 * 提供这些符号，即使请求方运行时几乎用不到签名/事件 HAL。
 *
 * -------------------------------------------------------------------------
 * 证书链怎么工作（DSP0274 slot 0）
 * -------------------------------------------------------------------------
 *
 * 生成：本例程 certs/gen_certs.py
 *   做出一对 ECDSA P-256 密钥：CA 自签 + 设备证（CA 签发）。
 *   再打成 SPDM 证书链 blob，写成本 sample 的 certs/certs_generated.h。
 *
 * g_spdm_cert_chain 布局（小端）：
 *
 *   [0..1]   Length     整段 blob 字节数（含这 4 字节头）
 *   [2..3]   Reserved   0
 *   [4..35]  RootHash   SHA-256(CA 的 DER)   —— GET_DIGESTS 哈希的一部分
 *   [36..]   DER 证书   先 CA，再 device     —— GET_CERTIFICATE 原样切出去
 *
 * 固件侧（公开材料 vs 私钥）：
 *   main.c 把 g_spdm_cert_chain 注册到 LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN。
 *   GET_DIGESTS     = SHA-256(整段 chain blob)
 *   GET_CERTIFICATE = 按 offset/length 拷贝 chain blob
 *   本文件只提供 g_spdm_device_key_pem，给 CHALLENGE / KEY_EXCHANGE_RSP 签名。
 *
 * 主机侧：
 *   只拿 CA 的 PEM 验叶子证书。设备私钥不要给 Python 请求方。
 *
 * -------------------------------------------------------------------------
 * SPDM 1.3 两个本文件实现的特性
 * -------------------------------------------------------------------------
 *
 * 1) 事件订阅（EVENT_CAP）
 *    GET_SUPPORTED_EVENT_TYPES / SUBSCRIBE_EVENT_TYPES 只能在会话内发。
 *    规范要求至少支持 DMTF 组，且组里必须有 EventLost。
 *    KEY_EXCHANGE 若带 EVENT_ALL_POLICY，libspdm 会先回调
 *    libspdm_event_subscribe(ALL)。
 *
 * 2) 多密钥绑定（MULTI_KEY + GET/SET_KEY_PAIR_INFO）
 *    本例程只有 1 个密钥对：id=1，绑定证书槽 0，ECDSA P-256。
 *    GET_DIGESTS 在 32 字节摘要后跟 KeyPairID / CertInfo / KeyUsage。
 *    GET_KEY_PAIR_INFO 读下面的静态变量；SET_KEY_PAIR_INFO 可改绑定。
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "hal/library/eventlib.h"
#include "hal/library/responder/asymsignlib.h"
#include "hal/library/responder/key_pair_info.h"
#include "industry_standard/spdm.h"
#include "library/spdm_crypt_lib.h"
#include "spdm_crypt_ext_lib/spdm_crypt_ext_lib.h"

#include "certs_generated.h"

LOG_MODULE_REGISTER(spdm_secret, LOG_LEVEL_INF);

/**
 * @brief 填写 CHALLENGE_AUTH 里的 OpaqueData。
 *
 * libspdm 组 CHALLENGE_AUTH 时会回调这里。本例程没有厂商私有字段，
 * 把长度写成 0；报文里仍会带 2 字节 OpaqueLength=0。
 *
 * @param spdm_version                   协商后的 SPDM 版本，本例程未使用。
 * @param slot_id                        本次挑战使用的证书槽，本例程未使用。
 * @param measurement_summary_hash       测量摘要（未开 MEAS 时为 NULL），未使用。
 * @param measurement_summary_hash_size  上面缓冲区长度，未使用。
 * @param opaque_data                    出参：要写入应答的 OpaqueData。长度为 0 时可不写。
 * @param opaque_data_size               入参是缓冲区容量；出参是实际写入字节数。
 *
 * @retval true   已把 *opaque_data_size 设为 0。
 * @retval false  opaque_data_size 为空指针。
 */
bool libspdm_challenge_opaque_data(spdm_version_number_t spdm_version, uint8_t slot_id,
				   uint8_t *measurement_summary_hash,
				   size_t measurement_summary_hash_size, void *opaque_data,
				   size_t *opaque_data_size)
{
	ARG_UNUSED(spdm_version);
	ARG_UNUSED(slot_id);
	ARG_UNUSED(measurement_summary_hash);
	ARG_UNUSED(measurement_summary_hash_size);
	/* 长度为 0，不往 opaque_data 里写内容。 */
	ARG_UNUSED(opaque_data);

	if (opaque_data_size == NULL) {
		return false;
	}

	*opaque_data_size = 0;
	return true;
}

/**
 * @brief 封装（ENCAP）互认证路径上的 CHALLENGE OpaqueData。
 *
 * 本例程未开 MUT_AUTH / ENCAP_CAP，运行时不会走到这里。
 * 为满足链接符号，直接复用上面的空实现。
 *
 * @param spdm_version                   同 libspdm_challenge_opaque_data()。
 * @param slot_id                        同 libspdm_challenge_opaque_data()。
 * @param measurement_summary_hash       同 libspdm_challenge_opaque_data()。
 * @param measurement_summary_hash_size  同 libspdm_challenge_opaque_data()。
 * @param opaque_data                    同 libspdm_challenge_opaque_data()。
 * @param opaque_data_size               同 libspdm_challenge_opaque_data()。
 *
 * @retval true   已把长度写成 0。
 * @retval false  参数非法。
 */
bool libspdm_encap_challenge_opaque_data(spdm_version_number_t spdm_version, uint8_t slot_id,
					 uint8_t *measurement_summary_hash,
					 size_t measurement_summary_hash_size, void *opaque_data,
					 size_t *opaque_data_size)
{
	return libspdm_challenge_opaque_data(spdm_version, slot_id, measurement_summary_hash,
					     measurement_summary_hash_size, opaque_data,
					     opaque_data_size);
}

/**
 * @brief 响应端非对称签名：用设备私钥签 transcript。
 *
 * libspdm 在下列应答里需要设备签名时调用本函数：
 *   CHALLENGE_AUTH     op_code = SPDM_CHALLENGE_AUTH
 *   KEY_EXCHANGE_RSP   op_code = SPDM_KEY_EXCHANGE_RSP
 *
 * 本例程每次从 PEM 解析私钥再签。量产应改成从 OTP / 安全元件取钥，
 * 保持本函数签名即可。
 *
 * @param spdm_version    协商版本，传给 cryptlib 选 signing context
 *                        （1.3 前缀是 "dmtf-spdm-v1.3.*"）。
 * @param op_code         当前 SPDM 操作码，决定 signing context 后缀。
 * @param base_asym_algo  协商出的非对称算法（本例程：ECDSA P-256）。
 * @param base_hash_algo  协商出的哈希算法（本例程：SHA-256）。
 * @param is_data_hash    true  = message 已经是 transcript 哈希，走 sign_hash；
 *                        false = message 是原文，由 cryptlib 自己做哈希。
 *                        SPDM 1.2/1.3 常见路径是 true。
 * @param message         待签数据（哈希或原文）。
 * @param message_size    message 字节数。
 * @param signature       出参：签名。ECDSA P-256 为 64 字节 r||s。
 * @param sig_size        入参是缓冲区容量；出参是实际签名长度。
 *
 * @retval true   签名已写入 signature / *sig_size。
 * @retval false  PEM 解析失败，或底层 sign 失败。
 */
bool libspdm_responder_data_sign(spdm_version_number_t spdm_version, uint8_t op_code,
				 uint32_t base_asym_algo, uint32_t base_hash_algo, bool is_data_hash,
				 const uint8_t *message, size_t message_size, uint8_t *signature,
				 size_t *sig_size)
{
	void *context;
	bool result;

	/* PEM → mbedtls 私钥上下文。第三参 password 为 NULL：本例程私钥未加密。 */
	result = libspdm_asym_get_private_key_from_pem(
		base_asym_algo, (const uint8_t *)g_spdm_device_key_pem,
		sizeof(g_spdm_device_key_pem), NULL, &context);
	if (!result) {
		return false;
	}

	if (is_data_hash) {
		/* SPDM 1.3 默认路径：先 Hash(transcript)，再包 signing context 后签。 */
		result = libspdm_asym_sign_hash(spdm_version, op_code, base_asym_algo,
						base_hash_algo, context, message, message_size,
						signature, sig_size);
	} else {
		/* 少见路径：cryptlib 对原文自己做哈希再签。 */
		result = libspdm_asym_sign(spdm_version, op_code, base_asym_algo, base_hash_algo,
					   context, message, message_size, signature, sig_size);
	}

	/* 无论成败都要释放私钥上下文，避免堆泄漏。 */
	libspdm_asym_free(base_asym_algo, context);
	return result;
}

/*
 * SPDM 1.3 事件组编码（DSP0274 SupportedEventGroupsList 里的一组）。
 *
 * 字段顺序必须与规范一致，所以用 pack(1)：
 *   RegistryID        1  0 = DMTF
 *   VendorIDLen       1  DMTF 组没有 VendorID，填 0
 *   EventTypeCount    2  本组有几种事件
 *   EventGroupVersion 2
 *   Attributes        4  bit0=ALL 表示支持“订阅本组全部事件”
 *   随后每个事件类型：EventTypeID(2) + Reserved(2)
 *
 * 规范：DMTF 组必须包含 EventLost(1)。本例程再加 CertificateChanged(4)。
 */
#pragma pack(1)
struct spdm_event_group_dmtf {
	uint8_t registry_id;
	uint8_t vendor_id_len;
	uint16_t event_type_count;
	uint16_t event_group_ver;
	uint32_t attributes;
	uint16_t type0_id;
	uint16_t type0_rsvd;
	uint16_t type1_id;
	uint16_t type1_rsvd;
};
#pragma pack()

/** 当前会话是否已订阅事件。本例程只记状态，不主动 SEND_EVENT。 */
static bool g_event_subscribed;
/** 订阅所属 session_id，退订时清 0。 */
static uint32_t g_event_session_id;

/**
 * @brief GET_SUPPORTED_EVENT_TYPES 的 HAL：回报本设备支持哪些事件。
 *
 * libspdm 组应答时调用。必须已经建立安全会话，否则协议层会先回
 * SESSION_REQUIRED，不会进到这里。
 *
 * @param spdm_context                     libspdm 上下文，本例程未使用。
 * @param spdm_version                     协商版本，本例程未使用。
 * @param session_id                       当前会话 ID，本例程未使用（只报静态能力）。
 * @param supported_event_groups_list      出参：按规范 pack 的事件组列表。
 * @param supported_event_groups_list_len  入参是缓冲区容量；出参是实际写入长度。
 * @param event_group_count                出参：组数。本例程固定为 1（DMTF 组）。
 *
 * @retval true   已写入 1 个 DMTF 组（EventLost + CertificateChanged）。
 * @retval false  指针为空，或调用方缓冲区小于 sizeof(group)。
 */
bool libspdm_event_get_types(void *spdm_context, spdm_version_number_t spdm_version,
			     uint32_t session_id, void *supported_event_groups_list,
			     uint32_t *supported_event_groups_list_len, uint8_t *event_group_count)
{
	struct spdm_event_group_dmtf group;

	ARG_UNUSED(spdm_context);
	ARG_UNUSED(spdm_version);
	ARG_UNUSED(session_id);

	if ((supported_event_groups_list == NULL) || (supported_event_groups_list_len == NULL) ||
	    (event_group_count == NULL)) {
		return false;
	}

	/* 调用方给的缓冲区必须能放下整组 DMTF 描述。 */
	if (*supported_event_groups_list_len < sizeof(group)) {
		return false;
	}

	memset(&group, 0, sizeof(group));
	group.registry_id = SPDM_REGISTRY_ID_DMTF; /* 0：DMTF 标准事件，不是厂商私有。 */
	group.vendor_id_len = 0;                   /* DMTF 组没有 VendorID 字段。 */
	group.event_type_count = 2;                /* 本组两种事件：Lost + CertChanged。 */
	group.event_group_ver = 1;
	group.attributes = 0;                      /* bit0 未置：本例程不声明“支持 ALL”。 */
	group.type0_id = SPDM_DMTF_EVENT_TYPE_EVENT_LOST;           /* 规范强制要有。 */
	group.type0_rsvd = 0;
	group.type1_id = SPDM_DMTF_EVENT_TYPE_CERTIFICATE_CHANGED;  /* 演示用第二种。 */
	group.type1_rsvd = 0;

	memcpy(supported_event_groups_list, &group, sizeof(group));
	*supported_event_groups_list_len = sizeof(group);
	*event_group_count = 1;
	return true;
}

/**
 * @brief SUBSCRIBE_EVENT_TYPES 的 HAL：记录订阅 / 退订。
 *
 * subscribe_type 由 libspdm 根据请求翻译，不是报文里的原始 Param1：
 *   ALL    KEY_EXCHANGE 带 EVENT_ALL_POLICY，或请求订阅全部
 *   NONE   请求 Param1=0，即退订
 *   LIST   请求带具体事件组列表
 *
 * 本例程只记“是否已订阅”和 session_id，不会主动 SEND_EVENT。
 *
 * @param spdm_context                 libspdm 上下文，本例程未使用。
 * @param spdm_version                 协商版本，本例程未使用。
 * @param session_id                   发起订阅的会话；退订时用来清状态。
 * @param subscribe_type               ALL / NONE / LIST，见上。
 * @param subscribe_event_group_count  LIST 时的组数；ALL/NONE 时应为 0。
 * @param subscribe_list_len           LIST 时列表字节数；ALL/NONE 必须为 0。
 * @param subscribe_list               LIST 时的组列表；ALL/NONE 必须为 NULL。
 *
 * @retval true   状态已更新。
 * @retval false  类型非法，或 ALL/NONE 却带了 list，或 LIST 却缺 list。
 */
bool libspdm_event_subscribe(void *spdm_context, spdm_version_number_t spdm_version,
			     uint32_t session_id, uint8_t subscribe_type,
			     uint8_t subscribe_event_group_count, uint32_t subscribe_list_len,
			     const void *subscribe_list)
{
	ARG_UNUSED(spdm_context);
	ARG_UNUSED(spdm_version);

	switch (subscribe_type) {
	case LIBSPDM_EVENT_SUBSCRIBE_ALL:
		/* 规范：ALL 时不得再带列表，否则视为 InvalidRequest。 */
		if ((subscribe_list_len != 0) || (subscribe_list != NULL)) {
			return false;
		}
		g_event_subscribed = true;
		g_event_session_id = session_id;
		LOG_INF("event subscribe ALL session=0x%x", session_id);
		return true;
	case LIBSPDM_EVENT_SUBSCRIBE_NONE:
		/* 退订：同样不允许带列表。 */
		if ((subscribe_list_len != 0) || (subscribe_list != NULL)) {
			return false;
		}
		g_event_subscribed = false;
		g_event_session_id = 0;
		LOG_INF("event unsubscribe ALL session=0x%x", session_id);
		return true;
	case LIBSPDM_EVENT_SUBSCRIBE_LIST:
		/* LIST 必须有非空列表。本例程不解析内容，只要非空就接受。 */
		if ((subscribe_event_group_count == 0) || (subscribe_list_len == 0) ||
		    (subscribe_list == NULL)) {
			return false;
		}
		g_event_subscribed = true;
		g_event_session_id = session_id;
		LOG_INF("event subscribe LIST groups=%u len=%u session=0x%x",
			subscribe_event_group_count, subscribe_list_len, session_id);
		return true;
	default:
		return false;
	}
}

/*
 * 多密钥：本例程只有一把 ECDSA P-256 密钥。
 *
 * SPDM 1.3 把“密钥对”和“证书槽”拆开：
 *   key_pair_id   从 1 起编号，0 非法
 *   slot          证书槽 0..7，本例程只用 slot 0
 *   assoc_mask    bit0=1 表示这对密钥绑定了 slot 0
 *
 * GET_DIGESTS（协商了 MULTI_KEY_CONN 时）在 32 字节摘要后附带：
 *   KeyPairID(1) + CertInfo(1) + KeyUsage(2)
 * 这些值来自 main.c 的 LIBSPDM_DATA_LOCAL_KEY_PAIR_ID / CERT_INFO /
 * KEY_USAGE_BIT_MASK，与这里 HAL 返回的绑定应当一致。
 */
#define SPDM_SAMPLE_KEY_PAIR_ID 1
#define SPDM_SAMPLE_KEY_PAIR_COUNT 1

/*
 * AlgorithmIdentifier for id-ecPublicKey + secp256r1。
 * GET_KEY_PAIR_INFO 的 PublicKeyInfo 填这个 DER，主机可据此知道算法。
 * 不是完整 SubjectPublicKeyInfo（不含公钥点），规范允许只给算法 OID。
 */
static const uint8_t g_ecp256_spki_algid[] = {0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE,
					      0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A, 0x86, 0x48,
					      0xCE, 0x3D, 0x03, 0x01, 0x07};

/** 当前允许的用法：KEY_EX + CHALLENGE。SET CHANGE 可改。 */
static uint16_t g_key_usage = SPDM_KEY_USAGE_BIT_MASK_KEY_EX_USE |
			      SPDM_KEY_USAGE_BIT_MASK_CHALLENGE_USE;
/** 当前非对称算法：ECC256。 */
static uint32_t g_key_asym = SPDM_KEY_PAIR_ASYM_ALGO_CAP_ECC256;
/** 当前绑定的证书槽位图：0x01 = slot 0。 */
static uint8_t g_key_slot_mask = 0x01;

/**
 * @brief GET_KEY_PAIR_INFO 的 HAL：读一把密钥对的能力和当前绑定。
 *
 * public_key_info 可为 NULL：SET 路径有时只查能力，不取 PublicKeyInfo。
 * 能力位告诉主机“能改绑定 / 能改用法 / 能改算法”；没有 GEN_KEY / ERASABLE，
 * 所以上位机不应发 GENERATE / ERASE（协议层通常会先拒）。
 *
 * @param spdm_context            libspdm 上下文，本例程未使用。
 * @param key_pair_id             要查询的密钥对，从 1 起；本例程只认 1。
 * @param capabilities            出参：这把钥匙支持哪些 SET 操作。
 * @param key_usage_capabilities  出参：用法能设成哪些位（KEY_EX | CHALLENGE）。
 * @param current_key_usage       出参：当前用法，来自 g_key_usage。
 * @param asym_algo_capabilities  出参：算法能设成哪些（仅 ECC256）。
 * @param current_asym_algo       出参：当前算法，来自 g_key_asym。
 * @param assoc_cert_slot_mask    出参：绑定的证书槽位图，bit0=slot 0。
 * @param public_key_info_len     入参是缓冲区容量；出参是实际写入的 DER 长度。
 *                                可为 NULL。
 * @param public_key_info         出参：AlgorithmIdentifier DER。可为 NULL。
 *
 * @retval true   已填好出参。
 * @retval false  key_pair_id 非法，或 PublicKeyInfo 缓冲区太小。
 */
bool libspdm_read_key_pair_info(void *spdm_context, uint8_t key_pair_id, uint16_t *capabilities,
				uint16_t *key_usage_capabilities, uint16_t *current_key_usage,
				uint32_t *asym_algo_capabilities, uint32_t *current_asym_algo,
				uint8_t *assoc_cert_slot_mask, uint16_t *public_key_info_len,
				uint8_t *public_key_info)
{
	ARG_UNUSED(spdm_context);

	/* 规范：id 从 1 起。本例程只有一把钥匙。 */
	if ((key_pair_id == 0) || (key_pair_id > SPDM_SAMPLE_KEY_PAIR_COUNT)) {
		return false;
	}

	if (public_key_info_len != NULL) {
		if (*public_key_info_len < sizeof(g_ecp256_spki_algid)) {
			return false;
		}
		*public_key_info_len = sizeof(g_ecp256_spki_algid);
	}
	if (public_key_info != NULL) {
		memcpy(public_key_info, g_ecp256_spki_algid, sizeof(g_ecp256_spki_algid));
	}

	/* CERT_ASSOC / KEY_USAGE / ASYM_ALGO：允许 SET CHANGE 改这三项。 */
	*capabilities = SPDM_KEY_PAIR_CAP_CERT_ASSOC_CAP | SPDM_KEY_PAIR_CAP_KEY_USAGE_CAP |
			SPDM_KEY_PAIR_CAP_ASYM_ALGO_CAP;
	*key_usage_capabilities = SPDM_KEY_USAGE_BIT_MASK_KEY_EX_USE |
				  SPDM_KEY_USAGE_BIT_MASK_CHALLENGE_USE;
	*current_key_usage = g_key_usage;
	*asym_algo_capabilities = SPDM_KEY_PAIR_ASYM_ALGO_CAP_ECC256;
	*current_asym_algo = g_key_asym;
	*assoc_cert_slot_mask = g_key_slot_mask;
	return true;
}

/**
 * @brief SET_KEY_PAIR_INFO 的 HAL：改绑定 / 擦除 / 生成（演示）。
 *
 * operation:
 *   CHANGE    改 usage / 算法 / 槽位绑定。desired_key_usage、desired_asym_algo
 *             为 0 表示该项不改；槽位掩码总是按入参写入。
 *   ERASE     清空静态状态。本例程能力位未声明 ERASABLE，协议层通常会先拒。
 *   GENERATE  生成新密钥。未声明 GEN_KEY，协议层会拒；这里只是演示记状态，
 *             并不真的换 P-256 密钥。
 *
 * @param spdm_context                 libspdm 上下文，本例程未使用。
 * @param key_pair_id                  要改的密钥对，本例程只认 1。
 * @param operation                    CHANGE / ERASE / GENERATE。
 * @param desired_key_usage            CHANGE 时想要的用法；0 表示不改。
 * @param desired_asym_algo            CHANGE 时想要的算法；0 表示不改。
 * @param desired_assoc_cert_slot_mask CHANGE 时想绑定的槽位图。
 * @param need_reset                   出参：true 表示改完要复位才生效。
 *                                     本例程固定写 false，立刻生效。
 *
 * @retval true   状态已更新，*need_reset = false。
 * @retval false  id 非法、need_reset 为空，或 operation 无法识别。
 */
bool libspdm_write_key_pair_info(void *spdm_context, uint8_t key_pair_id, uint8_t operation,
				 uint16_t desired_key_usage, uint32_t desired_asym_algo,
				 uint8_t desired_assoc_cert_slot_mask, bool *need_reset)
{
	ARG_UNUSED(spdm_context);

	if ((key_pair_id == 0) || (key_pair_id > SPDM_SAMPLE_KEY_PAIR_COUNT) ||
	    (need_reset == NULL)) {
		return false;
	}

	if (operation == SPDM_SET_KEY_PAIR_INFO_ERASE_OPERATION) {
		g_key_usage = 0;
		g_key_asym = 0;
		g_key_slot_mask = 0;
		LOG_INF("key pair %u erased", key_pair_id);
	} else if (operation == SPDM_SET_KEY_PAIR_INFO_CHANGE_OPERATION) {
		/* 0 表示“这一项保持原值”，与 DSP0274 SET_KEY_PAIR_INFO 约定一致。 */
		if (desired_key_usage != 0) {
			g_key_usage = desired_key_usage;
		}
		if (desired_asym_algo != 0) {
			g_key_asym = desired_asym_algo;
		}
		g_key_slot_mask = desired_assoc_cert_slot_mask;
		LOG_INF("key pair %u bound slot_mask=0x%02x usage=0x%04x", key_pair_id,
			g_key_slot_mask, g_key_usage);
	} else if (operation == SPDM_SET_KEY_PAIR_INFO_GENERATE_OPERATION) {
		/* 演示：只更新元数据，私钥仍是 certs_generated.h 里那把 P-256。 */
		g_key_usage = desired_key_usage;
		g_key_asym = desired_asym_algo;
		g_key_slot_mask = desired_assoc_cert_slot_mask;
		LOG_INF("key pair %u generate (demo, reuse existing P-256)", key_pair_id);
	} else {
		return false;
	}

	*need_reset = false;
	return true;
}
