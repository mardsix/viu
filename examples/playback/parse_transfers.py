#!/usr/bin/env python3

"""
Script to parse USB transfer records and control setup records
from JSONL output files.

Transfer records:
  - timestamp_ms: Timestamp in milliseconds since epoch
  - endpoint: Endpoint address (hex string)
  - size: Number of bytes transferred
  - data: Byte offset of transfer payload in companion .bin file

Control setup records:
  - setup: uint64_t representation of libusb_control_setup
  - data_size: Size of control setup data
  - data: Byte offset of control data in companion .bin file
"""


import json
import struct
import sys
from datetime import datetime
from pathlib import Path
from typing import Generator


def parse_transfer_file(file_path: str) -> Generator[dict, None, None]:
    """
    Parse the JSONL transfer file and yield transfer records.

    Args:
        file_path: Path to the USB transfers JSONL file

    Yields:
        Dictionary containing parsed transfer data
    """
    payload_path = str(Path(file_path).with_suffix(".bin"))

    try:
        with open(file_path, "r") as f, open(payload_path, "rb") as payload:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)

                    dt = datetime.fromtimestamp(record["timestamp_ms"] / 1000.0)
                    record["timestamp_str"] = dt.isoformat(
                        timespec="microseconds"
                    )
                    record["line_num"] = line_num
                    record["data_bytes"] = read_payload_bytes(
                        payload, record["data"], record["size"]
                    )

                    iso_count = record.get("iso_packet_descriptor_count")
                    if iso_count is not None and iso_count != "NA":
                        iso_offset = record.get("iso_packet_descriptor_offset")
                        if iso_offset is not None and iso_offset != "NA":
                            iso_descriptors = read_iso_descriptors(
                                payload, iso_offset, iso_count
                            )
                            record["iso_descriptors"] = iso_descriptors

                    yield record
                except json.JSONDecodeError as e:
                    print(
                        f"Warning: Failed to parse line {line_num}: {e}",
                        file=sys.stderr,
                    )
                except (KeyError, TypeError, ValueError) as e:
                    print(
                        f"Warning: Invalid record at line {line_num}: {e}",
                        file=sys.stderr,
                    )
    except FileNotFoundError:
        print(
            f"Error: File '{file_path}' or payload file '{payload_path}' not found",
            file=sys.stderr,
        )
        return


def parse_control_setup_file(file_path: str) -> Generator[dict, None, None]:
    """
    Parse the JSONL control setup file and yield control setup records.

    Args:
        file_path: Path to the control setup JSONL file

    Yields:
        Dictionary containing parsed control setup data
    """
    payload_path = str(Path(file_path).with_suffix(".bin"))

    try:
        with open(file_path, "r") as f, open(payload_path, "rb") as payload:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)

                    record["line_num"] = line_num
                    record["data_bytes"] = read_payload_bytes(
                        payload, record["data"], record["data_size"]
                    )
                    yield record
                except json.JSONDecodeError as e:
                    print(
                        f"Warning: Failed to parse line {line_num}: {e}",
                        file=sys.stderr,
                    )
                except (KeyError, TypeError, ValueError) as e:
                    print(
                        f"Warning: Invalid record at line {line_num}: {e}",
                        file=sys.stderr,
                    )
    except FileNotFoundError:
        print(
            f"Error: File '{file_path}' or payload file '{payload_path}' not found",
            file=sys.stderr,
        )
        return


def read_payload_bytes(payload_file, offset: int, size: int) -> bytes:
    if size <= 0:
        return b""

    payload_file.seek(offset)
    data = payload_file.read(size)
    if len(data) != size:
        raise ValueError(
            f"incomplete payload read at offset {offset}: expected {size}, got {len(data)}"
        )
    return data


def read_iso_descriptors(payload_file, offset: int, count: int) -> list:
    """
    Read ISO packet descriptors from the binary file.

    Each libusb_iso_packet_descriptor is 12 bytes:
    - length (4 bytes)
    - actual_length (4 bytes)
    - status (4 bytes)
    """
    if count <= 0:
        return []

    payload_file.seek(offset)
    descriptors = []

    for _ in range(count):
        data = payload_file.read(12)  # 3 * uint32
        if len(data) != 12:
            raise ValueError(
                f"incomplete ISO descriptor read at offset {offset}: expected 12, got {len(data)}"
            )
        length, actual_length, status = struct.unpack("<III", data)
        descriptors.append(
            {"length": length, "actual_length": actual_length, "status": status}
        )

    return descriptors


def format_control_setup(setup_uint64: int) -> str:
    """
    Format a libusb_control_setup as a human-readable string.

    The structure is:
    - bmRequestType (1 byte): bit 7=dir, bits 6:5=type, bits 4:0=recipient
    - bRequest (1 byte)
    - wValue (2 bytes, little-endian)
    - wIndex (2 bytes, little-endian)
    - wLength (2 bytes, little-endian)
    """
    try:
        data = struct.pack("<Q", setup_uint64)
        bm_request_type, b_request, w_value, w_index, w_length = struct.unpack(
            "<BBHHH", data[:7]
        )

        direction = "IN" if (bm_request_type & 0x80) else "OUT"
        req_type = (bm_request_type >> 5) & 0x03
        type_names = ["Standard", "Class", "Vendor", "Reserved"]
        recipient = bm_request_type & 0x1F
        recipients = {0: "Device", 1: "Interface", 2: "Endpoint", 3: "Other"}

        return (
            f"{direction} | bmRequestType=0x{bm_request_type:02x} ({type_names[req_type]} "
            f"{recipients.get(recipient, 'Unknown')}) | bRequest=0x{b_request:02x} | "
            f"wValue=0x{w_value:04x} | wIndex=0x{w_index:04x} | wLength={w_length}"
        )
    except Exception:
        return f"Setup: 0x{setup_uint64:016x}"


def format_transfer(record: dict) -> str:
    timestamp = record.get("timestamp_str", "Unknown")
    endpoint = record.get("endpoint", "Unknown")
    size = record.get("size", 0)
    data = record.get("data_bytes", b"")
    preview = data[:16]
    formatted_data = " ".join(f"{byte:02x}" for byte in preview)
    if len(data) > 16:
        formatted_data = f"{formatted_data} ..."

    iso_info = ""
    iso_count = record.get("iso_packet_descriptor_count")
    iso_offset = record.get("iso_packet_descriptor_offset")

    if iso_count is not None and iso_count != "NA":
        iso_info = f" | ISO Packets: {iso_count} (offset: {iso_offset})"
    elif iso_count == "NA":
        iso_info = " | No ISO descriptors"

    return (
        f"[{timestamp}] EP {endpoint} | Size: {size:4d} bytes | "
        f"Data: {formatted_data}{iso_info}"
    )


def format_control_setup_record(record: dict) -> str:
    setup_desc = format_control_setup(record.get("setup", 0))
    data_size = record.get("data_size", 0)
    data = record.get("data_bytes", b"")
    preview = data[:16]
    formatted_data = " ".join(f"{byte:02x}" for byte in preview)
    if len(data) > 16:
        formatted_data = f"{formatted_data} ..."

    return (
        f"[Control Setup] {setup_desc} | Data Size: {data_size:4d} bytes | "
        f"Data: {formatted_data}"
    )


def main():
    if len(sys.argv) < 2:
        print(
            "Usage: python parse_transfers.py <transfer_file|control_setup_file>"
        )
        print("Example: python parse_transfers.py /tmp/usb_transfers.jsonl")
        print("Example: python parse_transfers.py /tmp/control_setup.jsonl")
        sys.exit(1)

    file_path = sys.argv[1]

    if "control_setup" in file_path:
        print(f"Parsing control setup records from: {file_path}\n")
        print("-" * 150)

        setup_count = 0
        total_data_bytes = 0

        for record in parse_control_setup_file(file_path):
            print(format_control_setup_record(record))

            setup_count += 1
            total_data_bytes += record.get("data_size", 0)

        print("-" * 150)
        print(f"\nSummary:")
        print(f"  Total control setups: {setup_count}")
        print(f"  Total data bytes: {total_data_bytes}")
    else:
        print(f"Parsing USB transfer records from: {file_path}\n")
        print("-" * 120)

        transfer_count = 0
        total_bytes = 0
        iso_transfer_count = 0
        endpoints = {}

        for record in parse_transfer_file(file_path):
            print(format_transfer(record))

            transfer_count += 1
            total_bytes += record.get("size", 0)

            iso_count = record.get("iso_packet_descriptor_count")
            if iso_count is not None and iso_count != "NA":
                iso_transfer_count += 1

            endpoint = record.get("endpoint", "Unknown")
            if endpoint not in endpoints:
                endpoints[endpoint] = {"count": 0, "bytes": 0, "iso_count": 0}
            endpoints[endpoint]["count"] += 1
            endpoints[endpoint]["bytes"] += record.get("size", 0)

            if iso_count is not None and iso_count != "NA":
                endpoints[endpoint]["iso_count"] += 1

        print("-" * 120)
        print(f"\nSummary:")
        print(f"  Total transfers: {transfer_count}")
        print(f"  Total bytes: {total_bytes}")
        print(f"  ISO transfers: {iso_transfer_count}")
        print(f"\nTransfers by endpoint:")
        for ep in sorted(endpoints.keys()):
            stats = endpoints[ep]
            iso_str = (
                f", {stats['iso_count']} ISO" if stats["iso_count"] > 0 else ""
            )
            print(
                f"  {ep}: {stats['count']} transfers, {stats['bytes']} bytes{iso_str}"
            )


if __name__ == "__main__":
    main()
