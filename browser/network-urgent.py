#!/usr/bin/env python3
"""Native urgent relay conformance service; no guest/socket policy lives here.

Linux net/ipv4/tcp.c:tcp_sendmsg_locked,tcp_recv_urg,tcp_ioctl.
One-byte reads cannot cross an urgent mark. All sockets/threads die with stdin.
"""
import array
import base64
import fcntl
import json
import os
import select
import socket
import sys
import threading


def emit(event):
    print(json.dumps(event), flush=True)


def at_mark(sock):
    value = array.array("i", [0])
    fcntl.ioctl(sock.fileno(), 0x8905, value, True)  # SIOCATMARK
    return bool(value[0])


def main():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        client = socket.create_connection(listener.getsockname())
        peer, _ = listener.accept()
    client.setsockopt(socket.SOL_SOCKET, socket.SO_OOBINLINE, 1)
    peer.setsockopt(socket.SOL_SOCKET, socket.SO_OOBINLINE, 1)
    stopped = threading.Event()

    def echo():
        try:
            while not stopped.is_set():
                readable, _, _ = select.select([peer], [], [], 0.1)
                if not readable:
                    continue
                urgent = at_mark(peer)
                byte = peer.recv(1)
                if not byte:
                    peer.shutdown(socket.SHUT_WR)
                    return
                peer.sendall(byte, socket.MSG_OOB if urgent else 0)
        except OSError:
            if not stopped.is_set():
                emit({"error": "native urgent echo failed"})

    thread = threading.Thread(target=echo)
    thread.start()
    pending = bytearray()
    emit({"ready": True})
    try:
        while True:
            readable, _, _ = select.select([sys.stdin, client], [], [])
            if sys.stdin in readable:
                chunk = os.read(sys.stdin.fileno(), 65536)
                if not chunk:
                    break
                pending.extend(chunk)
                if len(pending) > 1024 * 1024:
                    raise ValueError("oversized urgent helper command")
                while b"\n" in pending:
                    line, _, rest = pending.partition(b"\n")
                    pending[:] = rest
                    command = json.loads(line)
                    data = base64.b64decode(command["data"], validate=True)
                    flags = socket.MSG_OOB if command["urgent"] else 0
                    if flags and len(data) != 1:
                        raise ValueError("urgent relay sends exactly its marked byte")
                    amount = client.send(data, flags)
                    emit({"request": command["request"], "sent": amount})
            if client in readable:
                urgent = at_mark(client)
                byte = client.recv(1)
                if not byte:
                    emit({"eof": True})
                    break
                emit({"urgent": urgent, "data": base64.b64encode(byte).decode("ascii")})
    finally:
        stopped.set()
        client.close()
        thread.join(1)
        peer.close()
        if thread.is_alive():
            raise RuntimeError("native urgent helper failed to retire")


if __name__ == "__main__":
    main()
