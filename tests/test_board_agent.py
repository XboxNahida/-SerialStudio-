"""板端桥接测试：不打开真实总线或 USB。"""
import importlib.util
import pathlib
import unittest
import subprocess
import sys
import json
import socket
import struct
import time

spec = importlib.util.spec_from_file_location("board_agent", pathlib.Path(__file__).parents[1] / "board" / "board_agent.py")
agent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(agent)


class Hardware:
    def __init__(self):
        self.calls = []

    def i2c(self, bus, address, tx, count):
        self.calls.append(("i2c", bus, address, tx, count))
        return bytes(range(count))

    def spi(self, device, mode, speed, tx):
        self.calls.append(("spi", device, mode, speed, tx))
        return bytes(byte ^ 255 for byte in tx)

    def usb_list(self):
        return [{"vid": "1234", "pid": "5678"}]


class BoardAgentTests(unittest.TestCase):
    def test_combined_i2c(self):
        hardware = Hardware()
        reply = agent.handle_request({"id": 7, "bus": "i2c", "op": "transfer", "device": 1,
                                      "address": 80, "tx": "00 10", "read": 2}, hardware)
        self.assertTrue(reply["ok"])
        self.assertEqual(reply["rx"], "0001")
        self.assertEqual(reply["id"], 7)
        self.assertEqual(hardware.calls, [("i2c", 1, 80, b"\x00\x10", 2)])

    def test_spi_full_duplex(self):
        hardware = Hardware()
        reply = agent.handle_request({"id": 8, "bus": "spi", "op": "transfer", "device": "/dev/spidev0.0",
                                      "mode": 3, "speed": 1000000, "tx": "00 ff"}, hardware)
        self.assertEqual(reply["rx"], "ff00")
        self.assertEqual(hardware.calls[0][2], 3)

    def test_reject_invalid_before_hardware(self):
        hardware = Hardware()
        for patch in ({"address": 128}, {"device": -1}, {"read": 4097}, {"tx": "GG"}, {"tx": "0"}, {"read": True}):
            request = {"id": 1, "bus": "i2c", "op": "transfer", "device": 0, "address": 80, "tx": "00", "read": 1}
            request.update(patch)
            reply = agent.handle_request(request, hardware)
            self.assertFalse(reply["ok"], patch)
            self.assertTrue(reply["error"])
        self.assertEqual(hardware.calls, [])

    def test_framing_split_joined_and_bounds(self):
        for framing, head, tail in (("lines", b"", b""), ("custom", b"\xaa\x55", b"\r\n")):
            codec = agent.Framing(framing, head, tail)
            packet = codec.encode({"id": 1, "bus": "system", "op": "info"})
            self.assertEqual(codec.feed(packet[:3]), [])
            self.assertEqual(len(codec.feed(packet[3:] + packet)), 2)
            codec.feed(b"x" * 70000)
            self.assertLessEqual(len(codec.buffer), 65536)

    def test_usb_and_errors(self):
        reply = agent.handle_request({"id": 2, "bus": "usb", "op": "list"}, Hardware())
        self.assertTrue(reply["ok"])
        self.assertEqual(len(reply["devices"]), 1)
        self.assertFalse(agent.handle_request({"id": 1, "bus": "wrong", "op": "x"}, Hardware())["ok"])

    def test_conflicting_response_delimiter_does_not_raise(self):
        codec = agent.Framing("custom", b"H", b'"ok":')
        self.assertEqual(agent.respond(b'{"id":1,"bus":"system","op":"info"}', codec), b"")

    def test_stdio_system_info(self):
        result = subprocess.run([sys.executable, "-B", agent.__file__], input=b'{"id":9,"bus":"system","op":"info"}\n',
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        reply = json.loads(result.stdout)
        self.assertEqual(reply["id"], 9)
        self.assertTrue(reply["ok"])
        self.assertIn("hostname", reply["info"])

    def test_tcp_server_survives_reset(self):
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]
        process = subprocess.Popen([sys.executable, "-B", agent.__file__, "--tcp", "127.0.0.1:%d" % port],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    peer = socket.create_connection(("127.0.0.1", port), timeout=1)
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        self.fail("TCP 桥接未启动")
                    time.sleep(0.02)
            with peer:
                peer.sendall(b'{"id":1,"bus":"system","op":"info"}\n')
                self.assertTrue(json.loads(peer.makefile("rb").readline())["ok"])
                linger = struct.pack("hh" if sys.platform == "win32" else "ii", 1, 0)
                peer.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, linger)
            with socket.create_connection(("127.0.0.1", port), timeout=2) as next_peer:
                next_peer.sendall(b'{"id":2,"bus":"system","op":"info"}\n')
                self.assertEqual(json.loads(next_peer.makefile("rb").readline())["id"], 2)
            self.assertIsNone(process.poll())
        finally:
            process.terminate()
            process.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
