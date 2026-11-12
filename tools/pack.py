#!/usr/bin/env python3
"""Turn the linked payload into an app file the stable firmware installs.

With --seed, the bytes outside this firmware's code and identity record
come from the installed app instead of zeros. The installer then transfers
only the blocks that differ. Use that file only for installs on your own
module; release the zero-padded file."""
import argparse
import hashlib
import struct
import zlib

LOAD_ADDRESS = 0x80000000
PAYLOAD_BYTES = 0x9BAC8
IDENTITY_OFFSET = 0x6E4F8
IDENTITY_BYTES = 24 + len(b"ASSIMILXR")  # the record, then this firmware's marker (remote.c)
BUILD_ID_OFFSET = IDENTITY_OFFSET + 12
CRC_OFFSET = IDENTITY_OFFSET + 20
HEADER_BYTES = 8


def finish(payload):
    struct.pack_into("<I", payload, CRC_OFFSET, 0)
    struct.pack_into("<I", payload, CRC_OFFSET, zlib.crc32(payload))
    return struct.pack("<II", HEADER_BYTES + PAYLOAD_BYTES, LOAD_ADDRESS) + payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("payload")
    parser.add_argument("app")
    parser.add_argument("--code-bytes", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--seed", help="installed app to fill unused bytes from")
    parser.add_argument("--seed-output")
    args = parser.parse_args()
    payload = bytearray(open(args.payload, "rb").read())
    if len(payload) > PAYLOAD_BYTES:
        raise SystemExit(f"payload is {len(payload)} bytes; the limit is {PAYLOAD_BYTES}")
    payload.extend(bytes(PAYLOAD_BYTES - len(payload)))
    payload[BUILD_ID_OFFSET:CRC_OFFSET + 4] = bytes(12)
    build_id = hashlib.sha256(payload).digest()[:8]
    payload[BUILD_ID_OFFSET:BUILD_ID_OFFSET + 8] = build_id
    open(args.app, "wb").write(finish(payload))
    print(f"{args.app}: build {build_id.hex()}")
    if args.seed:
        seeded = bytearray(open(args.seed, "rb").read()[HEADER_BYTES:])
        if len(seeded) != PAYLOAD_BYTES:
            raise SystemExit("seed app has the wrong size")
        seeded[:args.code_bytes] = payload[:args.code_bytes]
        seeded[IDENTITY_OFFSET:IDENTITY_OFFSET + IDENTITY_BYTES] = \
            payload[IDENTITY_OFFSET:IDENTITY_OFFSET + IDENTITY_BYTES]
        open(args.seed_output, "wb").write(finish(seeded))
        print(f"{args.seed_output}: build {build_id.hex()}, seeded from {args.seed}")


if __name__ == "__main__":
    main()
