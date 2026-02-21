#!/usr/bin/env python3

"""
Script to parse USB transfer records from the JSONL output file.
Each line is a JSON object with the following fields:
  - timestamp_ms: Timestamp in milliseconds since epoch
  - endpoint: Endpoint address (hex string)
  - size: Number of bytes transferred
  - data: Transfer data as hex string
"""


import json
import sys
from datetime import datetime
from typing import Generator


def parse_transfer_file(file_path: str) -> Generator[dict, None, None]:
    """
    Parse the JSONL transfer file and yield transfer records.

    Args:
        file_path: Path to the USB transfers JSONL file

    Yields:
        Dictionary containing parsed transfer data
    """
    try:
        with open(file_path, 'r') as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)

                    dt = datetime.fromtimestamp(record['timestamp_ms'] / 1000.0)
                    record['timestamp_str'] = dt.isoformat(timespec='microseconds')
                    record['line_num'] = line_num
                    yield record
                except json.JSONDecodeError as e:
                    print(
                        f"Warning: Failed to parse line {line_num}: {e}",
                        file=sys.stderr
                    )
    except FileNotFoundError:
        print(f"Error: File '{file_path}' not found", file=sys.stderr)
        return


def format_transfer(record: dict) -> str:
    timestamp = record.get('timestamp_str', 'Unknown')
    endpoint = record.get('endpoint', 'Unknown')
    size = record.get('size', 0)
    data = record.get('data', '')

    formatted_data = ' '.join(data[i:i+2] for i in range(0, len(data), 2))

    return (f"[{timestamp}] EP {endpoint} | Size: {size:4d} bytes | "
            f"Data: {formatted_data}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python parse_transfers.py <transfer_file>")
        print("Example: python parse_transfers.py /tmp/usb_transfers.jsonl")
        sys.exit(1)

    file_path = sys.argv[1]

    print(f"Parsing USB transfer records from: {file_path}\n")
    print("-" * 100)

    transfer_count = 0
    total_bytes = 0
    endpoints = {}

    for record in parse_transfer_file(file_path):
        print(format_transfer(record))

        transfer_count += 1
        total_bytes += record.get('size', 0)

        endpoint = record.get('endpoint', 'Unknown')
        if endpoint not in endpoints:
            endpoints[endpoint] = {'count': 0, 'bytes': 0}
        endpoints[endpoint]['count'] += 1
        endpoints[endpoint]['bytes'] += record.get('size', 0)

    print("-" * 100)
    print(f"\nSummary:")
    print(f"  Total transfers: {transfer_count}")
    print(f"  Total bytes: {total_bytes}")
    print(f"\nTransfers by endpoint:")
    for ep in sorted(endpoints.keys()):
        stats = endpoints[ep]
        print(f"  {ep}: {stats['count']} transfers, {stats['bytes']} bytes")


if __name__ == '__main__':
    main()
