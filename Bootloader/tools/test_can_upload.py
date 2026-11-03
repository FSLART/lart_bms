"""Teste do can_upload.py contra o bootloader real (logica em C) num bus CAN virtual.

Compila Bootloader/Src (bl_core, bl_protocol) + Firmware/Core/boot com o
sim_device.c numa biblioteca, liga-a a um bus "virtual" do python-can e corre
o uploader como na bancada: pedido pelo 0x0DC, INFO, BEGIN, DATA, END, RESET.
Depois arranca o "bootloader" e confirma que a imagem nova ficou em teste.

  python -m unittest Bootloader/tools/test_can_upload.py -v
"""
import ctypes
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import unittest

import can

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import can_upload  # noqa: E402

ROOT = TOOLS.parents[1]
RUN_PENDING, RUN_CONFIRMED, RUN_EXTERNAL = 2, 1, 3
RESET_SOFT_PIN = (1 << 28) | (1 << 26)


def build_library(directory):
    compiler = os.environ.get("CC", "gcc")
    if shutil.which(compiler) is None:
        raise unittest.SkipTest(f"sem {compiler}")
    sources = [TOOLS / "sim_device.c"] + [ROOT / "Bootloader/Src" / name for name in ("bl_core.c", "bl_protocol.c", "bl_log.c")] + \
        [ROOT / "Firmware/Core/boot" / name for name in ("boot_hash.c", "boot_image.c", "boot_journal.c", "boot_shared.c", "boot_names.c")]
    library = Path(directory) / "libbmsboot.so"
    subprocess.run([compiler, "-shared", "-fPIC", "-O1", "-std=gnu11", "-Wall", "-DBOOT_HOST_TEST",
                    f"-I{ROOT / 'Bootloader/Src'}", f"-I{ROOT / 'Firmware/Core/boot'}", "-o", str(library), *map(str, sources)],
                   check=True)
    return ctypes.CDLL(str(library))


def make_app(size, major, minor, seed, board=can_upload.BOARD_ID, entry=can_upload.APP_ADDRESS + 0x501, sp=can_upload.STACK_TOP):
    """Uma 'aplicacao' ligada a 0x08010200: vetores, descritor e bytes ao calhas."""
    generator = random.Random(seed)
    body = bytearray(generator.getrandbits(8) for _ in range(size))
    struct.pack_into("<II", body, 0, sp, entry)
    struct.pack_into("<IIBBHII", body, can_upload.APP_INFO_OFFSET, can_upload.APP_INFO_MAGIC, board, major, minor, 0, 0, 3)
    body[-4:] = b"\x12\x34\x56\x78"
    return bytes(body)


class Device(threading.Thread):
    """O BMS: a aplicacao reage ao 0x0DC/0x601 reiniciando para o bootloader, que responde ao 0x602."""

    def __init__(self, library, channel, bus_index=0):
        super().__init__(daemon=True)
        self.library = library
        self.bus = can.Bus(interface="virtual", channel=channel)
        self.bus_index = bus_index
        self.in_bootloader = False
        self.resets = 0
        self.done = threading.Event()

    def send(self, arbitration_id, data):
        self.bus.send(can.Message(arbitration_id=arbitration_id, data=bytes(data), is_extended_id=False))

    def run(self):
        reply = (ctypes.c_uint8 * 8)()
        diag_data = (ctypes.c_uint8 * 8)()
        diag_id = ctypes.c_uint32()
        while not self.done.is_set():
            msg = self.bus.recv(0.02)
            if msg is None:
                continue
            data = bytes(msg.data)
            if not self.in_bootloader:
                enter = (msg.arbitration_id == 0x0DC and data[:1] == b"\x01") or \
                        (msg.arbitration_id == 0x601 and self.bus_index == 1 and data == bytes([1, 0, 0, 0, 0, 0, 0, 0]))
                if enter:
                    self.library.sim_start(1)
                    self.in_bootloader = True
                    self.send(0x581, [0x55, 0, 0, 0, 0, 1, 0, 0])
                continue
            if msg.arbitration_id != 0x602:
                continue
            buffer = (ctypes.c_uint8 * 8)(*data.ljust(8, b"\0"))
            self.library.sim_frame(self.bus_index, buffer, len(data), reply)
            self.send(0x580, bytes(reply))
            while self.library.sim_diag(self.bus_index, ctypes.byref(diag_id), diag_data) >= 0:
                self.send(diag_id.value, bytes(diag_data))
            if self.library.sim_reset_requested():
                self.in_bootloader = False
                self.resets += 1

    def stop(self):
        self.done.set()
        self.join(2)
        self.bus.shutdown()


class UploadTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.library = build_library(cls.directory.name)
        cls.library.sim_boot.restype = ctypes.c_int
        cls.path = Path(cls.directory.name)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def write(self, name, data):
        path = self.path / name
        path.write_bytes(data)
        return path

    def flash_v1(self):
        self.library.sim_erase()
        app = make_app(6000, 13, 0, 1)
        self.assertEqual(self.library.sim_debugger_flash(app, len(app)), 0)
        self.assertEqual(self.library.sim_boot(1 << 27) & 0xFF, RUN_EXTERNAL)

    def upload(self, path, channel, *extra, bus_index=0):
        device = Device(self.library, channel, bus_index)
        device.start()
        try:
            can_upload.main([str(path), "--interface", "virtual", "--channel", channel, "--wait", "5", *extra])
        finally:
            device.stop()
        return device

    def test_package_reads_the_descriptor(self):
        source = self.write("app.bin", make_app(9000, 13, 1, 2))
        output = self.path / "app.mcuboot.bin"
        can_upload.main([str(source), "--package-only", str(output), "--build", "7"])
        image = output.read_bytes()
        can_upload.check_image(image)
        magic, _, header, _, size, _, major, minor, revision, build = struct.unpack_from("<IIHHIIBBHI", image)
        self.assertEqual((magic, header, size), (can_upload.IMAGE_MAGIC, 512, 9000))
        self.assertEqual((major, minor, revision, build), (13, 1, 0, 7))
        self.assertEqual(len(image), 512 + 9000 + 40)
        # uma imagem ja empacotada passa como esta
        again, version, packaged, _ = can_upload.prepare_image(output)
        self.assertTrue(packaged)
        self.assertEqual(again, image)
        self.assertEqual(version, (13, 1, 0, 7))

    def test_rejects_other_boards_and_old_link_address(self):
        with self.assertRaisesRegex(can_upload.UploadError, "outra placa"):
            can_upload.prepare_image(self.write("acu.bin", make_app(4000, 1, 0, 3, board=0x31554341)))
        with self.assertRaisesRegex(can_upload.UploadError, "0x08010200"):
            can_upload.prepare_image(self.write("old.bin", make_app(4000, 1, 0, 3, entry=0x08000401)))
        with self.assertRaisesRegex(can_upload.UploadError, "256K - 32"):
            can_upload.prepare_image(self.write("ram.bin", make_app(4000, 1, 0, 3, sp=0x20040000)))
        with self.assertRaisesRegex(can_upload.UploadError, "EXEC"):
            can_upload.prepare_image(self.write("big.bin", make_app(can_upload.IMAGE_MAX, 1, 0, 3)))

    def test_hex_input(self):
        app = make_app(5000, 13, 1, 4)
        lines = [":020000040801F1"]
        for offset in range(0, len(app), 16):
            address = 0x0200 + offset
            chunk = app[offset:offset + 16]
            record = bytes([len(chunk), address >> 8, address & 0xFF, 0]) + chunk
            lines.append(":" + (record + bytes([(-sum(record)) & 0xFF])).hex().upper())
        lines.append(":00000001FF")
        path = self.path / "app.hex"
        path.write_text("\n".join(lines) + "\n")
        image, version, _, _ = can_upload.prepare_image(path, build=1)
        self.assertEqual(image[512:512 + len(app)], app)
        self.assertEqual(version, (13, 1, 0, 1))

    def test_upload_over_can1_with_start_programmer(self):
        self.flash_v1()
        device = self.upload(self.write("v2.bin", make_app(7000, 13, 2, 5)), "bms-can1")
        self.assertEqual(device.resets, 1)
        result = self.library.sim_boot(RESET_SOFT_PIN)
        self.assertEqual(result >> 16, 13)
        self.assertEqual((result >> 8) & 0xFF, 2)
        self.assertEqual(result & 0xFF, RUN_PENDING)

    def test_upload_over_can2_with_enter_boot(self):
        self.flash_v1()
        device = self.upload(self.write("v3.bin", make_app(5000, 13, 3, 6)), "bms-can2", "--entry", "enter-boot", "--no-reset", bus_index=1)
        self.assertEqual(device.resets, 0)
        result = self.library.sim_boot(RESET_SOFT_PIN)
        self.assertEqual((result >> 8) & 0xFF, 3)


if __name__ == "__main__":
    unittest.main()
