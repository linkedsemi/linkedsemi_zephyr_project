/*
 * Copyright 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 *
 * SPDM over MCTP over USB 响应端例程。
 *
 * 协议栈：
 *   USB 设备栈（MCTP class）
 *     -> libmctp（本端 EID = SELF_ID）
 *       -> libspdm responder（MCTP message type 0x05 / 0x06）
 *
 * 主机通过 USB 发来完整 MCTP 消息后，rx_message() 缓存请求并唤醒 main()，
 * 由 libspdm_responder_dispatch_message() 解码、处理并回送响应。
 *
 * 使用 Zephyr mbedtls（cryptlib_mbedtls）：SPDM 1.3 响应端。
 * GET_VERSION / GET_CAPABILITIES / NEGOTIATE_ALGORITHMS /
 * GET_DIGESTS（含多密钥 KeyPairID 绑定）/ GET_CERTIFICATE / CHALLENGE /
 * GET_KEY_PAIR_INFO / SET_KEY_PAIR_INFO /
 * KEY_EXCHANGE / FINISH。会话内 GET_SUPPORTED_EVENT_TYPES /
 * SUBSCRIBE_EVENT_TYPES、VENDOR_DEFINED（1111/2222）和 END_SESSION
 * 走加密通道（MCTP type 0x06）。度量尚未打开。
 */

#include <sample_usbd.h>

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pmci/mctp/mctp_usb.h>
#include <zephyr/usb/usbd.h>
#include <libmctp.h>

#include "industry_standard/spdm.h"
#include "industry_standard/mctp.h"
#include "library/spdm_common_lib.h"
#include "library/spdm_responder_lib.h"
#include "library/spdm_transport_mctp_lib.h"

#include "certs_generated.h"

LOG_MODULE_REGISTER(spdm_mctp, LOG_LEVEL_INF);

/** 本端 MCTP Endpoint ID。 */
#define SELF_ID      10
/** 对端（总线 owner / 主机）MCTP Endpoint ID。 */
#define BUS_OWNER_ID 20

/** libspdm 发送缓存大小，需覆盖 MCTP 头尾 + SPDM 报文。 */
#define SPDM_SENDER_BUFFER_SIZE   2048
/** libspdm 接收缓存大小，需覆盖 MCTP 头尾 + SPDM 报文。 */
#define SPDM_RECEIVER_BUFFER_SIZE 2048
/** 单条 SPDM 消息最大长度（不含传输层封装）。 */
#define SPDM_MAX_MSG_SIZE         2048

/**
 * USB 收到一帧 SPDM 后，rx_message() give，main() 的 dispatch 循环 take。
 * 初值 0：上电时还没有待处理请求。
 */
K_SEM_DEFINE(spdm_rx_sem, 0, 1);
/**
 * USB-MCTP binding 实例。子类 = Managed Device Endpoint，协议 = MCTP 1.x。
 * 真正的 pkt_size 在 main() 里改成 251（USB 单包含 4 字节头不能超过 255）。
 */
MCTP_USB_DEFINE(mctp0, USBD_MCTP_SUBCLASS_MANAGED_DEVICE_ENDPOINT, USBD_MCTP_PROTOCOL_1_X);

static struct mctp *mctp_ctx;   /**< libmctp 实例，USB 枚举完成后再创建。 */
static bool usb_configured;     /**< 已收到 USB SET_CONFIGURATION。 */

static uint8_t *spdm_context;   /**< libspdm 上下文（含 secured message 区）。 */
static uint8_t *spdm_scratch;   /**< libspdm 编解码用 scratch buffer。 */

static uint8_t sender_buf[SPDM_SENDER_BUFFER_SIZE];
static uint8_t receiver_buf[SPDM_RECEIVER_BUFFER_SIZE];
static bool sender_in_use;      /**< true = libspdm 正在 encode，未 release。 */
static bool receiver_in_use;    /**< true = libspdm 正在 decode，未 release。 */

/** 最近一帧待处理的 MCTP 载荷（含 1 字节 message type）。 */
static uint8_t pending_rx[SPDM_RECEIVER_BUFFER_SIZE];
static size_t pending_rx_len;
static uint8_t pending_eid;      /**< 请求来源 EID，回包目的地址。 */
static uint8_t pending_msg_tag;  /**< 请求 message tag，回包原样带回。 */
static bool pending_valid;

/**
 * @brief USB 设备栈消息回调：感知枚举进度。
 *
 * 必须等到 SET_CONFIGURATION 之后才初始化 MCTP / SPDM。
 * 更早发数据时主机还没认到接口，包会丢。
 *
 * @param ctx  USB 设备上下文。本例程只用消息类型，此参数未使用。
 * @param msg  USB 栈事件。关心 type == USBD_MSG_CONFIGURATION。
 *
 * @return 无。副作用：置位 usb_configured。
 */
static void sample_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	ARG_UNUSED(ctx);

	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (msg->type == USBD_MSG_CONFIGURATION) {
		usb_configured = true;
	}
}

/**
 * @brief 初始化并启用 USB device next 栈（含 MCTP USB class）。
 *
 * sample_usbd_init_device() 会建描述符（VID 2FE3 PID 0001）并注册 MCTP 接口。
 * 有的控制器能检测 VBUS，插线后栈自己 enable；不能检测时要主动 usbd_enable()。
 *
 * @retval 0        成功，随后等 sample_msg_cb 报 CONFIGURATION。
 * @retval -ENODEV  sample_usbd_init_device() 失败。
 * @retval <0       usbd_enable() 返回的错误码。
 */
static int enable_usb_device_next(void)
{
	static struct usbd_context *mctp_poc_usbd;
	int err;

	mctp_poc_usbd = sample_usbd_init_device(sample_msg_cb);
	if (mctp_poc_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	/* 不能检测 VBUS 的控制器不会自动上电，这里手动打开设备栈。 */
	if (!usbd_can_detect_vbus(mctp_poc_usbd)) {
		err = usbd_enable(mctp_poc_usbd);
		if (err) {
			LOG_ERR("Failed to enable device support");
			return err;
		}
	}

	LOG_INF("USB device support enabled");
	return 0;
}

/**
 * @brief libspdm 发送回调：把已封装好的传输层报文经 MCTP 发给对端。
 *
 * dispatch 处理完请求后会调用本函数。message 里已经带了 1 字节
 * MCTP type（0x05 明文 / 0x06 密文），不要再加一层。
 *
 * 响应端回包必须：
 *   - 目的 EID = 请求来源 pending_eid
 *   - message tag = 请求带来的 pending_msg_tag
 *   - tag_owner = false（tag 仍由请求方持有）
 *
 * @param context       libspdm 上下文。本例程用全局 mctp_ctx，此参数未使用。
 * @param message_size  待发送字节数，含 MCTP message type。
 * @param message       sender_buf 里 encode 好的报文。
 * @param timeout       超时（微秒）。响应端 dispatch 时为 0，本例程忽略。
 *
 * @retval LIBSPDM_STATUS_SUCCESS    已交给 mctp_message_tx()。
 * @retval LIBSPDM_STATUS_SEND_FAIL  参数非法，或 MCTP 发送失败。
 */
static libspdm_return_t spdm_device_send_message(void *context, size_t message_size,
						 const void *message, uint64_t timeout)
{
	int rc;

	ARG_UNUSED(context);
	ARG_UNUSED(timeout);

	if ((mctp_ctx == NULL) || (message == NULL) || (message_size == 0)) {
		return LIBSPDM_STATUS_SEND_FAIL;
	}

	LOG_INF("SPDM TX %u bytes to EID %u tag %u", (unsigned int)message_size, pending_eid,
		pending_msg_tag);

	/* tag_owner=false：这是对请求的应答，不能自己另起一个 tag。 */
	rc = mctp_message_tx(mctp_ctx, pending_eid, false, pending_msg_tag, (void *)message,
			     message_size);
	if (rc != 0) {
		LOG_ERR("mctp_message_tx failed: %d", rc);
		return LIBSPDM_STATUS_SEND_FAIL;
	}

	return LIBSPDM_STATUS_SUCCESS;
}

/**
 * @brief libspdm 接收回调：把 rx_message() 缓存的一帧拷入接收 buffer。
 *
 * 由 libspdm_responder_dispatch_message() 在 acquire receiver buffer 之后调用。
 * 响应端是被动的：main() 已经等信号量确认有包，这里不再阻塞。
 *
 * @param context        libspdm 上下文，本例程未使用。
 * @param message_size   入参为 buffer 容量；成功后改为实际长度。
 * @param message        入参为接收 buffer 指针（*message 指向 receiver_buf）。
 *                       载荷写到 *message 指向的内存。
 * @param timeout        超时（微秒）。响应端 dispatch 时为 0，本例程忽略。
 *
 * @retval LIBSPDM_STATUS_SUCCESS       已填充一帧，并清掉 pending_valid。
 * @retval LIBSPDM_STATUS_RECEIVE_FAIL  无待处理帧、指针为空，或长度超出 buffer。
 */
static libspdm_return_t spdm_device_receive_message(void *context, size_t *message_size,
						    void **message, uint64_t timeout)
{
	uint8_t *buf;

	ARG_UNUSED(context);
	ARG_UNUSED(timeout);

	if ((message_size == NULL) || (message == NULL) || (*message == NULL) || !pending_valid) {
		return LIBSPDM_STATUS_RECEIVE_FAIL;
	}

	buf = *message;
	if (pending_rx_len > *message_size) {
		return LIBSPDM_STATUS_RECEIVE_FAIL;
	}

	memcpy(buf, pending_rx, pending_rx_len);
	*message_size = pending_rx_len;
	/* 消费掉这一帧，避免 dispatch 再次读到同一包。 */
	pending_valid = false;

	return LIBSPDM_STATUS_SUCCESS;
}

/**
 * @brief 申请 libspdm 发送 buffer。
 *
 * encode 时占用 sender_buf，send 完成后必须 release，否则下一轮会 ACQUIRE_FAIL。
 *
 * @param context      libspdm 上下文，本例程未使用。
 * @param msg_buf_ptr  出参：成功时指向 sender_buf。
 *
 * @retval LIBSPDM_STATUS_SUCCESS      已占用发送 buffer。
 * @retval LIBSPDM_STATUS_ACQUIRE_FAIL buffer 已被占用，或 msg_buf_ptr 为空。
 */
static libspdm_return_t spdm_acquire_sender_buffer(void *context, void **msg_buf_ptr)
{
	ARG_UNUSED(context);

	if (sender_in_use || (msg_buf_ptr == NULL)) {
		return LIBSPDM_STATUS_ACQUIRE_FAIL;
	}

	sender_in_use = true;
	*msg_buf_ptr = sender_buf;
	return LIBSPDM_STATUS_SUCCESS;
}

/**
 * @brief 释放 libspdm 发送 buffer，允许下一轮 encode。
 *
 * @param context      libspdm 上下文，本例程未使用。
 * @param msg_buf_ptr  先前 acquire 得到的指针。本例程不校验是否等于 sender_buf。
 *
 * @return 无。
 */
static void spdm_release_sender_buffer(void *context, const void *msg_buf_ptr)
{
	ARG_UNUSED(context);
	ARG_UNUSED(msg_buf_ptr);
	sender_in_use = false;
}

/**
 * @brief 申请 libspdm 接收 buffer。
 *
 * decode 时占用 receiver_buf。
 *
 * @param context      libspdm 上下文，本例程未使用。
 * @param msg_buf_ptr  出参：成功时指向 receiver_buf。
 *
 * @retval LIBSPDM_STATUS_SUCCESS      已占用接收 buffer。
 * @retval LIBSPDM_STATUS_ACQUIRE_FAIL buffer 已被占用，或 msg_buf_ptr 为空。
 */
static libspdm_return_t spdm_acquire_receiver_buffer(void *context, void **msg_buf_ptr)
{
	ARG_UNUSED(context);

	if (receiver_in_use || (msg_buf_ptr == NULL)) {
		return LIBSPDM_STATUS_ACQUIRE_FAIL;
	}

	receiver_in_use = true;
	*msg_buf_ptr = receiver_buf;
	return LIBSPDM_STATUS_SUCCESS;
}

/**
 * @brief 释放 libspdm 接收 buffer。
 *
 * @param context      libspdm 上下文，本例程未使用。
 * @param msg_buf_ptr  先前 acquire 得到的指针，本例程不校验。
 *
 * @return 无。
 */
static void spdm_release_receiver_buffer(void *context, const void *msg_buf_ptr)
{
	ARG_UNUSED(context);
	ARG_UNUSED(msg_buf_ptr);
	receiver_in_use = false;
}

/**
 * @brief 填写 VENDOR_DEFINED_RESPONSE 的 StandardID / VendorID。
 *
 * 与主机约定：StandardID=0、VendorID 长度为 0，载荷自己用 ASCII 定义。
 *
 * @param context             libspdm 上下文，本例程未使用。
 * @param resp_standard_id    出参：Registry / Standard ID，本例程写 0。
 * @param resp_vendor_id_len  出参：VendorID 字节数，本例程写 0。
 * @param resp_vendor_id      VendorID 缓冲区。长度为 0 时不写，本例程未使用。
 *
 * @retval LIBSPDM_STATUS_SUCCESS            已填写。
 * @retval LIBSPDM_STATUS_INVALID_PARAMETER  出参指针为空。
 */
static libspdm_return_t spdm_vendor_get_id(void *context, uint16_t *resp_standard_id,
					   uint8_t *resp_vendor_id_len, void *resp_vendor_id)
{
	ARG_UNUSED(context);
	ARG_UNUSED(resp_vendor_id);

	if ((resp_standard_id == NULL) || (resp_vendor_id_len == NULL)) {
		return LIBSPDM_STATUS_INVALID_PARAMETER;
	}

	*resp_standard_id = 0;
	*resp_vendor_id_len = 0;
	return LIBSPDM_STATUS_SUCCESS;
}

/**
 * @brief 会话内 VENDOR_DEFINED 应用载荷：请求 "1111"，应答 "2222"。
 *
 * 只在 KEY_EX / FINISH 成功后的加密通道（MCTP type 0x06）上出现。
 * 无法识别的载荷不报错，回空响应，避免把会话拆掉。
 *
 * @param context           libspdm 上下文，本例程未使用。
 * @param req_standard_id   请求里的 StandardID，本例程不校验。
 * @param req_vendor_id_len 请求 VendorID 长度，本例程不校验。
 * @param req_vendor_id     请求 VendorID，本例程不校验。
 * @param req_size          请求载荷字节数。
 * @param req_data          请求载荷。期望 4 字节 ASCII "1111"。
 * @param resp_size         入参是应答缓冲区容量；出参是实际写入长度。
 * @param resp_data         出参：应答载荷。匹配时写 "2222"。
 *
 * @retval LIBSPDM_STATUS_SUCCESS            已填写应答（匹配或空）。
 * @retval LIBSPDM_STATUS_INVALID_PARAMETER  指针为空。
 * @retval LIBSPDM_STATUS_BUFFER_TOO_SMALL   应答缓冲区 < 4。
 */
static libspdm_return_t spdm_vendor_response(void *context, uint16_t req_standard_id,
					     uint8_t req_vendor_id_len, const void *req_vendor_id,
					     uint16_t req_size, const void *req_data,
					     uint16_t *resp_size, void *resp_data)
{
	static const uint8_t ping[4] = { '1', '1', '1', '1' };
	static const uint8_t pong[4] = { '2', '2', '2', '2' };

	ARG_UNUSED(context);
	ARG_UNUSED(req_standard_id);
	ARG_UNUSED(req_vendor_id_len);
	ARG_UNUSED(req_vendor_id);

	if ((req_data == NULL) || (resp_size == NULL) || (resp_data == NULL)) {
		return LIBSPDM_STATUS_INVALID_PARAMETER;
	}

	if ((req_size == sizeof(ping)) && (memcmp(req_data, ping, sizeof(ping)) == 0)) {
		if (*resp_size < sizeof(pong)) {
			return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
		}
		memcpy(resp_data, pong, sizeof(pong));
		*resp_size = sizeof(pong);
		LOG_INF("session ping 1111 -> 2222");
		return LIBSPDM_STATUS_SUCCESS;
	}

	/* 未知载荷：回空，让对端自己判断，不要把会话拆掉。 */
	*resp_size = 0;
	LOG_WRN("unknown vendor payload (%u bytes)", req_size);
	return LIBSPDM_STATUS_SUCCESS;
}

/**
 * @brief 分配并配置 libspdm 响应端上下文。
 *
 * 分四步：
 * 1. 分配 context，调用 libspdm_init_context()。
 * 2. 注册三组与传输相关的回调（I/O、MCTP 编解码、收发 buffer）。
 * 3. 分配 scratch，供 encode/decode 临时使用。
 * 4. libspdm_set_data() 写入本端能力：版本、capability、算法、证书、多密钥绑定。
 *    GET_CAPABILITIES / NEGOTIATE_ALGORITHMS 应答就读这些值。
 *
 * 无入参。结果写到全局 spdm_context / spdm_scratch。
 *
 * @retval 0        成功，上下文已就绪。
 * @retval -ENOMEM  上下文或 scratch 分配失败。
 * @retval -EIO     libspdm_init_context() 或 vendor 回调注册失败。
 * @retval -EINVAL  libspdm_check_context() 失败（buffer 尺寸等不自洽）。
 */
static int spdm_responder_init(void)
{
	libspdm_data_parameter_t parameter;
	libspdm_return_t status;
	size_t ctx_size;
	size_t scratch_size;
	uint8_t data8;
	uint16_t data16;
	uint32_t data32;
	spdm_version_number_t spdm_version;

	/* ---------- 1. 分配并初始化 libspdm 上下文 ---------- */
	/* 大小 = sizeof(spdm_context) + 每个 session 的 secured_message 上下文。
	 * 当前 MAX_SESSION_COUNT=1，即使不做会话也会占一块。
	 */
	ctx_size = libspdm_get_context_size();
	LOG_INF("libspdm context size: %u", (unsigned int)ctx_size);

	spdm_context = k_malloc(ctx_size);
	if (spdm_context == NULL) {
		LOG_ERR("Failed to allocate SPDM context");
		return -ENOMEM;
	}

	memset(spdm_context, 0, ctx_size);
	status = libspdm_init_context(spdm_context);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("libspdm_init_context failed: 0x%x", status);
		return -EIO;
	}

	/* ---------- 2. 注册与 MCTP 对接的三组回调 ---------- */
	/* 设备 I/O：dispatch 时 receive 读 pending_rx，send 走 mctp_message_tx。 */
	libspdm_register_device_io_func(spdm_context, spdm_device_send_message,
					spdm_device_receive_message);
	/* 传输层：给 SPDM 报文加减 1 字节 MCTP message type（0x05/0x06）。
	 * 头/尾尺寸给 libspdm 算 DataTransferSize，须小于 sender/receiver buffer。
	 */
	libspdm_register_transport_layer_func(spdm_context, SPDM_MAX_MSG_SIZE,
					      LIBSPDM_MCTP_TRANSPORT_HEADER_SIZE,
					      LIBSPDM_MCTP_TRANSPORT_TAIL_SIZE,
					      libspdm_transport_mctp_encode_message,
					      libspdm_transport_mctp_decode_message);
	/* 收发工作内存：encode 写 sender_buf，decode 读 receiver_buf。 */
	libspdm_register_device_buffer_func(spdm_context, SPDM_SENDER_BUFFER_SIZE,
					    SPDM_RECEIVER_BUFFER_SIZE, spdm_acquire_sender_buffer,
					    spdm_release_sender_buffer,
					    spdm_acquire_receiver_buffer,
					    spdm_release_receiver_buffer);

	/* ---------- 3. scratch：安全消息/分片等临时区，普通 VERSION 也会用到 ---------- */
	scratch_size = libspdm_get_sizeof_required_scratch_buffer(spdm_context);
	spdm_scratch = k_malloc(scratch_size);
	if (spdm_scratch == NULL) {
		LOG_ERR("Failed to allocate SPDM scratch buffer (%u)", (unsigned int)scratch_size);
		return -ENOMEM;
	}
	LOG_INF("SPDM scratch buffer size: %u", (unsigned int)scratch_size);
	memset(spdm_scratch, 0, scratch_size);
	libspdm_set_scratch_buffer(spdm_context, spdm_scratch, scratch_size);

	/* ---------- 4. 本端 SPDM 能力（GET_CAPABILITIES / NEGOTIATE_ALGORITHMS 的答案） ---------- */
	/* LOCATION_LOCAL = 本端声明的能力，不是协商后的 connection 值。 */
	memset(&parameter, 0, sizeof(parameter));
	parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;

	/* 只声明 SPDM 1.3。VERSION 应答里的入口由此生成。
	 * SPDM_VERSION_NUMBER_SHIFT_BIT 把主/次版本挪到规范规定的 bit 位。
	 */
	spdm_version = SPDM_MESSAGE_VERSION_13 << SPDM_VERSION_NUMBER_SHIFT_BIT;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_SPDM_VERSION, &parameter, &spdm_version,
			 sizeof(spdm_version));

	/* CTExponent：计算复杂度超时 2^ct 微秒。0 表示最小等待。 */
	data8 = 0;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT, &parameter, &data8,
			 sizeof(data8));

	/* CERT_CAP | CHAL_CAP：主机可走 GET_DIGESTS / GET_CERTIFICATE / CHALLENGE。
	 * KEY_EX_CAP：允许 KEY_EXCHANGE / FINISH，用 ECDHE 派生会话密钥。
	 * ENCRYPT_CAP | MAC_CAP：会话建立后用 AES-128-GCM 保护后续 SPDM。
	 * HANDSHAKE_IN_THE_CLEAR_CAP：FINISH 本身仍走明文（MCTP type 0x05）。
	 * EVENT_CAP：会话内 GET_SUPPORTED_EVENT_TYPES / SUBSCRIBE_EVENT_TYPES。
	 * MULTI_KEY_CAP_NEG：DIGESTS 可带 KeyPairID 绑定，由 OtherParams 协商。
	 * GET/SET_KEY_PAIR_INFO_CAP：查询/改密钥对与证书槽的绑定。
	 * 故意不声明 ENCAP_CAP：本例程不做封装互认证。
	 */
	data32 = SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP |
		 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHAL_CAP |
		 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_EX_CAP |
		 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ENCRYPT_CAP |
		 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MAC_CAP |
		 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_HANDSHAKE_IN_THE_CLEAR_CAP |
		 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_EVENT_CAP |
		 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MULTI_KEY_CAP_NEG |
		 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_GET_KEY_PAIR_INFO_CAP |
		 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_SET_KEY_PAIR_INFO_CAP;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter, &data32,
			 sizeof(data32));

	/* 哈希 / 非对称：NEGOTIATE_ALGORITHMS 会带回这些位；CHALLENGE 用 ECDSA P-256。 */
	data32 = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_256;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO, &parameter, &data32,
			 sizeof(data32));

	data32 = SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P256;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO, &parameter, &data32,
			 sizeof(data32));

	/* 测量规格/哈希：MEAS_CAP 未开，填 0。启用测量后改为 DMTF spec + SHA-256。 */
	data8 = 0;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_SPEC, &parameter, &data8,
			 sizeof(data8));

	data32 = 0;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO, &parameter, &data32,
			 sizeof(data32));

	/* 会话算法：KEY_EX 用 ECDHE P-256 交换临时公钥，AES-128-GCM 做 AEAD，
	 * key schedule 按 DSP0274 HMAC-Hash 从共享密钥派生 handshake/application key。
	 */
	data16 = SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_256_R1;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_DHE_NAME_GROUP, &parameter, &data16,
			 sizeof(data16));
	data16 = SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_AES_128_GCM;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_AEAD_CIPHER_SUITE, &parameter, &data16,
			 sizeof(data16));
	/* 0 = 不要求请求方出示证书（不做 MUT_AUTH）。 */
	data16 = 0;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG, &parameter, &data16,
			 sizeof(data16));
	data16 = SPDM_ALGORITHMS_KEY_SCHEDULE_HMAC_HASH;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_KEY_SCHEDULE, &parameter, &data16,
			 sizeof(data16));
	/* OpaqueDataFormat1 是 KEY_EX 的硬性要求，缺了会 InvalidRequest。
	 * MULTI_KEY_CONN 表示愿意做 ResponderMultiKeyConn：DIGESTS 带 KeyPairID。
	 */
	data8 = SPDM_ALGORITHMS_OPAQUE_DATA_FORMAT_1 | SPDM_ALGORITHMS_MULTI_KEY_CONN;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_OTHER_PARAMS_SUPPORT, &parameter, &data8,
			 sizeof(data8));

	/* Slot 0：SPDM 证书链 blob（见 spdm_secret.c 文件头）。
	 * additional_data[0] = 槽号。GET_DIGESTS 对整段做 SHA-256；
	 * GET_CERTIFICATE 按偏移拷贝。设备私钥不在这里，签名走 HAL。
	 */
	parameter.additional_data[0] = 0;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN, &parameter,
			 (void *)g_spdm_cert_chain, g_spdm_cert_chain_len);

	/* bit0 = 只提供 slot 0。 */
	data8 = 0x01;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_SUPPORTED_SLOT_MASK, &parameter, &data8,
			 sizeof(data8));

	/* 多密钥绑定（SPDM 1.3）：把“证书槽”和“密钥对”对应起来。
	 * TOTAL_KEY_PAIRS=1：设备只有一把钥匙，id 从 1 起（0 非法）。
	 * additional_data[0] 是槽号：下面三条都是在描述 slot 0。
	 *   KEY_PAIR_ID=1           GET_DIGESTS 附加字段里的 KeyPairID
	 *   CERT_INFO=DeviceCert    证书模型（叶子是设备身份，不是别名证书）
	 *   KEY_USAGE=KEY_EX|CHAL   这把钥匙能做密钥交换和挑战
	 * 这些值必须和 spdm_secret.c 里 GET_KEY_PAIR_INFO HAL 返回的一致。
	 * 请求方 OtherParams 置 MULTI_KEY_CONN 后，DIGESTS 才会带这 4 字节。
	 */
	data8 = 1;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_TOTAL_KEY_PAIRS, &parameter, &data8,
			 sizeof(data8));
	parameter.additional_data[0] = 0;
	data8 = 1;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_KEY_PAIR_ID, &parameter, &data8,
			 sizeof(data8));
	data8 = SPDM_CERTIFICATE_INFO_CERT_MODEL_DEVICE_CERT;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_CERT_INFO, &parameter, &data8,
			 sizeof(data8));
	data16 = SPDM_KEY_USAGE_BIT_MASK_KEY_EX_USE | SPDM_KEY_USAGE_BIT_MASK_CHALLENGE_USE;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_KEY_USAGE_BIT_MASK, &parameter, &data16,
			 sizeof(data16));

	/* 会话内 VENDOR_DEFINED：主机发 1111，这里回 2222（AES-128-GCM，MCTP 0x06）。 */
	status = libspdm_register_vendor_get_id_callback_func(spdm_context, spdm_vendor_get_id);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("register vendor get_id failed: 0x%x", status);
		return -EIO;
	}
	status = libspdm_register_vendor_callback_func(spdm_context, spdm_vendor_response);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("register vendor callback failed: 0x%x", status);
		return -EIO;
	}

	/* 检查 buffer 尺寸、DataTransferSize 等是否自洽。 */
	if (!libspdm_check_context(spdm_context)) {
		LOG_ERR("libspdm_check_context failed");
		return -EINVAL;
	}

	LOG_INF("SPDM responder ready (SPDM 1.3, EVENT+MULTI_KEY, session 1111/2222)");
	return 0;
}

/**
 * @brief libmctp 收包回调：过滤 SPDM，缓存后唤醒 main()。
 *
 * USB 驱动组完一整条 MCTP 消息（可能已经重组过分片）后调用这里。
 * 只处理 type 0x05（明文 SPDM）和 0x06（加密 SPDM）；其它 type 丢掉。
 *
 * @param eid        源 Endpoint ID，回包时作为目的地址写入 pending_eid。
 * @param tag_owner  对端是否持有 message tag。请求一般为 true，本例程未使用。
 * @param msg_tag    MCTP message tag（0..7），回包时原样使用。
 * @param data       mctp_set_rx_all() 注册的用户指针，本例程为 NULL。
 * @param msg        MCTP 消息体，首字节为 message type。
 * @param len        msg 长度（字节）。
 *
 * @return 无。副作用：填充 pending_* 并 k_sem_give(spdm_rx_sem)。
 */
static void rx_message(uint8_t eid, bool tag_owner, uint8_t msg_tag, void *data, void *msg,
		       size_t len)
{
	uint8_t type;

	ARG_UNUSED(tag_owner);
	ARG_UNUSED(data);

	if ((msg == NULL) || (len == 0)) {
		return;
	}

	/* bit7 是 IC（完整性校验）标志，真正的 type 在低 7 位。 */
	type = ((uint8_t *)msg)[0] & 0x7f;
	if ((type != MCTP_MESSAGE_TYPE_SPDM) && (type != MCTP_MESSAGE_TYPE_SECURED_MCTP)) {
		LOG_INF("Ignore MCTP type 0x%02x from EID %u (%u bytes)", type, eid,
			(unsigned int)len);
		return;
	}

	if (len > sizeof(pending_rx)) {
		LOG_ERR("SPDM message too large: %u", (unsigned int)len);
		return;
	}

	memcpy(pending_rx, msg, len);
	pending_rx_len = len;
	pending_eid = eid;
	pending_msg_tag = msg_tag;
	pending_valid = true;
	k_sem_give(&spdm_rx_sem);
}

/**
 * @brief 例程入口：USB 枚举 → SPDM 初始化 → MCTP 收包循环。
 *
 * 阻塞在 spdm_rx_sem 上；每收到一帧 SPDM 请求调用一次
 * libspdm_responder_dispatch_message()。初始化失败时打印日志后返回 0，
 * 不复位系统。正常运行时 while(1) 不退出。
 *
 * @retval 0  初始化失败；或理论上不会走到（循环不退出）。
 */
int main(void)
{
	int ret;

	LOG_INF("SPDM over MCTP over USB, EID %d on %s", SELF_ID, CONFIG_BOARD_TARGET);

	ret = enable_usb_device_next();
	if (ret != 0) {
		LOG_ERR("Failed to enable USB device support");
		return 0;
	}

	/* 等主机完成 SET_CONFIGURATION，否则后面 MCTP 发不出去。 */
	while (!usb_configured) {
		k_msleep(5);
	}

	ret = spdm_responder_init();
	if (ret != 0) {
		LOG_ERR("SPDM responder init failed: %d", ret);
		return 0;
	}

	mctp_ctx = mctp_init();
	__ASSERT_NO_MSG(mctp_ctx != NULL);

	/* USB-MCTP 长度字段 1 字节且含 4 字节 USB 头，单包最大 255。
	 * Zephyr 默认 pkt_size = 255+4，封装后会超过 255，mctp_usb_tx 返回 -E2BIG，
	 * GET_CERTIFICATE 这种长响应就会让主机 USB 超时。改成 251 后 libmctp 会按此分片。
	 */
	mctp0.binding.pkt_size = MCTP_USB_MAX_PACKET_LENGTH - MCTP_USB_HEADER_SIZE;

	/* 把 USB binding 挂到本端 EID 10；所有消息都进 rx_message。 */
	mctp_register_bus(mctp_ctx, &mctp0.binding, SELF_ID);
	mctp_set_rx_all(mctp_ctx, rx_message, NULL);

	/* 响应端是被动的：USB 来一帧 SPDM → 信号量 → dispatch 一次。
	 * dispatch 内部会 receive → 按 opcode 处理 → send。
	 * 明文走 type 0x05，会话应用数据走 0x06（AES-128-GCM）。
	 */
	while (1) {
		libspdm_return_t status;

		k_sem_take(&spdm_rx_sem, K_FOREVER);
		status = libspdm_responder_dispatch_message(spdm_context);
		/* UNSUPPORTED_CAP 常见于对端探能力，不当错误刷屏。 */
		if (LIBSPDM_STATUS_IS_ERROR(status) && (status != LIBSPDM_STATUS_UNSUPPORTED_CAP)) {
			LOG_WRN("SPDM dispatch status: 0x%x", status);
		}
	}

	return 0;
}
