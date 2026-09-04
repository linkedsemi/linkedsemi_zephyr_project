#!/usr/bin/env python3
# Copyright 2026
# SPDX-License-Identifier: Apache-2.0
"""Generate ECDSA P-256 CA + device certs for the SPDM USB samples.

Run from this certs/ directory:

  python gen_certs.py

Outputs (this directory):
  ca.cert.pem          Host tester verifies GET_CERTIFICATE with this CA
  ca.key.pem           Only for re-issuing device certs
  device.key.pem       Device private key (also baked into firmware header)
  certs_generated.h    Firmware include; rebuild after regenerating

Also copies certs_generated.h to the sibling sample (spdm_response /
spdm_requester) so both sides keep the same CA.
"""

from __future__ import annotations

import datetime
import hashlib
import os
import shutil
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

HERE = os.path.dirname(os.path.abspath(__file__))
VALIDITY_DAYS = 3650


def _firmware_header_candidates() -> list[str]:
    mctp = os.path.dirname(os.path.dirname(HERE))
    dests = [os.environ.get("SPDM_FIRMWARE_CERT_HEADER", "")]
    for name in ("spdm_response", "spdm_requester"):
        dests.append(os.path.join(mctp, name, "certs", "certs_generated.h"))
    return dests


def _name(common_name: str) -> x509.Name:
    return x509.Name(
        [
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "libspdm-sample"),
            x509.NameAttribute(NameOID.COMMON_NAME, common_name),
        ]
    )


def _c_array(name: str, data: bytes, as_string: bool = False) -> str:
    if as_string:
        text = data.decode("ascii")
        if not text.endswith("\n"):
            text += "\n"
        lines = ['    "' + line + '\\n"' for line in text.splitlines()]
        return f"static const char {name}[] =\n" + "\n".join(lines) + ";\n"

    parts = [f"0x{b:02x}" for b in data]
    lines = []
    for i in range(0, len(parts), 12):
        lines.append("    " + ", ".join(parts[i : i + 12]) + ",")
    body = "\n".join(lines)
    return (
        f"static const uint8_t {name}[{len(data)}] = {{\n{body}\n}};\n"
        f"static const size_t {name}_len = sizeof({name});\n"
    )


def _sync_firmware_header(header_path: str) -> None:
    seen = set()
    src = os.path.abspath(header_path)
    seen.add(src)
    for dest in _firmware_header_candidates():
        if not dest:
            continue
        dest = os.path.abspath(dest)
        if dest in seen:
            continue
        seen.add(dest)
        parent = os.path.dirname(dest)
        if not os.path.isdir(parent):
            continue
        shutil.copy2(header_path, dest)
        print(f"synced firmware header -> {dest}")


def main() -> int:
    now = datetime.datetime.now(datetime.timezone.utc)
    not_before = now - datetime.timedelta(minutes=5)
    not_after = now + datetime.timedelta(days=VALIDITY_DAYS)

    ca_key = ec.generate_private_key(ec.SECP256R1())
    device_key = ec.generate_private_key(ec.SECP256R1())

    ca_cert = (
        x509.CertificateBuilder()
        .subject_name(_name("libspdm-sample-ca"))
        .issuer_name(_name("libspdm-sample-ca"))
        .public_key(ca_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(not_before)
        .not_valid_after(not_after)
        .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=False,
                key_encipherment=False,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=True,
                crl_sign=True,
                encipher_only=False,
                decipher_only=False,
            ),
            critical=True,
        )
        .add_extension(
            x509.SubjectKeyIdentifier.from_public_key(ca_key.public_key()),
            critical=False,
        )
        .sign(ca_key, hashes.SHA256())
    )

    device_cert = (
        x509.CertificateBuilder()
        .subject_name(_name("libspdm-sample-device"))
        .issuer_name(ca_cert.subject)
        .public_key(device_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(not_before)
        .not_valid_after(not_after)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=False,
                key_encipherment=False,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=False,
                crl_sign=False,
                encipher_only=False,
                decipher_only=False,
            ),
            critical=True,
        )
        .add_extension(
            x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()),
            critical=False,
        )
        .sign(ca_key, hashes.SHA256())
    )

    ca_der = ca_cert.public_bytes(serialization.Encoding.DER)
    device_der = device_cert.public_bytes(serialization.Encoding.DER)
    ca_pem = ca_cert.public_bytes(serialization.Encoding.PEM)
    ca_key_pem = ca_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.TraditionalOpenSSL,
        encryption_algorithm=serialization.NoEncryption(),
    )
    device_key_pem = device_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.TraditionalOpenSSL,
        encryption_algorithm=serialization.NoEncryption(),
    )

    root_hash = hashlib.sha256(ca_der).digest()
    chain_body = root_hash + ca_der + device_der
    chain = struct.pack("<HH", 4 + len(chain_body), 0) + chain_body

    def write_bytes(name: str, data: bytes) -> None:
        path = os.path.join(HERE, name)
        with open(path, "wb") as f:
            f.write(data)
        print(f"wrote {path} ({len(data)} bytes)")

    write_bytes("ca.cert.der", ca_der)
    write_bytes("ca.cert.pem", ca_pem)
    write_bytes("ca.key.pem", ca_key_pem)
    write_bytes("device.cert.der", device_der)
    write_bytes("device.key.pem", device_key_pem)
    write_bytes("spdm_cert_chain.bin", chain)

    header = os.path.join(HERE, "certs_generated.h")
    with open(header, "w", encoding="utf-8") as f:
        f.write(
            "/* Generated by certs/gen_certs.py. Do not edit by hand. */\n"
            "#ifndef SPDM_CERTS_GENERATED_H\n"
            "#define SPDM_CERTS_GENERATED_H\n\n"
            "#include <stddef.h>\n"
            "#include <stdint.h>\n\n"
        )
        f.write(_c_array("g_spdm_cert_chain", chain))
        f.write("\n")
        f.write(_c_array("g_spdm_device_key_pem", device_key_pem, as_string=True))
        f.write("\n#endif /* SPDM_CERTS_GENERATED_H */\n")
    print(f"wrote {header}")
    _sync_firmware_header(header)
    print(f"SPDM cert chain length: {len(chain)} bytes")
    print("Rebuild firmware after regenerating, then re-run the host tester.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
