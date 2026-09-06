'''
File:   print_test.py
Brief:  TCP print test utility for the TPTCM WiFi bridge (port 9100)
Author: Mistress-Lukutar
Date:   2026-09-03
Version: v1.0.0
'''

from __future__ import annotations

import argparse
import socket
import sys

DEFAULT_PORT = 9100
CONNECT_TIMEOUT_S = 5.0
DRAIN_TIMEOUT_S = 30.0

ESC = b'\x1b'
GS = b'\x1d'
INIT = ESC + b'@'
FEED_N = ESC + b'd'
CUT_PARTIAL = GS + b'V\x01'
CUT_FULL = GS + b'V\x00'


def build_payload(lines: list[str], encoding: str, feed: int, cut: str | None) -> bytes:
    '''Build an ESC/POS byte stream for the given text lines.

    Args:
        lines: Text lines to print.
        encoding: Codec used to encode the text (e.g. utf-8, cp866).
        feed: Number of empty lines to feed after the text.
        cut: Cut mode ('partial', 'full') or None for no cut.

    Returns:
        ESC/POS byte payload.
    '''
    payload = INIT
    for line in lines:
        payload += line.encode(encoding, errors='replace') + b'\n'
    if feed > 0:
        payload += FEED_N + bytes([feed])
    if cut == 'partial':
        payload += CUT_PARTIAL
    elif cut == 'full':
        payload += CUT_FULL
    return payload


def send_job(host: str, port: int, payload: bytes) -> None:
    '''Send a print job to the bridge and wait until it is drained.

    Args:
        host: Bridge IP address or hostname.
        port: RAW TCP port (JetDirect style).
        payload: ESC/POS bytes to send.

    Raises:
        OSError: If connect/send fails.
    '''
    with socket.create_connection((host, port), timeout=CONNECT_TIMEOUT_S) as sock:
        sock.settimeout(DRAIN_TIMEOUT_S)
        sock.sendall(payload)
        # Half-close so the bridge sees EOF, flushes the UART and closes.
        sock.shutdown(socket.SHUT_WR)
        try:
            sock.recv(1)
        except socket.timeout:
            pass


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    '''Parse command-line arguments.

    Args:
        argv: Argument list (None for sys.argv).

    Returns:
        Parsed namespace.
    '''
    parser = argparse.ArgumentParser(
        description='Send a test print job to the TPTCM WiFi bridge (RAW TCP :9100).',
    )
    parser.add_argument('host', help='bridge IP address or hostname')
    parser.add_argument('text', nargs='+', help='text line(s) to print')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT, help=f'TCP port (default {DEFAULT_PORT})')
    parser.add_argument('--encoding', default='utf-8', help='text encoding, e.g. cp866 for Cyrillic (default utf-8)')
    parser.add_argument('--feed', type=int, default=3, help='feed lines after text (default 3)')
    parser.add_argument('--cut', choices=('partial', 'full'), default=None, help='cut paper after printing')
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    '''Entry point.

    Args:
        argv: Argument list (None for sys.argv).

    Returns:
        Process exit code.
    '''
    args = parse_args(argv)
    payload = build_payload(args.text, args.encoding, args.feed, args.cut)
    try:
        send_job(args.host, args.port, payload)
    except OSError as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 1
    print(f'sent {len(payload)} bytes to {args.host}:{args.port}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
