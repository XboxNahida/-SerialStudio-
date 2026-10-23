#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Linux 开发板参考桥接器：沿用工作台 JSON 行或自定义帧头尾。

收到明确请求才访问外设；无自动探测写入、驱动解绑或权限提升。
默认处理标准输入中的一个 JSON 请求，适合通过 SSH 执行。
"""
import argparse
import ctypes
import glob
import json
import os
import platform
import re
import select
import socket
import struct
import sys

MAX_BYTES = 4096


def integer(value, minimum, maximum, name):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError("%s 范围应为 %d–%d 的整数" % (name, minimum, maximum))
    return value


def hex_bytes(value):
    if not isinstance(value, str) or len(value) > MAX_BYTES * 3:
        raise ValueError("HEX 内容过长或类型错误")
    clean = "".join(value.split())
    if len(clean) % 2 or re.search(r"[^0-9a-fA-F]", clean):
        raise ValueError("HEX 必须每字节两位，仅包含十六进制和空白字符")
    data = bytes.fromhex(clean)
    if len(data) > MAX_BYTES:
        raise ValueError("单次最多 4096 字节")
    return data


class LinuxHardware:
    def i2c(self, bus, address, tx, count):
        import fcntl

        class Message(ctypes.Structure):
            _fields_ = [("addr", ctypes.c_uint16), ("flags", ctypes.c_uint16),
                        ("length", ctypes.c_uint16), ("buf", ctypes.c_void_p)]

        class Transfer(ctypes.Structure):
            _fields_ = [("msgs", ctypes.POINTER(Message)), ("count", ctypes.c_uint32)]

        buffers, messages = [], []
        if tx:
            buffers.append(ctypes.create_string_buffer(tx, len(tx)))
            messages.append(Message(address, 0, len(tx), ctypes.addressof(buffers[-1])))
        if count:
            buffers.append(ctypes.create_string_buffer(count))
            messages.append(Message(address, 1, count, ctypes.addressof(buffers[-1])))
        array = (Message * len(messages))(*messages)
        transfer = Transfer(array, len(messages))
        fd = os.open("/dev/i2c-%d" % bus, os.O_RDWR)
        try:
            # 一次 I2C_RDWR 实现写后读，保留 repeated START。
            fcntl.ioctl(fd, 0x0707, bytes(transfer))
            return buffers[-1].raw[:count] if count else b""
        finally:
            os.close(fd)

    def spi(self, device, mode, speed, tx):
        import fcntl
        outgoing = ctypes.create_string_buffer(tx, len(tx))
        incoming = ctypes.create_string_buffer(len(tx))
        transfer = struct.pack("=QQIIHBBBBBB", ctypes.addressof(outgoing), ctypes.addressof(incoming),
                               len(tx), speed, 0, 8, 0, 0, 0, 0, 0)
        fd = os.open(device, os.O_RDWR)
        try:
            fcntl.ioctl(fd, 0x40016B01, bytes([mode]))
            fcntl.ioctl(fd, 0x40016B03, b"\x08")
            fcntl.ioctl(fd, 0x40046B04, struct.pack("=I", speed))
            # SPI_IOC_MESSAGE(1)，32 字节标准 Linux UAPI spi_ioc_transfer。
            fcntl.ioctl(fd, 0x40206B00, transfer)
            return incoming.raw
        finally:
            os.close(fd)

    def usb_list(self):
        devices = []
        for directory in sorted(glob.glob("/sys/bus/usb/devices/*")):
            if not os.path.isfile(os.path.join(directory, "idVendor")):
                continue
            item = {}
            for key, filename in (("vid", "idVendor"), ("pid", "idProduct"), ("bus", "busnum"),
                                  ("address", "devnum"), ("product", "product"), ("manufacturer", "manufacturer")):
                try:
                    with open(os.path.join(directory, filename), encoding="utf-8", errors="replace") as file:
                        item[key] = file.read(256).strip()
                except OSError:
                    item[key] = ""
            devices.append(item)
            if len(devices) >= 128:
                break
        return devices

    def usb_transfer(self, request, tx, count):
        try:
            import usb.core
            import usb.util
        except ImportError as error:
            raise ValueError("USB 端点读写需要板端安装 PyUSB 和 libusb；枚举不需要此依赖") from error
        vid = integer(request.get("vid"), 0, 65535, "VID")
        pid = integer(request.get("pid"), 0, 65535, "PID")
        interface = integer(request.get("interface", 0), 0, 255, "USB 接口")
        endpoint = integer(request.get("endpoint"), 1, 255, "USB 端点")
        if endpoint & 0x70 or endpoint & 0x0F == 0:
            raise ValueError("请选择非零且有效的 USB IN/OUT 端点")
        timeout = integer(request.get("timeout", 1000), 100, 5000, "USB 超时")
        devices = list(usb.core.find(find_all=True, idVendor=vid, idProduct=pid))
        bus = integer(request.get("usb_bus", 0), 0, 255, "USB 总线")
        address = integer(request.get("usb_address", 0), 0, 255, "USB 地址")
        devices = [dev for dev in devices if (not bus or dev.bus == bus) and (not address or dev.address == address)]
        if len(devices) != 1:
            raise ValueError("匹配 USB 设备数量为 %d，请检查 VID/PID 并指定总线/地址" % len(devices))
        device = devices[0]
        try:
            # 不主动复位、设置配置或解绑系统驱动。
            config = device.get_active_configuration()
            endpoints = [ep.bEndpointAddress for intf in config if intf.bInterfaceNumber == interface for ep in intf]
            if endpoint not in endpoints:
                raise ValueError("端点不属于所选接口的当前配置")
            usb.util.claim_interface(device, interface)
            try:
                if request["op"] == "read":
                    if not endpoint & 0x80 or not count:
                        raise ValueError("读取需要 IN 端点及正数读取长度")
                    return bytes(device.read(endpoint, count, timeout))
                if endpoint & 0x80 or not tx:
                    raise ValueError("写入需要 OUT 端点及非空数据")
                written = device.write(endpoint, tx, timeout)
                if written != len(tx):
                    raise OSError("USB 数据未完整写入")
                return b""
            finally:
                usb.util.release_interface(device, interface)
        finally:
            usb.util.dispose_resources(device)


def handle_request(request, hardware=None):
    hardware = hardware or LinuxHardware()
    response = {"id": None, "bus": "", "ok": False}
    try:
        if not isinstance(request, dict):
            raise ValueError("请求必须为 JSON 对象")
        response["id"] = integer(request.get("id"), 1, 2147483647, "请求 ID")
        bus, operation = request.get("bus"), request.get("op")
        response["bus"] = bus
        if bus == "system" and operation == "info":
            response["info"] = {"hostname": socket.gethostname(), "system": platform.platform(),
                                "i2c": sorted(glob.glob("/dev/i2c-*")), "spi": sorted(glob.glob("/dev/spidev*"))}
        elif bus == "usb" and operation == "list":
            response["devices"] = hardware.usb_list()
        elif bus in ("i2c", "spi", "usb"):
            tx = hex_bytes(request.get("tx", ""))
            count = integer(request.get("read", 0), 0, MAX_BYTES, "读取长度")
            if bus == "i2c" and operation == "transfer":
                device = integer(request.get("device"), 0, 255, "I²C 总线")
                address = integer(request.get("address"), 0, 127, "7 位 I²C 地址")
                if not tx and not count:
                    raise ValueError("I²C 写入数据与读取长度不能同时为空")
                rx = hardware.i2c(device, address, tx, count)
            elif bus == "spi" and operation == "transfer":
                device = request.get("device", "")
                if not isinstance(device, str) or not re.fullmatch(r"/dev/spidev[0-9]{1,3}\.[0-9]{1,3}", device):
                    raise ValueError("SPI 设备格式为 /dev/spidev0.0")
                mode = integer(request.get("mode"), 0, 3, "SPI 模式")
                speed = integer(request.get("speed"), 1, 100000000, "SPI 频率")
                if not tx:
                    raise ValueError("SPI 全双工传输需要发送数据，可填写 FF 产生读取时钟")
                rx = hardware.spi(device, mode, speed, tx)
            elif bus == "usb" and operation in ("read", "write"):
                rx = hardware.usb_transfer(request, tx, count)
            else:
                raise ValueError("不支持此总线操作")
            response["rx"] = rx.hex()
        else:
            raise ValueError("不支持的 bus/op")
        response["ok"] = True
    except Exception as error:
        response["error"] = "%s: %s" % (type(error).__name__, error)
    return response


class Framing:
    def __init__(self, mode="lines", head=b"", tail=b""):
        if mode not in ("lines", "custom") or (mode == "custom" and (not head or not tail or max(len(head), len(tail)) > 32)):
            raise ValueError("选择 JSON 行或配置各 1–32 字节的自定义帧头尾")
        self.mode, self.head, self.tail, self.buffer = mode, head, tail, b""

    def encode(self, obj):
        body = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if self.mode == "lines":
            return body + b"\n"
        if self.tail in body:
            raise ValueError("JSON 与帧尾冲突，请选择 JSON 行或不冲突的帧尾")
        return self.head + body + self.tail

    def feed(self, data):
        self.buffer += data
        result = []
        while self.buffer:
            if self.mode == "lines":
                end = self.buffer.find(b"\n")
                if end < 0:
                    break
                result.append(self.buffer[:end].rstrip(b"\r"))
                self.buffer = self.buffer[end + 1:]
            else:
                start = self.buffer.find(self.head)
                if start < 0:
                    self.buffer = self.buffer[-(len(self.head) - 1):] if len(self.head) > 1 else b""
                    break
                self.buffer = self.buffer[start:]
                end = self.buffer.find(self.tail, len(self.head))
                if end < 0:
                    break
                result.append(self.buffer[len(self.head):end])
                self.buffer = self.buffer[end + len(self.tail):]
        if len(self.buffer) > 65536:
            self.buffer = b""
            result.append(b"")
        return result


def respond(body, codec):
    try:
        if len(body) > 65536:
            raise ValueError("请求超过 64 KiB")
        request = json.loads(body)
        # 常见固定响应字段也必须能封包，避免已知帧尾冲突仍执行硬件操作。
        codec.encode({"id": request.get("id"), "bus": request.get("bus"), "ok": False, "error": ""})
        reply = handle_request(request)
    except Exception as error:
        reply = {"id": None, "bus": "", "ok": False, "error": str(error)}
    try:
        return codec.encode(reply)
    except ValueError as error:
        print("桥接响应无法按当前帧尾封包：%s；请使用 JSON 行或不冲突的帧尾" % error, file=sys.stderr, flush=True)
        return b""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--tcp", metavar="IP:端口")
    transport.add_argument("--udp", metavar="IP:端口")
    transport.add_argument("--serial", metavar="/dev/ttyS1")
    transport.add_argument("--stdio", action="store_true", help="连续处理标准输入；默认只处理一次")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--framing", choices=("lines", "custom"), default="lines")
    parser.add_argument("--head", default="AA55")
    parser.add_argument("--tail", default="0D0A")
    args = parser.parse_args()
    codec = Framing(args.framing, hex_bytes(args.head), hex_bytes(args.tail))
    if args.tcp or args.udp:
        host, port = (args.tcp or args.udp).rsplit(":", 1)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM if args.tcp else socket.SOCK_DGRAM) as server:
            server.bind((host, int(port)))
            if args.tcp:
                server.listen(1)
                while True:
                    peer, _ = server.accept()
                    with peer:
                        codec.buffer = b""
                        try:
                            while True:
                                data = peer.recv(4096)
                                if not data:
                                    break
                                for body in codec.feed(data):
                                    peer.sendall(respond(body, codec))
                        except (ConnectionError, OSError) as error:
                            print("TCP 对端已断开：%s" % error, file=sys.stderr, flush=True)
            else:
                while True:
                    data, peer = server.recvfrom(65507)
                    codec.buffer = b""
                    for body in codec.feed(data):
                        server.sendto(respond(body, codec), peer)
    elif args.serial:
        import termios
        speed = getattr(termios, "B%d" % args.baud, None)
        if speed is None:
            raise ValueError("系统 termios 不支持此波特率")
        fd = os.open(args.serial, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            attrs = termios.tcgetattr(fd)
            attrs[0], attrs[1], attrs[2], attrs[3] = 0, 0, termios.CS8 | termios.CREAD | termios.CLOCAL, 0
            attrs[4] = attrs[5] = speed
            attrs[6][termios.VMIN], attrs[6][termios.VTIME] = 1, 0
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            while True:
                select.select([fd], [], [])
                for body in codec.feed(os.read(fd, 4096)):
                    output = respond(body, codec)
                    while output:
                        select.select([], [fd], [])
                        try:
                            output = output[os.write(fd, output):]
                        except BlockingIOError:
                            continue
        finally:
            os.close(fd)
    else:
        while True:
            # 逐字节读取仅用于 SSH 单次控制；不依赖 EOF 或大块缓冲才输出。
            data = sys.stdin.buffer.read(1)
            if not data:
                break
            for body in codec.feed(data):
                sys.stdout.buffer.write(respond(body, codec))
                sys.stdout.buffer.flush()
                if not args.stdio:
                    return


if __name__ == "__main__":
    main()
