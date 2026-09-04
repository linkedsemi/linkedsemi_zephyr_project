#!/usr/bin/env python3
# Copyright 2026
# SPDX-License-Identifier: Apache-2.0
#
# PC 是 SPDM 请求方，测设备固件例程 spdm_response（设备是响应方）。
# 烧录 samples/spdm/spdm_response 后直接运行本文件。
# 本文件已包含 USB 后端 + SPDM 协议，无需其它 .py。
#
#   pip3 install pyusb cryptography
#   udev: SUBSYSTEM=="usb", ATTR{idVendor}=="2fe3", ATTR{idProduct}=="0001", MODE="0666"
#
# 证书在本例程 certs/ 目录（与固件 certs_generated.h 同一套 CA）。
from __future__ import annotations

import hashlib
import os
import re
import struct
import sys
import time

if sys.platform == "win32":
    sys.stderr.write("This is the Linux tester. On Windows run usb_host_tester_windows.py\n")
    sys.exit(1)

HERE = os.path.dirname(os.path.abspath(__file__))

from contextlib import suppress

import usb.core
import usb.util
from cryptography import x509
from cryptography.hazmat.primitives import hashes, hmac as crypto_hmac
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDFExpand
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat, load_pem_private_key

VID = 0x2FE3
PID = 0x0001

MCTP_DMTF0 = 0x1A
MCTP_DMTF1 = 0xB4

SELF_EID = 10
BUS_OWNER_EID = 20

MCTP_TYPE_SPDM = 0x05
MCTP_TYPE_SECURED = 0x06

SPDM_DIGESTS = 0x01
SPDM_CERTIFICATE = 0x02
SPDM_CHALLENGE_AUTH = 0x03
SPDM_VERSION = 0x04
SPDM_CAPABILITIES = 0x61
SPDM_ALGORITHMS = 0x63
SPDM_KEY_EXCHANGE_RSP = 0x64
SPDM_FINISH_RSP = 0x65
SPDM_END_SESSION_ACK = 0x6C
SPDM_SUBSCRIBE_EVENT_TYPES_ACK = 0x70
SPDM_KEY_PAIR_INFO = 0x7C
SPDM_SET_KEY_PAIR_INFO_ACK = 0x7D
SPDM_VENDOR_DEFINED_RESPONSE = 0x7E
SPDM_ERROR = 0x7F
SPDM_GET_DIGESTS = 0x81
SPDM_GET_CERTIFICATE = 0x82
SPDM_CHALLENGE = 0x83
SPDM_GET_VERSION = 0x84
SPDM_GET_CAPABILITIES = 0xE1
SPDM_GET_SUPPORTED_EVENT_TYPES = 0xE2
SPDM_NEGOTIATE_ALGORITHMS = 0xE3
SPDM_KEY_EXCHANGE = 0xE4
SPDM_FINISH = 0xE5
SPDM_END_SESSION = 0xEC
SPDM_SUBSCRIBE_EVENT_TYPES = 0xF0
SPDM_GET_KEY_PAIR_INFO = 0xFC
SPDM_SET_KEY_PAIR_INFO = 0xFD
SPDM_VENDOR_DEFINED_REQUEST = 0xFE
SPDM_SUPPORTED_EVENT_TYPES = 0x62

SPDM_VER = 0x13
SPDM_REQ_CONTEXT_SIZE = 8

SPDM_BASE_ASYM_ECDSA_P256 = 0x00000010
SPDM_BASE_HASH_SHA256 = 0x00000001
SPDM_DHE_SECP_256_R1 = 0x0008
SPDM_AEAD_AES_128_GCM = 0x0001
SPDM_KEY_SCHEDULE_SPDM = 0x0001
SPDM_OPAQUE_DATA_FORMAT_1 = 0x02
SPDM_MULTI_KEY_CONN = 0x10

SPDM_REQ_FLAGS_ENCRYPT = 0x00000040
SPDM_REQ_FLAGS_MAC = 0x00000080
SPDM_REQ_FLAGS_KEY_EX = 0x00000200
SPDM_REQ_FLAGS_HANDSHAKE_IN_THE_CLEAR = 0x00008000
SPDM_REQ_FLAGS_MULTI_KEY_NEG = 0x08000000
SPDM_RSP_FLAGS_CERT = 0x00000002
SPDM_RSP_FLAGS_CHAL = 0x00000004
SPDM_RSP_FLAGS_KEY_EX = 0x00000200
SPDM_RSP_FLAGS_EVENT = 0x02000000
SPDM_RSP_FLAGS_MULTI_KEY_NEG = 0x08000000
SPDM_RSP_FLAGS_MULTI_KEY_ONLY = 0x04000000
SPDM_RSP_FLAGS_GET_KEY_PAIR = 0x10000000
SPDM_RSP_FLAGS_SET_KEY_PAIR = 0x20000000

SPDM_KEY_USAGE_KEY_EX = 0x0001
SPDM_KEY_USAGE_CHALLENGE = 0x0002
SPDM_KEY_PAIR_ASYM_ECC256 = 0x00000008
SPDM_SET_KEY_PAIR_CHANGE = 0
SPDM_EVENT_ALL_POLICY = 0x02
SPDM_SUBSCRIBE_ATTR_ALL = 0x01
SPDM_CERT_MODEL_DEVICE = 0x01

# USB-MCTP LEN is 1 byte including the 4-byte USB header (max 255).
# SPDM CERTIFICATE header is 8 bytes; keep each portion inside one USB packet.
CERT_PORTION = 200

DEFAULT_TIMEOUT_MS = 8000

CA_CERT_PEM = os.path.join(HERE, "certs", "ca.cert.pem")
DEVICE_KEY_PEM = os.path.join(HERE, "certs", "device.key.pem")
CERT_CHAIN_BIN = os.path.join(HERE, "certs", "spdm_cert_chain.bin")


class MctpUsbTransport:
    """Framed MCTP-over-USB bulk transfer. Subclass fills in the endpoints."""

    def __init__(self):
        self.device = None
        self.endpoint_in = None
        self.endpoint_out = None
        self.interface = None
        self.in_mps = None
        self.out_mps = None
        self._rx_buf = bytearray()

    def send_data(self, data, timeout=1000):
        if not self.endpoint_out:
            raise RuntimeError("OUT endpoint not initialized")
        return self.endpoint_out.write(data, timeout=timeout)

    def _drain_in(self):
        self._rx_buf = bytearray()
        while True:
            with suppress(usb.core.USBError):
                _ = self.endpoint_in.read(self.in_mps, timeout=20)
                continue
            break

    def _read_in(self, timeout_ms):
        chunk = bytes(self.endpoint_in.read(self.in_mps, timeout=timeout_ms))
        if chunk:
            self._rx_buf.extend(chunk)

    @staticmethod
    def _find_header(buf):
        for i in range(max(0, len(buf) - 2)):
            if buf[i] == MCTP_DMTF0 and buf[i + 1] == MCTP_DMTF1 and buf[i + 2] == 0x00:
                return i
        return 0

    def recv_mctp_frame(self, timeout=DEFAULT_TIMEOUT_MS):
        deadline = time.time() + (timeout / 1000.0)

        while True:
            while len(self._rx_buf) < 4:
                remain_ms = int(max(0.0, (deadline - time.time()) * 1000))
                if remain_ms == 0:
                    raise usb.core.USBTimeoutError("Timeout waiting for MCTP header")
                self._read_in(remain_ms)

            idx = self._find_header(self._rx_buf)
            if idx > 0:
                del self._rx_buf[:idx]

            if len(self._rx_buf) < 4:
                continue

            if (
                self._rx_buf[0] != MCTP_DMTF0
                or self._rx_buf[1] != MCTP_DMTF1
                or self._rx_buf[2] != 0x00
            ):
                del self._rx_buf[0]
                continue

            total_len = int(self._rx_buf[3])
            if total_len < 4:
                del self._rx_buf[:4]
                continue

            while len(self._rx_buf) < total_len:
                remain_ms = int(max(0.0, (deadline - time.time()) * 1000))
                if remain_ms == 0:
                    raise usb.core.USBTimeoutError(
                        f"Timeout waiting full frame: have={len(self._rx_buf)} need={total_len}"
                    )
                self._read_in(remain_ms)

            frame = bytes(self._rx_buf[:total_len])
            del self._rx_buf[:total_len]
            return frame

    def select_bulk_interface(self, cfg):
        self.interface = None
        for intf in cfg:
            ep_in = usb.util.find_descriptor(
                intf,
                custom_match=lambda e: (
                    usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN
                    and usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK
                ),
            )
            ep_out = usb.util.find_descriptor(
                intf,
                custom_match=lambda e: (
                    usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
                    and usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK
                ),
            )
            if ep_in and ep_out:
                self.interface = intf
                self.endpoint_in = ep_in
                self.endpoint_out = ep_out
                break

        if not self.interface:
            raise ValueError("Bulk IN/OUT endpoints not found")

        self.in_mps = int(self.endpoint_in.wMaxPacketSize)
        self.out_mps = int(self.endpoint_out.wMaxPacketSize)

    def arm_bulk_pipes(self):
        with suppress(usb.core.USBError):
            self.device.clear_halt(self.endpoint_in.bEndpointAddress)
        with suppress(usb.core.USBError):
            self.device.clear_halt(self.endpoint_out.bEndpointAddress)
        self._drain_in()


def get_active_config(device):
    try:
        return device.get_active_configuration()
    except usb.core.USBError:
        device.set_configuration()
        return device.get_active_configuration()


def build_frame(payload):
    total_len = 4 + len(payload)
    if total_len > 255:
        raise ValueError(f"Frame too large for 1-byte LEN: total_len={total_len} (>255)")
    return bytes([MCTP_DMTF0, MCTP_DMTF1, 0x00, total_len]) + payload


def build_mctp(msg_body, msg_tag=0, tag_owner=True, dest=SELF_EID, src=BUS_OWNER_EID):
    flags = 0xC0 | (0x08 if tag_owner else 0) | (msg_tag & 0x07)
    mctp_hdr = bytes([0x01, dest, src, flags])
    return mctp_hdr + msg_body


def build_mctp_spdm(spdm_msg, msg_tag=0, tag_owner=True, dest=SELF_EID, src=BUS_OWNER_EID):
    return build_mctp(bytes([MCTP_TYPE_SPDM]) + spdm_msg, msg_tag=msg_tag, tag_owner=tag_owner,
                      dest=dest, src=src)


def parse_mctp_packet(frame):
    if len(frame) < 4:
        raise ValueError("USB frame too short")
    payload = frame[4:]
    if len(payload) < 5:
        raise ValueError(f"MCTP payload too short: {payload.hex()}")
    flags = payload[3]
    som = (flags >> 7) & 1
    eom = (flags >> 6) & 1
    return som, eom, payload[4:]


def hexdump(label, data):
    print(f"{label}: {data.hex(' ')}")


def expect_code(spdm, code, name):
    if len(spdm) < 2:
        raise SystemExit(f"{name}: response too short")
    if spdm[1] == SPDM_ERROR:
        raise SystemExit(f"{name}: SPDM ERROR 0x{spdm[2]:02x}/0x{spdm[3]:02x}")
    if spdm[1] != code:
        raise SystemExit(f"Expected {name} (0x{code:02x}), got 0x{spdm[1]:02x}")


def split_der_certs(blob: bytes) -> list[bytes]:
    certs = []
    i = 0
    while i < len(blob):
        if blob[i] != 0x30:
            raise ValueError(f"DER cert at offset {i} does not start with SEQUENCE")
        if blob[i + 1] < 0x80:
            length = blob[i + 1]
            hdr = 2
        elif blob[i + 1] == 0x81:
            length = blob[i + 2]
            hdr = 3
        elif blob[i + 1] == 0x82:
            length = (blob[i + 2] << 8) | blob[i + 3]
            hdr = 4
        else:
            raise ValueError(f"unsupported DER length at offset {i}")
        total = hdr + length
        certs.append(blob[i : i + total])
        i += total
    return certs


def spdm13_signing_context(op_context: bytes) -> bytes:
    prefix = bytearray(b"dmtf-spdm-v1.3.*")
    prefix[11] = ord("1")
    prefix[13] = ord("3")
    prefix[15] = ord("*")
    ctx = bytes(prefix) * 4
    pad = bytes(36 - len(op_context))
    return ctx + pad + op_context


def verify_challenge_auth(device_cert, m1m2_hash: bytes, signature: bytes) -> None:
    signed = spdm13_signing_context(b"responder-challenge_auth signing") + m1m2_hash
    digest = hashlib.sha256(signed).digest()
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    der_sig = encode_dss_signature(r, s)
    device_cert.public_key().verify(der_sig, digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))


class SpdmHost:
    def __init__(self, usb_dev):
        self.usb = usb_dev
        self.tag = 0
        self.transcript = bytearray()
        self.message_a = bytearray()
        self.message_d = bytearray()
        self.last_src = SELF_EID
        self.last_dst = BUS_OWNER_EID
        self.last_tag = 0

    def recv_mctp_payload(self) -> bytes:
        msg = bytearray()
        deadline = time.time() + (DEFAULT_TIMEOUT_MS / 1000.0)
        while True:
            remain_ms = int(max(1.0, (deadline - time.time()) * 1000))
            frame = self.usb.recv_mctp_frame(timeout=remain_ms)
            som, eom, body = parse_mctp_packet(frame)
            payload = frame[4:]
            if len(payload) >= 4:
                self.last_src = payload[2]
                self.last_dst = payload[1]
                self.last_tag = payload[3] & 0x07
            if som:
                msg = bytearray(body)
            else:
                msg.extend(body)
            if eom:
                break
        if not msg:
            raise ValueError("empty MCTP payload")
        return bytes(msg)

    def recv_spdm(self) -> bytes:
        msg = self.recv_mctp_payload()
        if (msg[0] & 0x7F) != MCTP_TYPE_SPDM:
            raise ValueError(f"Unexpected MCTP type in reassembled message: {msg[:8].hex()}")
        return bytes(msg[1:])

    def exchange(self, request: bytes, expected_code: int, name: str, record_a: bool = False) -> bytes:
        self.usb.send_data(build_frame(build_mctp_spdm(request, msg_tag=self.tag)))
        self.tag = (self.tag + 1) & 0x07
        rsp = self.recv_spdm()
        hexdump(name, rsp)
        expect_code(rsp, expected_code, name)
        self.transcript.extend(request)
        self.transcript.extend(rsp)
        if record_a:
            self.message_a.extend(request)
            self.message_a.extend(rsp)
        return rsp

    def exchange_secured(self, keys: "SecuredSession", request: bytes, expected_code: int, name: str) -> bytes:
        inner = bytes([MCTP_TYPE_SECURED]) + keys.encode(request)
        self.usb.send_data(build_frame(build_mctp(inner, msg_tag=self.tag)))
        self.tag = (self.tag + 1) & 0x07
        msg = self.recv_mctp_payload()
        if (msg[0] & 0x7F) != MCTP_TYPE_SECURED:
            raise ValueError(f"{name}: expected MCTP type 0x06, got {msg[:8].hex()}")
        rsp = keys.decode(msg[1:])
        hexdump(name, rsp)
        expect_code(rsp, expected_code, name)
        return rsp


def build_negotiate_algorithms() -> bytes:
    # DHE / AEAD / KeySchedule for KEY_EX. Skip ReqAsym (no MUT_AUTH).
    # AlgSupported in a table must be non-zero or libspdm returns InvalidRequest.
    tables = b""
    tables += struct.pack("<BBH", 2, 0x20, SPDM_DHE_SECP_256_R1)
    tables += struct.pack("<BBH", 3, 0x20, SPDM_AEAD_AES_128_GCM)
    tables += struct.pack("<BBH", 5, 0x20, SPDM_KEY_SCHEDULE_SPDM)
    param1 = 3
    length = 32 + len(tables)
    header = struct.pack("<BBBB", SPDM_VER, SPDM_NEGOTIATE_ALGORITHMS, param1, 0)
    body = struct.pack(
        "<HBBII",
        length,
        0,
        SPDM_OPAQUE_DATA_FORMAT_1 | SPDM_MULTI_KEY_CONN,
        SPDM_BASE_ASYM_ECDSA_P256,
        SPDM_BASE_HASH_SHA256,
    )
    body += bytes(12)
    body += bytes([0, 0, 0, 0])
    msg = header + body + tables
    if len(msg) != length:
        raise RuntimeError(f"NEGOTIATE_ALGORITHMS length {len(msg)} != {length}")
    return msg


def load_ca_cert():
    if not os.path.isfile(CA_CERT_PEM):
        raise SystemExit(
            f"Missing {CA_CERT_PEM}. Run: python certs/gen_certs.py "
            "(from this sample's certs/ directory)"
        )
    with open(CA_CERT_PEM, "rb") as f:
        return x509.load_pem_x509_certificate(f.read())


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def hmac_sha256(key: bytes, data: bytes) -> bytes:
    h = crypto_hmac.HMAC(key, hashes.SHA256())
    h.update(data)
    return h.finalize()


def hkdf_extract(salt: bytes, ikm: bytes) -> bytes:
    return hmac_sha256(salt, ikm)


def hkdf_expand(prk: bytes, info: bytes, length: int) -> bytes:
    return HKDFExpand(algorithm=hashes.SHA256(), length=length, info=info).derive(prk)


def spdm_bin_concat(label: str, length: int, context: bytes | None) -> bytes:
    # length(2 LE) || "spdm1.2 " || label || [TH hash]
    out = struct.pack("<H", length) + b"spdm1.3 " + label.encode("ascii")
    if context is not None:
        out += context
    return out


def dhe_p256_keypair():
    priv = ec.generate_private_key(ec.SECP256R1())
    pub = priv.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)
    if pub[0] != 0x04 or len(pub) != 65:
        raise RuntimeError(f"unexpected P-256 public point: {pub[:4].hex()} len={len(pub)}")
    return priv, pub[1:]


def dhe_p256_shared(priv, peer_xy: bytes) -> bytes:
    if len(peer_xy) != 64:
        raise RuntimeError(f"peer DHE key must be 64 bytes, got {len(peer_xy)}")
    peer = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), b"\x04" + peer_xy)
    return priv.exchange(ec.ECDH(), peer)


def derive_handshake_keys(shared_secret: bytes, th1: bytes):
    """SPDM 1.3 HKDF: handshake_secret, req/rsp hs secrets, finished keys."""
    handshake_secret = hkdf_extract(bytes(32), shared_secret)
    req_hs = hkdf_expand(handshake_secret, spdm_bin_concat("req hs data", 32, th1), 32)
    rsp_hs = hkdf_expand(handshake_secret, spdm_bin_concat("rsp hs data", 32, th1), 32)
    req_finished = hkdf_expand(req_hs, spdm_bin_concat("finished", 32, None), 32)
    rsp_finished = hkdf_expand(rsp_hs, spdm_bin_concat("finished", 32, None), 32)
    return handshake_secret, req_finished, rsp_finished


def derive_application_keys(handshake_secret: bytes, th2: bytes):
    """TH2 之后派生 AES-128-GCM 应用流量密钥（DSP0274 key schedule）。"""
    salt1 = hkdf_expand(handshake_secret, spdm_bin_concat("derived", 32, None), 32)
    master_secret = hkdf_extract(salt1, bytes(32))
    req_app = hkdf_expand(master_secret, spdm_bin_concat("req app data", 32, th2), 32)
    rsp_app = hkdf_expand(master_secret, spdm_bin_concat("rsp app data", 32, th2), 32)
    req_key = hkdf_expand(req_app, spdm_bin_concat("key", 16, None), 16)
    req_salt = hkdf_expand(req_app, spdm_bin_concat("iv", 12, None), 12)
    rsp_key = hkdf_expand(rsp_app, spdm_bin_concat("key", 16, None), 16)
    rsp_salt = hkdf_expand(rsp_app, spdm_bin_concat("iv", 12, None), 12)
    return req_key, req_salt, rsp_key, rsp_salt


def aead_iv(salt: bytes, seq: int) -> bytes:
    iv = bytearray(salt)
    seq_le = struct.pack("<Q", seq)
    for i in range(8):
        iv[i] ^= seq_le[i]
    return bytes(iv)


class SecuredSession:
    """DSP0277 AES-128-GCM over MCTP type 0x06。内层仍是 type 0x05 + SPDM。"""

    def __init__(self, session_id: int, req_key: bytes, req_salt: bytes, rsp_key: bytes, rsp_salt: bytes,
                 role: str = "requester"):
        self.session_id = session_id
        self.req_key = req_key
        self.req_salt = req_salt
        self.rsp_key = rsp_key
        self.rsp_salt = rsp_salt
        self.req_seq = 0
        self.rsp_seq = 0
        self.role = role

    def encode(self, spdm_msg: bytes) -> bytes:
        app = bytes([MCTP_TYPE_SPDM]) + spdm_msg
        plaintext = struct.pack("<H", len(app)) + app
        length = len(plaintext) + 16
        if self.role == "responder":
            key, salt, seq = self.rsp_key, self.rsp_salt, self.rsp_seq
            self.rsp_seq += 1
        else:
            key, salt, seq = self.req_key, self.req_salt, self.req_seq
            self.req_seq += 1
        aad = struct.pack("<IHH", self.session_id, seq & 0xFFFF, length)
        iv = aead_iv(salt, seq)
        ct_tag = AESGCM(key).encrypt(iv, plaintext, aad)
        return aad + ct_tag

    def decode(self, secured: bytes) -> bytes:
        if len(secured) < 8:
            raise ValueError(f"secured message too short: {len(secured)}")
        session_id, seq_hdr, length = struct.unpack_from("<IHH", secured, 0)
        if session_id != self.session_id:
            raise ValueError(f"session_id 0x{session_id:08x} != 0x{self.session_id:08x}")
        if len(secured) < 8 + length:
            raise ValueError(f"secured length field {length} exceeds buffer {len(secured) - 8}")
        aad = secured[:8]
        ct_tag = secured[8 : 8 + length]
        if self.role == "responder":
            key, salt, seq = self.req_key, self.req_salt, self.req_seq
            self.req_seq += 1
        else:
            key, salt, seq = self.rsp_key, self.rsp_salt, self.rsp_seq
            self.rsp_seq += 1
        iv = aead_iv(salt, seq)
        plaintext = AESGCM(key).decrypt(iv, ct_tag, aad)
        app_len = struct.unpack_from("<H", plaintext, 0)[0]
        app = plaintext[2 : 2 + app_len]
        if not app or (app[0] & 0x7F) != MCTP_TYPE_SPDM:
            raise ValueError(f"inner MCTP type is not SPDM: {app[:4].hex()}")
        return app[1:]


def verify_key_exchange_sig(device_cert, th_hash: bytes, signature: bytes) -> None:
    signed = spdm13_signing_context(b"responder-key_exchange_rsp signing") + th_hash
    digest = sha256(signed)
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    der_sig = encode_dss_signature(r, s)
    device_cert.public_key().verify(der_sig, digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))


def build_vendor_defined(payload: bytes) -> bytes:
    return struct.pack("<BBBBHBH", SPDM_VER, SPDM_VENDOR_DEFINED_REQUEST, 0, 0, 0, 0, len(payload)) + payload


def parse_vendor_payload(spdm: bytes) -> bytes:
    std_id, vid_len = struct.unpack_from("<HB", spdm, 4)
    payload_len = struct.unpack_from("<H", spdm, 7 + vid_len)[0]
    start = 7 + vid_len + 2
    return spdm[start : start + payload_len]


def get_slot0_digest(host: SpdmHost) -> bytes:
    digests = host.exchange(bytes([SPDM_VER, SPDM_GET_DIGESTS, 0, 0]), SPDM_DIGESTS, "DIGESTS")
    supported = digests[2]
    provisioned = digests[3]
    print(f"supported_slot_mask=0x{supported:02x} provisioned_slot_mask=0x{provisioned:02x}")
    digest = digests[4:36]
    print(f"slot 0 digest = {digest.hex()}")
    extra = digests[36:]
    if extra:
        key_pair_id = extra[0]
        cert_info = extra[1]
        key_usage = struct.unpack_from("<H", extra, 2)[0]
        print(
            f"multi-key bind: key_pair_id={key_pair_id} cert_info=0x{cert_info:02x} "
            f"key_usage=0x{key_usage:04x}"
        )
        if key_pair_id != 1:
            raise SystemExit(f"expected key_pair_id=1, got {key_pair_id}")
        if (cert_info & 0x7) != SPDM_CERT_MODEL_DEVICE:
            raise SystemExit(f"expected DeviceCert model, cert_info=0x{cert_info:02x}")
        if (key_usage & SPDM_KEY_USAGE_KEY_EX) == 0:
            raise SystemExit("slot 0 key_usage missing KEY_EX_USE")
        # SPDM 1.3 MessageD: first DIGESTS after VCA, used in KEY_EX TH.
        if not host.message_d:
            host.message_d = bytearray(digests)
    else:
        raise SystemExit("DIGESTS missing SPDM 1.3 multi-key fields (KeyPairID/CertInfo/KeyUsage)")
    print("GET_DIGESTS ok")
    return digest


def get_slot0_certificate(host: SpdmHost, digest: bytes):
    chain = bytearray()
    offset = 0
    while True:
        req = struct.pack("<BBBBHH", SPDM_VER, SPDM_GET_CERTIFICATE, 0, 0, offset, CERT_PORTION)
        cert_rsp = host.exchange(req, SPDM_CERTIFICATE, f"CERTIFICATE@{offset}")
        portion, remain = struct.unpack_from("<HH", cert_rsp, 4)
        chain.extend(cert_rsp[8 : 8 + portion])
        print(f"portion={portion} remain={remain} collected={len(chain)}")
        if remain == 0:
            break
        offset += portion

    if hashlib.sha256(chain).digest() != digest:
        raise SystemExit("GET_DIGESTS hash does not match GET_CERTIFICATE chain")

    length, _reserved = struct.unpack_from("<HH", chain, 0)
    if length != len(chain):
        raise SystemExit(f"SPDM cert chain length field {length} != blob {len(chain)}")
    root_hash = chain[4:36]
    der_blob = bytes(chain[36:])
    certs = split_der_certs(der_blob)
    print(f"parsed {len(certs)} DER cert(s) in chain")
    if hashlib.sha256(certs[0]).digest() != root_hash:
        raise SystemExit("root_hash does not match first cert in chain")
    return bytes(chain), certs


def get_key_pair_info(host: SpdmHost, key_pair_id: int = 1, keys: SecuredSession | None = None):
    req = bytes([SPDM_VER, SPDM_GET_KEY_PAIR_INFO, 0, 0, key_pair_id])
    if keys is None:
        rsp = host.exchange(req, SPDM_KEY_PAIR_INFO, "KEY_PAIR_INFO")
    else:
        rsp = host.exchange_secured(keys, req, SPDM_KEY_PAIR_INFO, "KEY_PAIR_INFO")
    if len(rsp) < 21:
        raise SystemExit(f"KEY_PAIR_INFO too short: {len(rsp)}")
    total, kid, caps, usage_cap, cur_usage, asym_cap, cur_asym, pk_len, assoc = struct.unpack_from(
        "<BBHHHIIHB", rsp, 4
    )
    print(
        f"key_pair_id={kid}/{total} assoc_slot=0x{assoc:02x} usage=0x{cur_usage:04x} "
        f"asym=0x{cur_asym:08x} caps=0x{caps:04x} pk_info_len={pk_len}"
    )
    if kid != key_pair_id:
        raise SystemExit(f"KEY_PAIR_INFO id {kid} != requested {key_pair_id}")
    if assoc != 0x01:
        raise SystemExit(f"expected key pair bound to slot 0, assoc=0x{assoc:02x}")
    return {
        "total": total,
        "id": kid,
        "caps": caps,
        "usage_cap": usage_cap,
        "usage": cur_usage,
        "asym_cap": asym_cap,
        "asym": cur_asym,
        "assoc": assoc,
    }


def set_key_pair_info_change(host: SpdmHost, info: dict) -> None:
    req = bytes([SPDM_VER, SPDM_SET_KEY_PAIR_INFO, SPDM_SET_KEY_PAIR_CHANGE, 0, info["id"]])
    req += struct.pack("<BHIB", 0, info["usage"], info["asym"], info["assoc"])
    print("Send SPDM SET_KEY_PAIR_INFO CHANGE (keep slot-0 binding)")
    host.exchange(req, SPDM_SET_KEY_PAIR_INFO_ACK, "SET_KEY_PAIR_INFO_ACK")
    print("SET_KEY_PAIR_INFO ok")


def run_key_exchange(host: SpdmHost, leaf, cert_chain_hash: bytes, req_session_id: int = 0x00FF) -> SecuredSession:
    # KEY_EX 作用：双方各生成一次性 P-256 密钥，算出共享密钥，再 HKDF 派生会话密钥。
    # 设备用证书私钥签 TH，证明对端真是这张证书对应的设备，而不是中间人。
    priv, req_xy = dhe_p256_keypair()
    ke_req = struct.pack(
        "<BBBBHBB", SPDM_VER, SPDM_KEY_EXCHANGE, 0, 0, req_session_id, SPDM_EVENT_ALL_POLICY, 0
    )
    ke_req += os.urandom(32)
    ke_req += req_xy
    ke_req += struct.pack("<H", 0)

    print("Send SPDM KEY_EXCHANGE (EVENT_ALL_POLICY)")
    ke_rsp = host.exchange(ke_req, SPDM_KEY_EXCHANGE_RSP, "KEY_EXCHANGE_RSP")
    if len(ke_rsp) < 40 + 64 + 2 + 64:
        raise SystemExit(f"KEY_EXCHANGE_RSP too short: {len(ke_rsp)}")

    rsp_session_id = struct.unpack_from("<H", ke_rsp, 4)[0]
    mut_auth = ke_rsp[6]
    if mut_auth != 0:
        raise SystemExit(f"unexpected mut_auth_requested=0x{mut_auth:02x}")
    rsp_xy = ke_rsp[40:104]
    opaque_len = struct.unpack_from("<H", ke_rsp, 104)[0]
    sig_off = 106 + opaque_len
    if len(ke_rsp) < sig_off + 64:
        raise SystemExit(f"KEY_EXCHANGE_RSP truncated: opaque={opaque_len} len={len(ke_rsp)}")
    signature = ke_rsp[sig_off : sig_off + 64]
    if len(ke_rsp) != sig_off + 64:
        raise SystemExit(
            f"KEY_EXCHANGE_RSP extra {len(ke_rsp) - sig_off - 64} bytes "
            "(handshake-in-the-clear should omit verify_data)"
        )

    th_prefix = bytes(host.message_a) + bytes(host.message_d) + cert_chain_hash
    th_for_sig = sha256(th_prefix + ke_req + ke_rsp[:sig_off])
    verify_key_exchange_sig(leaf, th_for_sig, signature)
    print("KEY_EXCHANGE_RSP signature verified")

    th1 = sha256(th_prefix + ke_req + ke_rsp)
    shared = dhe_p256_shared(priv, rsp_xy)
    handshake_secret, req_finished, rsp_finished = derive_handshake_keys(shared, th1)
    session_id = (rsp_session_id << 16) | req_session_id
    print(f"session_id=0x{session_id:08x} (ECDHE shared secret derived)")

    finish_hdr = bytes([SPDM_VER, SPDM_FINISH, 0x00, 0x00])
    th_finish = sha256(th_prefix + ke_req + ke_rsp + finish_hdr)
    finish_req = finish_hdr + hmac_sha256(req_finished, th_finish)

    print("Send SPDM FINISH")
    finish_rsp = host.exchange(finish_req, SPDM_FINISH_RSP, "FINISH_RSP")
    if len(finish_rsp) < 4 + 32:
        raise SystemExit(f"FINISH_RSP too short: {len(finish_rsp)}")
    th_finish_rsp = sha256(th_prefix + ke_req + ke_rsp + finish_req + finish_rsp[:4])
    expect_hmac = hmac_sha256(rsp_finished, th_finish_rsp)
    if finish_rsp[4:36] != expect_hmac:
        raise SystemExit("FINISH_RSP verify_data HMAC mismatch")

    th2 = sha256(th_prefix + ke_req + ke_rsp + finish_req + finish_rsp)
    req_key, req_salt, rsp_key, rsp_salt = derive_application_keys(handshake_secret, th2)
    print(f"FINISH_RSP HMAC verified, session 0x{session_id:08x} established (handshake in the clear)")
    print("KEY_EX ok")
    return SecuredSession(session_id, req_key, req_salt, rsp_key, rsp_salt)


def run_event_subscribe(host: SpdmHost, keys: SecuredSession) -> None:
    print("Send SPDM GET_SUPPORTED_EVENT_TYPES")
    req = bytes([SPDM_VER, SPDM_GET_SUPPORTED_EVENT_TYPES, 0, 0])
    rsp = host.exchange_secured(keys, req, SPDM_SUPPORTED_EVENT_TYPES, "SUPPORTED_EVENT_TYPES")
    if len(rsp) < 8:
        raise SystemExit(f"SUPPORTED_EVENT_TYPES too short: {len(rsp)}")
    group_count = rsp[2]
    list_len = struct.unpack_from("<I", rsp, 4)[0]
    event_list = rsp[8 : 8 + list_len]
    print(f"event_group_count={group_count} list_len={list_len} list={event_list.hex()}")
    if group_count == 0 or list_len == 0:
        raise SystemExit("responder advertised EVENT_CAP but empty event list")

    print("Send SPDM SUBSCRIBE_EVENT_TYPES (list)")
    sub = struct.pack("<BBBBI", SPDM_VER, SPDM_SUBSCRIBE_EVENT_TYPES, group_count, 0, list_len)
    sub += event_list
    host.exchange_secured(keys, sub, SPDM_SUBSCRIBE_EVENT_TYPES_ACK, "SUBSCRIBE_EVENT_TYPES_ACK")
    print("SUBSCRIBE_EVENT_TYPES ok")


def unsubscribe_events(host: SpdmHost, keys: SecuredSession) -> None:
    print("Send SPDM SUBSCRIBE_EVENT_TYPES (unsubscribe all)")
    req = bytes([SPDM_VER, SPDM_SUBSCRIBE_EVENT_TYPES, 0, 0])
    host.exchange_secured(keys, req, SPDM_SUBSCRIBE_EVENT_TYPES_ACK, "UNSUBSCRIBE_ACK")
    print("unsubscribe ok")


def run_session_ping(host: SpdmHost, keys: SecuredSession, rounds: int = 10, interval_s: float = 1.0) -> None:
    ping = b"1111"
    pong = b"2222"
    for i in range(rounds):
        print(f"Send session VENDOR_DEFINED ping {i + 1}/{rounds}: {ping!r}")
        rsp = host.exchange_secured(
            keys, build_vendor_defined(ping), SPDM_VENDOR_DEFINED_RESPONSE, f"VENDOR_PONG@{i + 1}"
        )
        payload = parse_vendor_payload(rsp)
        if payload != pong:
            raise SystemExit(f"session pong mismatch: {payload!r}")
        print(f"session pong {i + 1}/{rounds}: {payload!r}")
        time.sleep(interval_s)


def end_session(host: SpdmHost, keys: SecuredSession) -> None:
    print("Send SPDM END_SESSION")
    req = bytes([SPDM_VER, SPDM_END_SESSION, 0x00, 0x00])
    host.exchange_secured(keys, req, SPDM_END_SESSION_ACK, "END_SESSION_ACK")
    print("END_SESSION ok (negotiated state preserved)")


def run_spdm_handshake(usb_dev) -> None:
    ca_cert = load_ca_cert()
    host = SpdmHost(usb_dev)

    print("Send SPDM GET_VERSION")
    ver = host.exchange(bytes([0x10, SPDM_GET_VERSION, 0x00, 0x00]), SPDM_VERSION, "VERSION", record_a=True)
    if len(ver) >= 6:
        count = ver[5]
        versions = [struct.unpack_from("<H", ver, 6 + i * 2)[0] for i in range(count)]
        print("VERSION entries: " + ", ".join(f"0x{v:04x}" for v in versions))
        if not any((v >> 8) == 0x13 for v in versions):
            raise SystemExit("Responder VERSION did not include SPDM 1.3")
    print("GET_VERSION ok")

    print("Send SPDM GET_CAPABILITIES (1.3)")
    data_transfer_size = 2048
    max_spdm_msg_size = 2048
    # Requester flags describe *this* PC, not the device.
    # MULTI_KEY_CAP on the request requires CERT_CAP (DSP0274 / libspdm).
    # This tester does not do mutual auth, so do not advertise MULTI_KEY_CAP.
    # Responder multi-key is selected later via NEGOTIATE_ALGORITHMS
    # OtherParams MULTI_KEY_CONN (ResponderMultiKeyConn).
    req_flags = (
        SPDM_REQ_FLAGS_ENCRYPT
        | SPDM_REQ_FLAGS_MAC
        | SPDM_REQ_FLAGS_KEY_EX
        | SPDM_REQ_FLAGS_HANDSHAKE_IN_THE_CLEAR
    )
    get_caps = struct.pack(
        "<BBBBBBHIII",
        SPDM_VER,
        SPDM_GET_CAPABILITIES,
        0,
        0,
        0,
        0,
        0,
        req_flags,
        data_transfer_size,
        max_spdm_msg_size,
    )
    caps = host.exchange(get_caps, SPDM_CAPABILITIES, "CAPABILITIES", record_a=True)
    flags = struct.unpack_from("<I", caps, 8)[0]
    print(f"capability flags = 0x{flags:08x}")
    if (flags & SPDM_RSP_FLAGS_CERT) == 0 or (flags & SPDM_RSP_FLAGS_CHAL) == 0:
        raise SystemExit("Responder did not advertise CERT_CAP and CHAL_CAP")
    if (flags & SPDM_RSP_FLAGS_KEY_EX) == 0:
        raise SystemExit("Responder did not advertise KEY_EX_CAP; rebuild firmware")
    if (flags & SPDM_RSP_FLAGS_EVENT) == 0:
        raise SystemExit("Responder did not advertise EVENT_CAP")
    if (flags & (SPDM_RSP_FLAGS_MULTI_KEY_NEG | SPDM_RSP_FLAGS_MULTI_KEY_ONLY)) == 0:
        raise SystemExit("Responder did not advertise MULTI_KEY_CAP")
    if (flags & SPDM_RSP_FLAGS_GET_KEY_PAIR) == 0 or (flags & SPDM_RSP_FLAGS_SET_KEY_PAIR) == 0:
        raise SystemExit("Responder did not advertise GET/SET_KEY_PAIR_INFO_CAP")
    print("GET_CAPABILITIES ok")

    print("Send SPDM NEGOTIATE_ALGORITHMS")
    algs = host.exchange(build_negotiate_algorithms(), SPDM_ALGORITHMS, "ALGORITHMS", record_a=True)
    base_asym = struct.unpack_from("<I", algs, 12)[0]
    base_hash = struct.unpack_from("<I", algs, 16)[0]
    other = algs[7]
    print(f"selected asym=0x{base_asym:08x} hash=0x{base_hash:08x} other_params=0x{other:02x}")
    print("NEGOTIATE_ALGORITHMS ok")

    print("Send SPDM GET_DIGESTS")
    digest = get_slot0_digest(host)

    print("Send SPDM GET_CERTIFICATE")
    chain, certs = get_slot0_certificate(host, digest)
    leaf = x509.load_der_x509_certificate(certs[-1])
    ca_cert.public_key().verify(
        leaf.signature,
        leaf.tbs_certificate_bytes,
        ec.ECDSA(leaf.signature_hash_algorithm),
    )
    print(f"device cert CN={leaf.subject.rfc4514_string()} verified by CA")
    print("GET_CERTIFICATE ok")

    print("Send SPDM CHALLENGE")
    nonce = os.urandom(32)
    req_ctx = os.urandom(SPDM_REQ_CONTEXT_SIZE)
    chal_req = bytes([SPDM_VER, SPDM_CHALLENGE, 0x00, 0x00]) + nonce + req_ctx
    chal = host.exchange(chal_req, SPDM_CHALLENGE_AUTH, "CHALLENGE_AUTH")
    sig = chal[-64:]
    unsigned = chal[:-64]
    if unsigned[-(SPDM_REQ_CONTEXT_SIZE):] != req_ctx:
        raise SystemExit("CHALLENGE_AUTH requester_context mismatch")
    m1m2 = hashlib.sha256(host.transcript[:-64]).digest()
    verify_challenge_auth(leaf, m1m2, sig)
    print(f"CHALLENGE_AUTH signature verified ({len(unsigned)} + 64 bytes)")
    print("CHALLENGE ok")

    print("Send SPDM GET_KEY_PAIR_INFO")
    info = get_key_pair_info(host, 1)
    set_key_pair_info_change(host, info)
    info2 = get_key_pair_info(host, 1)
    if info2["assoc"] != info["assoc"]:
        raise SystemExit("SET_KEY_PAIR_INFO did not keep slot binding")

    keys = run_key_exchange(host, leaf, digest, req_session_id=0x00FF)
    run_event_subscribe(host, keys)
    run_session_ping(host, keys, rounds=10, interval_s=1.0)
    unsubscribe_events(host, keys)
    end_session(host, keys)

    print("Re-establish session with cached cert chain (digest must match)")
    digest2 = get_slot0_digest(host)
    if digest2 != digest:
        raise SystemExit("cached cert digest changed; GET_CERTIFICATE would be required")
    print("cached slot 0 digest unchanged, skip GET_CERTIFICATE and CHALLENGE")

    keys2 = run_key_exchange(host, leaf, digest2, req_session_id=0x11FF)
    run_session_ping(host, keys2, rounds=1, interval_s=0.0)
    end_session(host, keys2)

    print("SPDM 1.3 CERT/CHAL/KEY_PAIR/EVENT session ping-pong and cached reconnect succeeded.")


def load_device_private_key():
    if not os.path.isfile(DEVICE_KEY_PEM):
        raise SystemExit(f"Missing {DEVICE_KEY_PEM}")
    with open(DEVICE_KEY_PEM, "rb") as f:
        return load_pem_private_key(f.read(), password=None)


def _chain_from_generated_header() -> bytes | None:
    """certs_generated.h 里已经有完整 SPDM chain blob，缺 .bin 时从这里还原。"""
    header = os.path.join(HERE, "certs", "certs_generated.h")
    if not os.path.isfile(header):
        return None
    text = open(header, encoding="utf-8").read()
    match = re.search(r"g_spdm_cert_chain\[\d+\]\s*=\s*\{([^}]+)\}", text)
    if not match:
        return None
    hexes = re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))
    if not hexes:
        return None
    return bytes(int(h, 16) for h in hexes)


def load_cert_chain_blob() -> bytes:
    if os.path.isfile(CERT_CHAIN_BIN):
        with open(CERT_CHAIN_BIN, "rb") as f:
            return f.read()
    ca_der_path = os.path.join(HERE, "certs", "ca.cert.der")
    dev_der_path = os.path.join(HERE, "certs", "device.cert.der")
    if os.path.isfile(ca_der_path) and os.path.isfile(dev_der_path):
        with open(ca_der_path, "rb") as f:
            ca_der = f.read()
        with open(dev_der_path, "rb") as f:
            device_der = f.read()
        root_hash = sha256(ca_der)
        body = root_hash + ca_der + device_der
        blob = struct.pack("<HH", 4 + len(body), 0) + body
    else:
        blob = _chain_from_generated_header()
        if blob is None:
            raise SystemExit(
                f"Missing {CERT_CHAIN_BIN} (and no certs_generated.h fallback). "
                "Run: python certs/gen_certs.py"
            )
    try:
        with open(CERT_CHAIN_BIN, "wb") as f:
            f.write(blob)
        print(f"wrote {CERT_CHAIN_BIN} ({len(blob)} bytes)")
    except OSError as err:
        print(f"warning: could not cache {CERT_CHAIN_BIN}: {err}")
    return blob


def ensure_host_responder_certs() -> None:
    """USB claim 之前先确认证书材料在，避免占着设备然后立刻退出。"""
    load_device_private_key()
    load_cert_chain_blob()


def sign_spdm_hash(priv, op_context: bytes, th_hash: bytes) -> bytes:
    signed = spdm13_signing_context(op_context) + th_hash
    digest = sha256(signed)
    der = priv.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = decode_dss_signature(der)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def spdm_error(code: int, param2: int = 0) -> bytes:
    return bytes([SPDM_VER, SPDM_ERROR, code, param2])


class SpdmPythonResponder:
    """Host-side SPDM 1.3 responder for the device-as-requester sample."""

    RSP_FLAGS = (
        SPDM_RSP_FLAGS_CERT
        | SPDM_RSP_FLAGS_CHAL
        | SPDM_RSP_FLAGS_KEY_EX
        | SPDM_REQ_FLAGS_ENCRYPT
        | SPDM_REQ_FLAGS_MAC
        | SPDM_REQ_FLAGS_HANDSHAKE_IN_THE_CLEAR
        | SPDM_RSP_FLAGS_EVENT
        | SPDM_RSP_FLAGS_MULTI_KEY_NEG
        | SPDM_RSP_FLAGS_GET_KEY_PAIR
        | SPDM_RSP_FLAGS_SET_KEY_PAIR
    )

    def __init__(self, usb_dev):
        self.host = SpdmHost(usb_dev)
        self.priv = load_device_private_key()
        self.chain = load_cert_chain_blob()
        self.digest = sha256(self.chain)
        der_blob = self.chain[36:]
        certs = split_der_certs(der_blob)
        self.leaf = x509.load_der_x509_certificate(certs[-1])
        self.message_a = bytearray()
        self.message_d = bytearray()
        self.transcript = bytearray()
        self.keys: SecuredSession | None = None
        self.handshake_secret = None
        self.th2_parts = None
        self.ke_state = None
        self.assoc = 0x01
        self.usage = SPDM_KEY_USAGE_KEY_EX | SPDM_KEY_USAGE_CHALLENGE
        self.asym = SPDM_KEY_PAIR_ASYM_ECC256
        self.multi_key = False
        self.req_flags = 0
        self.event_group = struct.pack(
            "<BBHHIHHHH",
            0,
            0,
            2,
            1,
            0,
            1,
            0,
            4,
            0,
        )

    def reply(self, spdm: bytes, secured: bool = False) -> None:
        tag = self.host.last_tag
        if secured:
            if self.keys is None:
                raise SystemExit("secured reply without session")
            inner = bytes([MCTP_TYPE_SECURED]) + self.keys.encode(spdm)
            body = build_mctp(inner, msg_tag=tag, tag_owner=False)
        else:
            body = build_mctp_spdm(spdm, msg_tag=tag, tag_owner=False)
        self.host.usb.send_data(build_frame(body))

    def recv_request(self) -> tuple[bytes, bool]:
        msg = self.host.recv_mctp_payload()
        mtype = msg[0] & 0x7F
        if mtype == MCTP_TYPE_SECURED:
            if self.keys is None:
                raise SystemExit("secured request before session")
            return self.keys.decode(msg[1:]), True
        if mtype != MCTP_TYPE_SPDM:
            raise SystemExit(f"unexpected MCTP type 0x{mtype:02x}")
        return msg[1:], False

    def handle_version(self, req: bytes) -> bytes:
        rsp = bytes([0x10, SPDM_VERSION, 0, 0, 0, 1]) + struct.pack("<H", 0x1300)
        self.message_a.extend(req)
        self.message_a.extend(rsp)
        self.transcript.extend(req)
        self.transcript.extend(rsp)
        return rsp

    def handle_caps(self, req: bytes) -> bytes:
        if len(req) >= 12:
            self.req_flags = struct.unpack_from("<I", req, 8)[0]
        rsp = struct.pack(
            "<BBBBBBHIII",
            SPDM_VER,
            SPDM_CAPABILITIES,
            0,
            0,
            0,
            0,
            0,
            self.RSP_FLAGS,
            2048,
            2048,
        )
        self.message_a.extend(req)
        self.message_a.extend(rsp)
        self.transcript.extend(req)
        self.transcript.extend(rsp)
        return rsp

    def handle_algs(self, req: bytes) -> bytes:
        other = SPDM_OPAQUE_DATA_FORMAT_1
        # Request bit4 = ResponderMultiKeyConn (device wants our DIGESTS extra fields).
        # Response bit4 = RequesterMultiKeyConnSel (only if the requester has MULTI_KEY_CAP).
        if len(req) > 7 and (req[7] & SPDM_MULTI_KEY_CONN):
            self.multi_key = True
        if self.req_flags & SPDM_REQ_FLAGS_MULTI_KEY_NEG:
            other |= SPDM_MULTI_KEY_CONN
        tables = b""
        tables += struct.pack("<BBH", 2, 0x20, SPDM_DHE_SECP_256_R1)
        tables += struct.pack("<BBH", 3, 0x20, SPDM_AEAD_AES_128_GCM)
        tables += struct.pack("<BBH", 5, 0x20, SPDM_KEY_SCHEDULE_SPDM)
        length = 36 + len(tables)
        header = struct.pack("<BBBB", SPDM_VER, SPDM_ALGORITHMS, 3, 0)
        body = struct.pack(
            "<HBBIII",
            length,
            0,
            other,
            0,
            SPDM_BASE_ASYM_ECDSA_P256,
            SPDM_BASE_HASH_SHA256,
        )
        body += bytes(11)  # reserved2
        body += bytes([0, 0, 0])  # mel_spec, ext_asym, ext_hash
        body += struct.pack("<H", 0)
        rsp = header + body + tables
        self.message_a.extend(req)
        self.message_a.extend(rsp)
        self.transcript.extend(req)
        self.transcript.extend(rsp)
        return rsp

    def handle_digests(self, req: bytes) -> bytes:
        extra = b""
        if self.multi_key:
            extra = struct.pack("<BBH", 1, SPDM_CERT_MODEL_DEVICE, self.usage)
        rsp = bytes([SPDM_VER, SPDM_DIGESTS, 0x01, 0x01]) + self.digest + extra
        if not self.message_d:
            self.message_d = bytearray(rsp)
        self.transcript.extend(req)
        self.transcript.extend(rsp)
        return rsp

    def handle_certificate(self, req: bytes) -> bytes:
        offset, length = struct.unpack_from("<HH", req, 4)
        length = min(length, CERT_PORTION)
        portion = self.chain[offset : offset + length]
        remain = max(0, len(self.chain) - offset - len(portion))
        # SPDM 1.3 + MULTI_KEY_CONN: param2 必须是 CertModel。
        # 填 0 (NONE) 且 PortionLength!=0 时，libspdm 会回 INVALID_MSG_FIELD (0x80010005)。
        cert_info = SPDM_CERT_MODEL_DEVICE if self.multi_key else 0
        rsp = struct.pack(
            "<BBBBHH", SPDM_VER, SPDM_CERTIFICATE, 0, cert_info, len(portion), remain
        ) + portion
        self.transcript.extend(req)
        self.transcript.extend(rsp)
        return rsp

    def handle_challenge(self, req: bytes) -> bytes:
        req_ctx = req[36:44] if len(req) >= 44 else b"\x00" * 8
        header = bytes([SPDM_VER, SPDM_CHALLENGE_AUTH, 0x00, 0x01])
        nonce = os.urandom(32)
        unsigned = header + self.digest + nonce + struct.pack("<H", 0) + req_ctx
        self.transcript.extend(req)
        th = sha256(bytes(self.transcript) + unsigned)
        sig = sign_spdm_hash(self.priv, b"responder-challenge_auth signing", th)
        rsp = unsigned + sig
        self.transcript.extend(rsp)
        return rsp

    def handle_key_pair_info(self, req: bytes) -> bytes:
        kid = req[4] if len(req) > 4 else 1
        pk = bytes(
            [
                0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01,
                0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07,
            ]
        )
        hdr = bytes([SPDM_VER, SPDM_KEY_PAIR_INFO, 0, 0])
        hdr += struct.pack(
            "<BBHHHIIHB",
            1,
            kid,
            0x000C,
            self.usage,
            self.usage,
            SPDM_KEY_PAIR_ASYM_ECC256,
            self.asym,
            len(pk),
            self.assoc,
        )
        return hdr + pk

    def handle_set_key_pair(self, req: bytes) -> bytes:
        if len(req) >= 14 and req[2] == SPDM_SET_KEY_PAIR_CHANGE:
            _rsvd, usage, asym, assoc = struct.unpack_from("<BHIB", req, 5)
            if usage:
                self.usage = usage
            if asym:
                self.asym = asym
            self.assoc = assoc
            print(f"SET_KEY_PAIR_INFO CHANGE assoc=0x{self.assoc:02x} usage=0x{self.usage:04x}")
        return bytes([SPDM_VER, SPDM_SET_KEY_PAIR_INFO_ACK, 0, 0])

    def handle_key_exchange(self, req: bytes) -> bytes:
        req_session_id = struct.unpack_from("<H", req, 4)[0]
        req_xy = req[40:104]
        priv, rsp_xy = dhe_p256_keypair()
        rsp_session_id = 0xFF00
        header = struct.pack("<BBBBHBB", SPDM_VER, SPDM_KEY_EXCHANGE_RSP, 0, 0, rsp_session_id, 0, 0)
        nonce = os.urandom(32)
        unsigned = header + nonce + rsp_xy + struct.pack("<H", 0)
        th_prefix = bytes(self.message_a) + bytes(self.message_d) + self.digest
        th_sig = sha256(th_prefix + req + unsigned)
        sig = sign_spdm_hash(self.priv, b"responder-key_exchange_rsp signing", th_sig)
        rsp = unsigned + sig
        th1 = sha256(th_prefix + req + rsp)
        shared = dhe_p256_shared(priv, req_xy)
        handshake_secret, req_finished, rsp_finished = derive_handshake_keys(shared, th1)
        session_id = (rsp_session_id << 16) | req_session_id
        self.ke_state = {
            "req": req,
            "rsp": rsp,
            "handshake_secret": handshake_secret,
            "req_finished": req_finished,
            "rsp_finished": rsp_finished,
            "session_id": session_id,
        }
        print(f"KEY_EXCHANGE session_id=0x{session_id:08x}")
        return rsp

    def handle_finish(self, req: bytes) -> bytes:
        st = self.ke_state
        if st is None:
            return spdm_error(0x01)
        finish_hdr = req[:4]
        th_prefix = bytes(self.message_a) + bytes(self.message_d) + self.digest
        th_finish = sha256(th_prefix + st["req"] + st["rsp"] + finish_hdr)
        expect = hmac_sha256(st["req_finished"], th_finish)
        if req[4:36] != expect:
            return spdm_error(0x01)
        rsp_hdr = bytes([SPDM_VER, SPDM_FINISH_RSP, 0, 0])
        th_rsp = sha256(th_prefix + st["req"] + st["rsp"] + req + rsp_hdr)
        verify = hmac_sha256(st["rsp_finished"], th_rsp)
        rsp = rsp_hdr + verify
        th2 = sha256(th_prefix + st["req"] + st["rsp"] + req + rsp)
        req_key, req_salt, rsp_key, rsp_salt = derive_application_keys(st["handshake_secret"], th2)
        self.keys = SecuredSession(st["session_id"], req_key, req_salt, rsp_key, rsp_salt,
                                   role="responder")
        print("FINISH ok, session established")
        return rsp

    def handle_vendor(self, req: bytes) -> bytes:
        payload = parse_vendor_payload(req)
        pong = b"2222" if payload == b"1111" else b""
        return struct.pack("<BBBBHBH", SPDM_VER, SPDM_VENDOR_DEFINED_RESPONSE, 0, 0, 0, 0, len(pong)) + pong

    def handle_events(self, req: bytes) -> bytes:
        code = req[1]
        if code == SPDM_GET_SUPPORTED_EVENT_TYPES:
            lst = self.event_group
            return struct.pack("<BBBBI", SPDM_VER, SPDM_SUPPORTED_EVENT_TYPES, 1, 0, len(lst)) + lst
        if code == SPDM_SUBSCRIBE_EVENT_TYPES:
            print(f"SUBSCRIBE_EVENT_TYPES param1={req[2]} len={len(req)}")
            return bytes([SPDM_VER, SPDM_SUBSCRIBE_EVENT_TYPES_ACK, 0, 0])
        return spdm_error(0x05, code)

    def dispatch(self, req: bytes) -> bytes:
        if len(req) < 2:
            return spdm_error(0x01)
        code = req[1]
        handlers = {
            SPDM_GET_VERSION: self.handle_version,
            SPDM_GET_CAPABILITIES: self.handle_caps,
            SPDM_NEGOTIATE_ALGORITHMS: self.handle_algs,
            SPDM_GET_DIGESTS: self.handle_digests,
            SPDM_GET_CERTIFICATE: self.handle_certificate,
            SPDM_CHALLENGE: self.handle_challenge,
            SPDM_GET_KEY_PAIR_INFO: self.handle_key_pair_info,
            SPDM_SET_KEY_PAIR_INFO: self.handle_set_key_pair,
            SPDM_KEY_EXCHANGE: self.handle_key_exchange,
            SPDM_FINISH: self.handle_finish,
            SPDM_VENDOR_DEFINED_REQUEST: self.handle_vendor,
            SPDM_GET_SUPPORTED_EVENT_TYPES: self.handle_events,
            SPDM_SUBSCRIBE_EVENT_TYPES: self.handle_events,
            SPDM_END_SESSION: lambda _r: bytes([SPDM_VER, SPDM_END_SESSION_ACK, 0, 0]),
        }
        fn = handlers.get(code)
        if fn is None:
            print(f"unsupported request 0x{code:02x}")
            return spdm_error(0x05, code)
        return fn(req)

    def run(self) -> None:
        print("Host SPDM 1.3 responder waiting for device requester...")
        first = True
        while True:
            if first:
                deadline = time.time() + 60.0
                while True:
                    try:
                        req, secured = self.recv_request()
                        break
                    except usb.core.USBTimeoutError:
                        if time.time() >= deadline:
                            raise SystemExit("timeout waiting for device GET_VERSION")
                        print("waiting for device requester...")
                first = False
            else:
                try:
                    req, secured = self.recv_request()
                except usb.core.USBTimeoutError:
                    raise SystemExit("timeout waiting for next SPDM request from device")
            hexdump(f"REQ{'sec' if secured else ''}", req)
            rsp = self.dispatch(req)
            hexdump("RSP", rsp)
            self.reply(rsp, secured=secured)
            if req[1] == SPDM_END_SESSION:
                print("device END_SESSION; host responder idle")
                self.keys = None
                self.ke_state = None
                first = True


def run_spdm_responder(usb_dev) -> None:
    SpdmPythonResponder(usb_dev).run()



class HostUSBDevice(MctpUsbTransport):
    def __init__(self, vid, pid):
        super().__init__()
        self.vid = vid
        self.pid = pid
        self._detached = False

    def connect(self):
        self.device = usb.core.find(idVendor=self.vid, idProduct=self.pid)
        if self.device is None:
            raise ValueError(
                f"Device not found (VID=0x{self.vid:04X} PID=0x{self.pid:04X}). "
                "Check lsusb. If the board enumerated, try another USB port / cable."
            )
        cfg = get_active_config(self.device)
        self.select_bulk_interface(cfg)
        intf_num = self.interface.bInterfaceNumber
        with suppress(NotImplementedError, usb.core.USBError):
            if self.device.is_kernel_driver_active(intf_num):
                self.device.detach_kernel_driver(intf_num)
                self._detached = True
        try:
            usb.util.claim_interface(self.device, intf_num)
        except usb.core.USBError as err:
            raise PermissionError(
                "Cannot claim USB interface. Run with sudo, or add a udev rule:\n"
                '  SUBSYSTEM=="usb", ATTR{idVendor}=="2fe3", '
                'ATTR{idProduct}=="0001", MODE="0666"'
            ) from err
        self.arm_bulk_pipes()
        print(
            f"Connected (Linux) to {self.vid:04x}:{self.pid:04x} "
            f"(IN_MPS={self.in_mps}, OUT_MPS={self.out_mps})"
        )

    def disconnect(self):
        if self.device is None or self.interface is None:
            return
        intf_num = self.interface.bInterfaceNumber
        with suppress(usb.core.USBError):
            usb.util.release_interface(self.device, intf_num)
        with suppress(usb.core.USBError):
            usb.util.dispose_resources(self.device)
        if self._detached:
            with suppress(usb.core.USBError):
                self.device.attach_kernel_driver(intf_num)
        self.device = None
        self.interface = None
        self.endpoint_in = None
        self.endpoint_out = None
        self._detached = False
        self._rx_buf = bytearray()

def main() -> None:
    usb_dev = HostUSBDevice(VID, PID)
    try:
        usb_dev.connect()
        run_spdm_handshake(usb_dev)
    finally:
        usb_dev.disconnect()


if __name__ == "__main__":
    main()
