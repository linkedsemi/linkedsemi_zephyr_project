/*
 * Copyright 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 *
 * SPDM over MCTP over USB **请求端**例程。
 *
 * 和响应方例程的差别只在 SPDM 角色，USB 仍然是 gadget：
 *
 *   本板 USB 设备栈（MCTP class，VID 2FE3 PID 0001）
 *     -> libmctp（本端 EID = 10，对端主机 EID = 20）
 *       -> libspdm requester（主动发 GET_VERSION ... END_SESSION）
 *
 * 主机上要先跑本目录 Python 响应端，再插板或等本例程 5 秒延迟：
 *   python usb_host_tester_windows.py
 *   python3 usb_host_tester_linux.py
 *
 * 主动流程（全部 SPDM 1.3）：
 *   GET_VERSION / GET_CAPABILITIES / NEGOTIATE_ALGORITHMS
 *   GET_DIGESTS / GET_CERTIFICATE / CHALLENGE     验证对端身份
 *   GET_KEY_PAIR_INFO / SET_KEY_PAIR_INFO CHANGE  测多密钥绑定
 *   KEY_EXCHANGE / FINISH（EVENT_ALL_POLICY）      建会话
 *   GET_SUPPORTED_EVENT_TYPES / SUBSCRIBE          测事件订阅
 *   VENDOR_DEFINED 1111 -> 2222（加密通道）
 *   退订 / END_SESSION
 *
 * 请求方例程仍链接 ../spdm_response/src/spdm_secret.c：共享 libspdm 编了
 * responder_lib，链接时需要事件/密钥对/签名这些 HAL 符号。
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
#include "library/spdm_requester_lib.h"
#include "library/spdm_transport_mctp_lib.h"
#include "library/spdm_crypt_lib.h"

#include "certs_generated.h"

LOG_MODULE_REGISTER(spdm_mctp_req, LOG_LEVEL_INF);

/** 本端 MCTP Endpoint ID，和响应方例程相同，主机按 EID 10 找设备。 */
#define SELF_ID      10
/** 对端（PC / 总线 owner）MCTP Endpoint ID。请求都发到这里。 */
#define BUS_OWNER_ID 20

/** libspdm 发送缓存：含 MCTP type 字节 + SPDM 报文。 */
#define SPDM_SENDER_BUFFER_SIZE   2048
/** libspdm 接收缓存。 */
#define SPDM_RECEIVER_BUFFER_SIZE 2048
/** 单条 SPDM 消息最大长度（不含传输层）。 */
#define SPDM_MAX_MSG_SIZE         2048

/**
 * 请求方 send 之后立刻 receive 等应答。USB 上来的帧由 rx_message() give，
 * spdm_device_receive_message() take。超时在 receive 回调里用 K_MSEC 实现。
 */
K_SEM_DEFINE(spdm_rx_sem, 0, 1);
/**
 * USB-MCTP binding。USB 角色仍是 gadget（和响应方同一套 class），
 * 只有 SPDM 角色换成 requester。pkt_size 同样在 main() 里改成 251。
 */
MCTP_USB_DEFINE(mctp0, USBD_MCTP_SUBCLASS_MANAGED_DEVICE_ENDPOINT, USBD_MCTP_PROTOCOL_1_X);

static struct mctp *mctp_ctx;   /**< libmctp 实例，USB 枚举完成后再创建。 */
static bool usb_configured;     /**< 已收到 USB SET_CONFIGURATION。 */

static uint8_t *spdm_context; /**< libspdm 上下文（含 session 安全消息区）。 */
static uint8_t *spdm_scratch; /**< encode/decode 临时区。 */

static uint8_t sender_buf[SPDM_SENDER_BUFFER_SIZE];
static uint8_t receiver_buf[SPDM_RECEIVER_BUFFER_SIZE];
static bool sender_in_use;      /**< true = libspdm 正在 encode，未 release。 */
static bool receiver_in_use;    /**< true = libspdm 正在 decode，未 release。 */

/** 最近一帧待处理的 MCTP 载荷（含 1 字节 message type）。 */
static uint8_t pending_rx[SPDM_RECEIVER_BUFFER_SIZE];
static size_t pending_rx_len;
static bool pending_valid;
/**
 * 本端作为请求方持有 message tag。每发一请求加 1，模 8。
 * 响应方例程相反：它回包时复用请求带来的 tag。
 */
static uint8_t tx_msg_tag;

/**
 * @brief USB 设备栈消息回调：感知枚举进度。
 *
 * 必须等到 SET_CONFIGURATION 之后才主动发 GET_VERSION。
 * 更早发时主机还没认到接口，包会丢。
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
 * USB 角色仍是 gadget，和响应方例程同一套描述符（VID 2FE3 PID 0001）。
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
 * @brief libspdm 发送回调：把请求经 MCTP 发给主机。
 *
 * 请求方自己分配 MCTP tag（tag_owner=true），目的 EID 固定是 BUS_OWNER_ID。
 * message 里已经带了 1 字节 MCTP type（0x05 明文 / 0x06 密文）。
 *
 * @param context       libspdm 上下文。本例程用全局 mctp_ctx，此参数未使用。
 * @param message_size  待发送字节数，含 MCTP message type。
 * @param message       sender_buf 里 encode 好的报文。
 * @param timeout       超时（微秒）。本例程忽略，发送本身不阻塞等 ACK。
 *
 * @retval LIBSPDM_STATUS_SUCCESS    已交给 mctp_message_tx()。
 * @retval LIBSPDM_STATUS_SEND_FAIL  参数非法，或 MCTP 发送失败。
 */
static libspdm_return_t spdm_device_send_message(void *context, size_t message_size,
						 const void *message, uint64_t timeout)
{
	int rc;
	uint8_t tag;

	ARG_UNUSED(context);
	ARG_UNUSED(timeout);

	if ((mctp_ctx == NULL) || (message == NULL) || (message_size == 0)) {
		return LIBSPDM_STATUS_SEND_FAIL;
	}

	/* 请求方持有 tag：本次用当前值，然后 0..7 循环加一。 */
	tag = tx_msg_tag;
	tx_msg_tag = (tx_msg_tag + 1) & 0x07;

	LOG_INF("SPDM TX %u bytes to EID %u tag %u", (unsigned int)message_size, BUS_OWNER_ID, tag);

	/* tag_owner=true：这是新请求，不是对别人的应答。 */
	rc = mctp_message_tx(mctp_ctx, BUS_OWNER_ID, true, tag, (void *)message, message_size);
	if (rc != 0) {
		LOG_ERR("mctp_message_tx failed: %d", rc);
		return LIBSPDM_STATUS_SEND_FAIL;
	}

	return LIBSPDM_STATUS_SUCCESS;
}

/**
 * @brief libspdm 接收回调：阻塞等待 USB 上的 SPDM 应答。
 *
 * 请求方在 send 之后会立刻 receive，所以这里要等 rx_message() 把帧
 * 放进 pending_rx。timeout 单位是微秒（libspdm 惯例）；
 * 为 0 时用默认 8s，给主机 Python 响应端留余量。
 *
 * @param context        libspdm 上下文，本例程未使用。
 * @param message_size   入参为 buffer 容量；成功后改为实际长度。
 * @param message        入参为接收 buffer 指针（*message 指向 receiver_buf）。
 * @param timeout        等待上限（微秒）。0 表示用默认 8000ms；
 *                       实际等待会被夹在 8000ms ~ 15000ms（USB+Python 比 ST1 慢）。
 *
 * @retval LIBSPDM_STATUS_SUCCESS       已填充一帧，并清掉 pending_valid。
 * @retval LIBSPDM_STATUS_RECEIVE_FAIL  参数非法、超时、或长度超出 buffer。
 */
static libspdm_return_t spdm_device_receive_message(void *context, size_t *message_size,
						    void **message, uint64_t timeout)
{
	uint8_t *buf;
	int64_t wait_ms;
	int ret;

	ARG_UNUSED(context);

	if ((message_size == NULL) || (message == NULL) || (*message == NULL)) {
		return LIBSPDM_STATUS_RECEIVE_FAIL;
	}

	/* libspdm 传微秒；k_sem_take 用毫秒。
	 *
	 * GET_VERSION 发生在协商 CT 之前，超时公式是 RTT + ST1。
	 * 规范 ST1 = 100ms。USB + 主机 Python 经常超过 100ms，所以下限 8s。
	 * timeout==0 同样用 8s（响应端 dispatch 路径）。
	 */
	wait_ms = 8000;
	if (timeout != 0) {
		wait_ms = (int64_t)(timeout / 1000);
		if (wait_ms < 8000) {
			wait_ms = 8000;
		}
		if (wait_ms > 15000) {
			wait_ms = 15000;
		}
	}

	/* 已经有缓存帧就不必再等（例如 USB 比 libspdm 更快到）。 */
	if (!pending_valid) {
		ret = k_sem_take(&spdm_rx_sem, K_MSEC(wait_ms));
		if (ret != 0) {
			LOG_ERR("SPDM RX timeout (%lld ms)", (long long)wait_ms);
			return LIBSPDM_STATUS_RECEIVE_FAIL;
		}
	}

	if (!pending_valid || (pending_rx_len > *message_size)) {
		return LIBSPDM_STATUS_RECEIVE_FAIL;
	}

	buf = *message;
	memcpy(buf, pending_rx, pending_rx_len);
	*message_size = pending_rx_len;
	pending_valid = false;

	return LIBSPDM_STATUS_SUCCESS;
}

/**
 * @brief 申请 libspdm 发送 buffer。
 *
 * encode 时占用 sender_buf，send 完成后必须 release。
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
 * @param msg_buf_ptr  先前 acquire 得到的指针，本例程不校验。
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
 * @brief 从 SPDM 证书链 blob 里切出第一张 DER 证书（CA）。
 *
 * blob 起点应是 RootHash 之后的 DER 区（跳过 Length(2)+Reserved(2)+RootHash(32)）。
 * ASN.1 SEQUENCE 开头是 0x30，随后是短/长形式长度。
 * 切出的 CA 用来注册 LIBSPDM_DATA_PEER_PUBLIC_ROOT_CERT，
 * GET_CERTIFICATE 时 libspdm 用它验对端叶子证书。
 *
 * @param blob      DER 区起点，必须是 0x30 SEQUENCE。
 * @param blob_len  blob 剩余字节数。
 * @param out       出参：指向第一张证书的起始（即 blob 本身）。
 * @param out_len   出参：第一张证书的总长度（TLV 头 + 内容）。
 *
 * @retval true   已解析出一张完整 DER。
 * @retval false  不是 SEQUENCE、长度形式无法识别、或声明长度超出 blob。
 */
static bool parse_first_der(const uint8_t *blob, size_t blob_len, const uint8_t **out, size_t *out_len)
{
	size_t hdr;
	size_t length;

	if ((blob == NULL) || (blob_len < 2) || (blob[0] != 0x30)) {
		return false;
	}

	if (blob[1] < 0x80) {
		/* 短形式：长度 < 128，就在第 2 字节。 */
		length = blob[1];
		hdr = 2;
	} else if ((blob[1] == 0x81) && (blob_len >= 3)) {
		/* 长形式 1 字节长度：0x81 后面跟 1 字节。 */
		length = blob[2];
		hdr = 3;
	} else if ((blob[1] == 0x82) && (blob_len >= 4)) {
		/* 长形式 2 字节长度：0x82 后面跟大端 16 位。CA DER 通常走这条。 */
		length = ((size_t)blob[2] << 8) | blob[3];
		hdr = 4;
	} else {
		return false;
	}

	if ((hdr + length) > blob_len) {
		return false;
	}

	*out = blob;
	*out_len = hdr + length;
	return true;
}

/**
 * @brief 分配并配置 libspdm 请求端上下文。
 *
 * 步骤与响应方类似（context / 传输回调 / scratch / set_data），差别在于：
 *   - 能力 flags 用 REQUEST_FLAGS（描述“我这台请求方能干什么”）
 *   - 注册的是对端根证书，不是本端证书链
 *   - RTT_US 给 libspdm 算等待应答的超时
 *
 * 无入参。结果写到全局 spdm_context / spdm_scratch。
 *
 * @retval 0        成功，上下文已就绪。
 * @retval -ENOMEM  上下文或 scratch 分配失败。
 * @retval -EIO     libspdm_init_context() 失败。
 * @retval -EINVAL  切不出 CA DER，或 libspdm_check_context() 失败。
 */
static int spdm_requester_init(void)
{
	libspdm_data_parameter_t parameter;
	libspdm_return_t status;
	size_t ctx_size;
	size_t scratch_size;
	uint8_t data8;
	uint16_t data16;
	uint32_t data32;
	uint64_t data64;
	spdm_version_number_t spdm_version;
	const uint8_t *ca_der;
	size_t ca_len;

	/* ---------- 1. 分配并初始化 libspdm 上下文 ---------- */
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
	/* 请求方 send 自己分配 tag，receive 会阻塞等 USB 应答。 */
	libspdm_register_device_io_func(spdm_context, spdm_device_send_message,
					spdm_device_receive_message);
	/* 传输层：给 SPDM 报文加减 1 字节 MCTP type（0x05/0x06）。 */
	libspdm_register_transport_layer_func(spdm_context, SPDM_MAX_MSG_SIZE,
					      LIBSPDM_MCTP_TRANSPORT_HEADER_SIZE,
					      LIBSPDM_MCTP_TRANSPORT_TAIL_SIZE,
					      libspdm_transport_mctp_encode_message,
					      libspdm_transport_mctp_decode_message);
	libspdm_register_device_buffer_func(spdm_context, SPDM_SENDER_BUFFER_SIZE,
					    SPDM_RECEIVER_BUFFER_SIZE, spdm_acquire_sender_buffer,
					    spdm_release_sender_buffer,
					    spdm_acquire_receiver_buffer,
					    spdm_release_receiver_buffer);

	/* ---------- 3. scratch：安全消息/分片等临时区 ---------- */
	scratch_size = libspdm_get_sizeof_required_scratch_buffer(spdm_context);
	spdm_scratch = k_malloc(scratch_size);
	if (spdm_scratch == NULL) {
		LOG_ERR("Failed to allocate SPDM scratch buffer (%u)", (unsigned int)scratch_size);
		return -ENOMEM;
	}
	LOG_INF("SPDM scratch buffer size: %u", (unsigned int)scratch_size);
	memset(spdm_scratch, 0, scratch_size);
	libspdm_set_scratch_buffer(spdm_context, spdm_scratch, scratch_size);

	/* ---------- 4. 本端作为请求方声明的能力（GET_CAPABILITIES 请求体） ---------- */
	memset(&parameter, 0, sizeof(parameter));
	parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;

	/* 只谈 SPDM 1.3。GET_VERSION 会带这个入口。 */
	spdm_version = SPDM_MESSAGE_VERSION_13 << SPDM_VERSION_NUMBER_SHIFT_BIT;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_SPDM_VERSION, &parameter, &spdm_version,
			 sizeof(spdm_version));

	/* CTExponent：计算复杂度超时。请求方填 0 即可。 */
	data8 = 0;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT, &parameter, &data8,
			 sizeof(data8));

	/* 等待对端应答的往返时间。libspdm 要求 uint64_t，传 uint32 会被直接丢掉，
	 * rtt 保持 0，GET_VERSION 超时就只剩 ST1=100ms。
	 */
	data64 = 5000000;
	status = libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_RTT_US, &parameter,
				  &data64, sizeof(data64));
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("set RTT_US failed: 0x%x", status);
		return -EINVAL;
	}

	/* 单条 SPDM 消息上限，须 ≤ sender/receiver buffer。 */
	data32 = SPDM_MAX_MSG_SIZE;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_DATA_TRANSFER_SIZE, &parameter,
			 &data32, sizeof(data32));
	libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_MAX_SPDM_MSG_SIZE, &parameter,
			 &data32, sizeof(data32));

	/* 请求方 GET_CAPABILITIES 里填的是“我能做什么”，不是设备能力。
	 * CERT_CAP：本端有证书材料（规范：MULTI_KEY_CAP 必须同时带 CERT_CAP，
	 *           否则对端回 InvalidRequest）。本例程不做双向认证，但位置仍要合法。
	 * ENCRYPT/MAC/KEY_EX/HANDSHAKE_IN_THE_CLEAR：要做明文握手 + AES-GCM 会话。
	 * MULTI_KEY_CAP_NEG：允许协商 OtherParams.MULTI_KEY_CONN。
	 * 没有 EVENT_CAP：事件能力是响应方声明的；请求方只是去订阅。
	 */
	data32 = SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CERT_CAP |
		 SPDM_GET_CAPABILITIES_REQUEST_FLAGS_ENCRYPT_CAP |
		 SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MAC_CAP |
		 SPDM_GET_CAPABILITIES_REQUEST_FLAGS_KEY_EX_CAP |
		 SPDM_GET_CAPABILITIES_REQUEST_FLAGS_HANDSHAKE_IN_THE_CLEAR_CAP |
		 SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MULTI_KEY_CAP_NEG;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter, &data32,
			 sizeof(data32));

	/* 哈希 / 非对称必须和响应方例程一致，否则 CHALLENGE 验签失败。 */
	data32 = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_256;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO, &parameter, &data32,
			 sizeof(data32));
	data32 = SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P256;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO, &parameter, &data32,
			 sizeof(data32));

	/* 度量未开，和响应方一样填 0。 */
	data8 = 0;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_SPEC, &parameter, &data8,
			 sizeof(data8));
	data32 = 0;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO, &parameter, &data32,
			 sizeof(data32));

	/* 与响应方例程同一套会话算法，否则 NEGOTIATE_ALGORITHMS 对不上。 */
	data16 = SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_256_R1;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_DHE_NAME_GROUP, &parameter, &data16,
			 sizeof(data16));
	data16 = SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_AES_128_GCM;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_AEAD_CIPHER_SUITE, &parameter, &data16,
			 sizeof(data16));
	/* 0 = 本端不做 MUT_AUTH，不向对端出示请求方证书。 */
	data16 = 0;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG, &parameter, &data16,
			 sizeof(data16));
	data16 = SPDM_ALGORITHMS_KEY_SCHEDULE_HMAC_HASH;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_KEY_SCHEDULE, &parameter, &data16,
			 sizeof(data16));
	/* OpaqueDataFormat1 是 KEY_EX 的硬性要求。
	 * MULTI_KEY_CONN 在请求里表示 ResponderMultiKeyConn：希望对端
	 * DIGESTS 带 KeyPairID。对端若也支持，协商成功。
	 */
	data8 = SPDM_ALGORITHMS_OPAQUE_DATA_FORMAT_1 | SPDM_ALGORITHMS_MULTI_KEY_CONN;
	libspdm_set_data(spdm_context, LIBSPDM_DATA_OTHER_PARAMS_SUPPORT, &parameter, &data8,
			 sizeof(data8));

	/* 信任锚：从与响应方例程同一份 chain blob 里切出 CA DER。
	 * 偏移 = Length(2) + Reserved(2) + RootHash(32)。
	 * Python 响应端必须用同一套 CA/设备证，否则 GET_CERTIFICATE 验签失败。
	 */
	if (!parse_first_der(g_spdm_cert_chain + sizeof(uint16_t) + sizeof(uint16_t) + 32,
			     g_spdm_cert_chain_len - (sizeof(uint16_t) + sizeof(uint16_t) + 32),
			     &ca_der, &ca_len)) {
		LOG_ERR("failed to parse CA DER from baked cert chain");
		return -EINVAL;
	}
	libspdm_set_data(spdm_context, LIBSPDM_DATA_PEER_PUBLIC_ROOT_CERT, &parameter,
			 (void *)ca_der, ca_len);

	if (!libspdm_check_context(spdm_context)) {
		LOG_ERR("libspdm_check_context failed");
		return -EINVAL;
	}

	LOG_INF("SPDM requester ready (SPDM 1.3)");
	return 0;
}

/**
 * @brief 主动跑完一遍 SPDM 1.3 流程。
 *
 * 任一步失败就返回，方便看串口定位。
 * 多密钥 GET/SET_KEY_PAIR_INFO 在会话外就能发（不需要加密）。
 * 事件必须在 start_session 成功之后，走 type 0x06。
 *
 * 无入参。使用全局 spdm_context。
 *
 * @retval 0     全部步骤成功。
 * @retval -EIO  某一步 libspdm API 失败，或 vendor ping 应答不是 "2222"。
 */
static int spdm_requester_run(void)
{
	libspdm_return_t status;
	uint8_t slot_mask;
	uint8_t digest[32];
	uint8_t cert_chain[LIBSPDM_MAX_CERT_CHAIN_SIZE];
	size_t cert_chain_size;
	uint8_t meas_hash[32];
	uint32_t session_id;
	uint8_t heartbeat;
	uint8_t total_key_pairs;
	uint16_t capabilities;
	uint16_t key_usage_cap;
	uint16_t current_key_usage;
	uint32_t asym_cap;
	uint32_t current_asym;
	uint8_t assoc_mask;
	uint8_t public_key_info[32];
	uint16_t public_key_info_len;
	uint8_t event_group_count;
	uint8_t event_list[64];
	uint32_t event_list_len;
	static const uint8_t ping[4] = {'1', '1', '1', '1'};
	uint8_t pong[16];
	uint16_t pong_size;
	uint16_t rsp_std_id;
	uint8_t rsp_vid_len;
	uint8_t rsp_vid[8];
	int i;

	/* VCA：GET_VERSION + GET_CAPABILITIES + NEGOTIATE_ALGORITHMS。
	 * 第二个参数 false = 不要跳过 version 协商。
	 */
	LOG_INF("GET_VERSION / CAPABILITIES / ALGORITHMS");
	status = libspdm_init_connection(spdm_context, false);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("libspdm_init_connection failed: 0x%x", status);
		return -EIO;
	}

	/* 第二个参数 NULL = 明文（会话外）。协商了 MULTI_KEY 时，
	 * 应答在 32 字节摘要后还有 KeyPairID 等字段，libspdm 自己解析。
	 * slot_mask 的 bit 表示哪些槽有证书；digest 是 slot 0 的 SHA-256。
	 */
	LOG_INF("GET_DIGESTS");
	status = libspdm_get_digest(spdm_context, NULL, &slot_mask, digest);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("GET_DIGESTS failed: 0x%x", status);
		return -EIO;
	}
	LOG_INF("slot_mask=0x%02x digest=%02x%02x...", slot_mask, digest[0], digest[1]);

	/* 第三个参数 0 = 拉 slot 0 整条链。libspdm 会用前面注册的 CA 验叶子。 */
	LOG_INF("GET_CERTIFICATE");
	cert_chain_size = sizeof(cert_chain);
	status = libspdm_get_certificate(spdm_context, NULL, 0, &cert_chain_size, cert_chain);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("GET_CERTIFICATE failed: 0x%x", status);
		return -EIO;
	}
	LOG_INF("certificate chain %u bytes", (unsigned int)cert_chain_size);

	/* slot 0，不要测量摘要。1.3 的 8 字节 requester_context 由 libspdm 自动填。
	 * 对端用设备私钥签 transcript，本端用叶子证书验签。
	 */
	LOG_INF("CHALLENGE");
	status = libspdm_challenge(spdm_context, NULL, 0,
				   SPDM_CHALLENGE_REQUEST_NO_MEASUREMENT_SUMMARY_HASH, meas_hash,
				   &slot_mask);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("CHALLENGE failed: 0x%x", status);
		return -EIO;
	}

	/* 多密钥：查 id=1 的绑定，应是 slot 0（assoc_mask bit0）。
	 * 第三个参数 NULL = 会话外明文。
	 */
	public_key_info_len = sizeof(public_key_info);
	LOG_INF("GET_KEY_PAIR_INFO id=1");
	status = libspdm_get_key_pair_info(spdm_context, NULL, 1, &total_key_pairs, &capabilities,
					   &key_usage_cap, &current_key_usage, &asym_cap,
					   &current_asym, &assoc_mask, &public_key_info_len,
					   public_key_info);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("GET_KEY_PAIR_INFO failed: 0x%x", status);
		return -EIO;
	}
	LOG_INF("key pairs=%u assoc_slot=0x%02x usage=0x%04x", total_key_pairs, assoc_mask,
		current_key_usage);

	/* CHANGE 且把刚才读到的值原样写回：验证“改绑定”通路，实际不改槽位。 */
	LOG_INF("SET_KEY_PAIR_INFO CHANGE (keep slot 0 binding)");
	status = libspdm_set_key_pair_info(spdm_context, NULL, 1,
					   SPDM_SET_KEY_PAIR_INFO_CHANGE_OPERATION,
					   current_key_usage, current_asym, assoc_mask);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("SET_KEY_PAIR_INFO failed: 0x%x", status);
		return -EIO;
	}

	/* 再 GET 一次，确认 CHANGE 之后仍是 slot 0。 */
	public_key_info_len = sizeof(public_key_info);
	status = libspdm_get_key_pair_info(spdm_context, NULL, 1, &total_key_pairs, &capabilities,
					   &key_usage_cap, &current_key_usage, &asym_cap,
					   &current_asym, &assoc_mask, &public_key_info_len,
					   public_key_info);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("GET_KEY_PAIR_INFO (after SET) failed: 0x%x", status);
		return -EIO;
	}
	LOG_INF("after SET: assoc_slot=0x%02x usage=0x%04x", assoc_mask, current_key_usage);

	/* use_psk=false：走 KEY_EXCHANGE/FINISH，不是 PSK。
	 * EVENT_ALL_POLICY：会话一建立就订阅全部事件（对端 HAL 会收到 ALL）。
	 * slot 0；不要测量摘要。明文握手：FINISH 仍走 0x05，之后应用数据走 0x06。
	 */
	LOG_INF("KEY_EXCHANGE / FINISH (EVENT_ALL_POLICY)");
	status = libspdm_start_session(spdm_context, false, NULL, 0,
				       SPDM_KEY_EXCHANGE_REQUEST_NO_MEASUREMENT_SUMMARY_HASH, 0,
				       SPDM_KEY_EXCHANGE_REQUEST_SESSION_POLICY_EVENT_ALL_POLICY,
				       &session_id, &heartbeat, meas_hash);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("start_session failed: 0x%x", status);
		return -EIO;
	}
	LOG_INF("session 0x%x established", session_id);

	/* 会话内：先问对端支持哪些事件，再按这份列表订阅（LIST，不是 ALL）。
	 * 必须带 session_id，走加密通道。
	 */
	event_list_len = sizeof(event_list);
	LOG_INF("GET_SUPPORTED_EVENT_TYPES");
	status = libspdm_get_event_types(spdm_context, session_id, &event_group_count,
					 &event_list_len, event_list);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("GET_SUPPORTED_EVENT_TYPES failed: 0x%x", status);
		return -EIO;
	}
	LOG_INF("event groups=%u list_len=%u", event_group_count, event_list_len);

	LOG_INF("SUBSCRIBE_EVENT_TYPES (supported list)");
	status = libspdm_subscribe_event_types(spdm_context, session_id, event_group_count,
					       event_list_len, event_list);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("SUBSCRIBE_EVENT_TYPES failed: 0x%x", status);
		return -EIO;
	}

	/* 加密通道上的应用数据：StandardID=0、VendorID 空，载荷 ASCII "1111"，
	 * 期望对端回 "2222"。隔 1s 共 3 次。
	 */
	for (i = 0; i < 3; i++) {
		pong_size = sizeof(pong);
		rsp_vid_len = sizeof(rsp_vid);
		status = libspdm_vendor_send_request_receive_response(
			spdm_context, &session_id, 0, 0, NULL, (uint16_t)sizeof(ping), ping,
			&rsp_std_id, &rsp_vid_len, rsp_vid, &pong_size, pong);
		if (LIBSPDM_STATUS_IS_ERROR(status) || (pong_size != 4) ||
		    (memcmp(pong, "2222", 4) != 0)) {
			LOG_ERR("vendor ping failed: 0x%x pong_size=%u", status, pong_size);
			return -EIO;
		}
		LOG_INF("session ping %d/3: 1111 -> 2222", i + 1);
		k_sleep(K_SECONDS(1));
	}

	/* 组数 0 + list 为空 = 退订全部。 */
	LOG_INF("SUBSCRIBE_EVENT_TYPES unsubscribe");
	status = libspdm_subscribe_event_types(spdm_context, session_id, 0, 0, NULL);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("unsubscribe failed: 0x%x", status);
		return -EIO;
	}

	/* 第三个参数 0 = 没有 end_session_attributes。仍走加密通道。 */
	LOG_INF("END_SESSION");
	status = libspdm_stop_session(spdm_context, session_id, 0);
	if (LIBSPDM_STATUS_IS_ERROR(status)) {
		LOG_ERR("END_SESSION failed: 0x%x", status);
		return -EIO;
	}

	LOG_INF("SPDM 1.3 requester flow succeeded");
	return 0;
}

/**
 * @brief libmctp 收包回调：缓存 SPDM 应答，唤醒正在 receive 里等的 libspdm。
 *
 * 请求方只关心 type 0x05 / 0x06。目的 EID 和 tag 由 libspdm 在协议层匹配，
 * 这里不必再看 eid / msg_tag。
 *
 * @param eid        源 Endpoint ID，本例程未使用。
 * @param tag_owner  对端是否持有 tag。应答一般为 false，本例程未使用。
 * @param msg_tag    MCTP message tag，本例程未使用。
 * @param data       mctp_set_rx_all() 注册的用户指针，本例程为 NULL。
 * @param msg        MCTP 消息体，首字节为 message type。
 * @param len        msg 长度（字节）。
 *
 * @return 无。副作用：填充 pending_rx 并 k_sem_give(spdm_rx_sem)。
 */
static void rx_message(uint8_t eid, bool tag_owner, uint8_t msg_tag, void *data, void *msg,
		       size_t len)
{
	uint8_t type;

	ARG_UNUSED(tag_owner);
	ARG_UNUSED(data);
	ARG_UNUSED(msg_tag);
	ARG_UNUSED(eid);

	if ((msg == NULL) || (len == 0)) {
		return;
	}

	/* bit7 是 IC 标志，真正的 type 在低 7 位。 */
	type = ((uint8_t *)msg)[0] & 0x7f;
	if ((type != MCTP_MESSAGE_TYPE_SPDM) && (type != MCTP_MESSAGE_TYPE_SECURED_MCTP)) {
		LOG_INF("Ignore MCTP type 0x%02x (%u bytes)", type, (unsigned int)len);
		return;
	}

	if (len > sizeof(pending_rx)) {
		LOG_ERR("SPDM message too large: %u", (unsigned int)len);
		return;
	}

	memcpy(pending_rx, msg, len);
	pending_rx_len = len;
	pending_valid = true;
	k_sem_give(&spdm_rx_sem);
}

/**
 * @brief 例程入口：USB 枚举 → 配 SPDM → 等主机认接口 → 主动跑握手。
 *
 * 5 秒延迟：给 Windows/Linux 上位机 claim USB 接口的时间。
 * 跑完后空转，避免 main 退出把内核带下去。
 *
 * @retval 0  初始化失败，或握手结束后停在空转循环（不会真正返回）。
 */
int main(void)
{
	int ret;

	LOG_INF("SPDM requester over MCTP over USB, EID %d on %s", SELF_ID, CONFIG_BOARD_TARGET);

	ret = enable_usb_device_next();
	if (ret != 0) {
		LOG_ERR("Failed to enable USB device support");
		return 0;
	}

	/* 等主机完成 SET_CONFIGURATION。 */
	while (!usb_configured) {
		k_msleep(5);
	}

	ret = spdm_requester_init();
	if (ret != 0) {
		LOG_ERR("SPDM requester init failed: %d", ret);
		return 0;
	}

	mctp_ctx = mctp_init();
	__ASSERT_NO_MSG(mctp_ctx != NULL);

	/* USB-MCTP 长度字段含 4 字节 USB 头，单包 ≤255。pkt_size=251 让 libmctp 分片。 */
	mctp0.binding.pkt_size = MCTP_USB_MAX_PACKET_LENGTH - MCTP_USB_HEADER_SIZE;

	mctp_register_bus(mctp_ctx, &mctp0.binding, SELF_ID);
	mctp_set_rx_all(mctp_ctx, rx_message, NULL);

	/* 给上位机 Python 响应端 claim 接口的时间，然后再发 GET_VERSION。 */
	k_sleep(K_SECONDS(5));

	ret = spdm_requester_run();
	if (ret != 0) {
		LOG_ERR("SPDM requester run failed: %d", ret);
	}

	/* 握手无论成败都不要让 main 返回，否则 Zephyr 可能停调度。 */
	while (1) {
		k_sleep(K_SECONDS(10));
	}

	return 0;
}
