"""Teste do can_upload.py contra o bootloader real (logica em C) num bus CAN virtual.

Compila Bootloader/Src (bl_core, bl_protocol) + Firmware/Core/boot com o
sim_device.c numa biblioteca, liga-a a um bus "virtual" do python-can e corre
o uploader como na bancada: pedido pelo 0x0DC, INFO, BEGIN, DATA, END, RESET.
Depois arranca o "bootloader" e confirma que a imagem nova ficou em teste.

  python -m unittest Bootloader/tools/test_can_upload.py -v
"""
import contextlib
import ctypes
import io
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
import zlib

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


def response(command, status=0, value=0, protocol=1, state=0):
    return can.Message(arbitration_id=0x580, is_extended_id=False,
                       data=bytes([command | 0x80, status]) + struct.pack("<I", value) + bytes([protocol, state]))


class FakeBus:
    """Um bus sem threads: script(msg) devolve as tramas que 'chegam' a seguir a cada envio."""

    def __init__(self, script=None):
        self.script = script
        self.sent = []
        self.queue = []

    def send(self, msg, timeout=None):
        self.sent.append(msg)
        if self.script is not None:
            self.queue.extend(self.script(msg))

    def recv(self, timeout=None):
        return self.queue.pop(0) if self.queue else None


class ImageTest(unittest.TestCase):
    """O que o uploader verifica antes de abrir o CAN: HEX, vetores, descritor, cabecalho, TLV, SHA-256."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name)
        self.app = make_app(3000, 1, 2, 9)
        self.image = can_upload.package(self.app, (1, 2, 0, 5))
        self.end = 512 + len(self.app)

    def tearDown(self):
        self.directory.cleanup()

    def tlv(self, *entries, total=None):
        body = b"".join(struct.pack("<HH", kind, len(data)) + data for kind, data in entries)
        return struct.pack("<HH", can_upload.TLV_INFO_MAGIC, total if total is not None else 4 + len(body)) + body

    def test_check_image_rejects(self):
        digest = self.image[-32:]
        head = self.image[:self.end]
        flags = bytearray(self.image)
        flags[16] = 1
        longer = bytearray(self.image)
        struct.pack_into("<I", longer, 12, 4000)
        no_magic = bytearray(self.image)
        no_magic[self.end:self.end + 2] = b"\0\0"
        changed = bytearray(self.image)
        changed[600] ^= 0xFF
        cases = [
            (self.image[:500], "curta demais"),
            (bytes(flags), "Cabecalho MCUboot"),
            (bytes(longer), "truncada"),
            (bytes(no_magic), "Bloco de TLV"),
            (head + self.tlv((0x10, digest), total=44), "Bloco de TLV"),
            (head + struct.pack("<HH", can_upload.TLV_INFO_MAGIC, 7) + b"\x10\0\x20", "TLV truncado"),
            (head + struct.pack("<HHHH", can_upload.TLV_INFO_MAGIC, 40, 0x10, 100) + digest, "passa do fim"),
            (bytes(changed), "nao confere"),
            (head + self.tlv((0x10, digest[:16])), "nao confere"),
            (head + self.tlv((0x20, digest)), "exatamente um"),
            (head + self.tlv((0x10, digest), (0x10, digest)), "exatamente um"),
        ]
        for image, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(can_upload.UploadError, message):
                can_upload.check_image(image)
        self.assertEqual(can_upload.check_image(self.image), digest.hex())

    def test_check_application_rejects(self):
        misaligned = bytearray(self.app)
        struct.pack_into("<I", misaligned, 0, can_upload.STACK_TOP - 4)
        no_descriptor = bytearray(self.app)
        struct.pack_into("<I", no_descriptor, can_upload.APP_INFO_OFFSET, 0)
        for payload, message in ((self.app[:0x200 + 16], "pequena demais"), (bytes(misaligned), "SP inicial"),
                                 (bytes(no_descriptor), "Sem descritor")):
            with self.subTest(message=message), self.assertRaisesRegex(can_upload.UploadError, message):
                can_upload.check_application(payload)

    def hex_file(self, lines):
        path = self.path / "app.hex"
        path.write_text("\n".join(lines) + "\n")
        return path

    @staticmethod
    def record(kind, address, data):
        body = bytes([len(data), address >> 8, address & 0xFF, kind]) + data
        return ":" + (body + bytes([(-sum(body)) & 0xFF])).hex().upper()

    def test_read_hex(self):
        # linha sem ':', checksum errado, nada em 0x08010200 (ligado a 0x08000000)
        with self.assertRaisesRegex(can_upload.UploadError, "linha HEX invalida"):
            can_upload.read_hex(self.hex_file(["020000040801F1"]))
        bad = self.record(0, 0, b"\x01\x02")
        with self.assertRaisesRegex(can_upload.UploadError, "checksum errado"):
            can_upload.read_hex(self.hex_file([bad[:-2] + "00"]))
        with self.assertRaisesRegex(can_upload.UploadError, "nada a partir"):
            can_upload.read_hex(self.hex_file([self.record(4, 0, b"\x08\x00"), self.record(0, 0x0400, b"\x01\x02\x03\x04"),
                                               self.record(1, 0, b"")]))
        # segmento (tipo 2) abaixo de 1 MB ignorado, linhas vazias, buracos a 0xFF, nada depois do EOF
        data = self.hex_file([self.record(2, 0, b"\x10\x00"), self.record(0, 0, b"\xAA"), "",
                              self.record(4, 0, b"\x08\x01"), self.record(0, 0x0200, b"\x01\x02"),
                              self.record(0, 0x0206, b"\x07"), self.record(1, 0, b""), self.record(0, 0x0300, b"\x09")])
        self.assertEqual(can_upload.read_hex(data), b"\x01\x02\xff\xff\xff\xff\x07")

    def test_versions_and_files(self):
        self.assertEqual(can_upload.parse_version("1.2.3+45"), (1, 2, 3, 45))
        self.assertEqual(can_upload.parse_version("4.5.6"), (4, 5, 6, None))
        for text in ("1.2", "a.b.c", "1.2.3+x"):
            with self.subTest(text=text), self.assertRaisesRegex(can_upload.UploadError, "invalida"):
                can_upload.parse_version(text)
        with self.assertRaisesRegex(can_upload.UploadError, ".elf"):
            can_upload.prepare_image(self.path / "lart_bms.elf")

        source = self.path / "app.bin"
        source.write_bytes(self.app)
        _, version, packaged, _ = can_upload.prepare_image(source, "7.8.9+10")
        self.assertEqual((version, packaged), ((7, 8, 9, 10), False))
        _, version, _, _ = can_upload.prepare_image(source, "7.8.9", build=3)
        self.assertEqual(version, (7, 8, 9, 3))
        # sem build no descritor nem na linha de comando: a data do ficheiro (AAMMDDhhmm)
        os.utime(source, (1767225600, 1767225600))   # 2026-01-01 00:00 UTC
        _, version, _, _ = can_upload.prepare_image(source)
        self.assertEqual(len(str(version[3])), 10)
        self.assertTrue(str(version[3]).startswith(("2512", "2601")))


class LinkTest(unittest.TestCase):
    """O protocolo do lado do PC: repeticoes, timeouts, recusas, ruido no bus, diagnosticos."""

    def test_retries_then_gives_up(self):
        bus = FakeBus()
        with self.assertRaisesRegex(TimeoutError, "Sem resposta ao comando 0"):
            can_upload.Link(bus).request(can_upload.command(can_upload.INFO), timeout=0.01, retries=3)
        self.assertEqual(len(bus.sent), 3)
        self.assertTrue(all(msg.arbitration_id == 0x602 and not msg.is_extended_id for msg in bus.sent))

    def test_answer_on_the_second_try(self):
        bus = FakeBus(lambda msg: [response(0, value=0x30000)] if len(bus.sent) == 2 else [])
        self.assertEqual(can_upload.Link(bus).request(can_upload.command(can_upload.INFO), timeout=0.01), 0x30000)
        self.assertEqual(len(bus.sent), 2)

    def test_ignores_noise_and_other_answers(self):
        noise = [can.Message(arbitration_id=0x580, is_extended_id=True, data=bytes(8)),
                 can.Message(arbitration_id=0x580, is_extended_id=False, is_remote_frame=True),
                 can.Message(arbitration_id=0x580, is_extended_id=False, is_error_frame=True),
                 can.Message(arbitration_id=0x580, is_extended_id=False, data=bytes(4)),
                 response(1, value=0),          # resposta a outro comando
                 response(2, value=4),          # DATA com outro offset (repeticao atrasada)
                 response(2, value=8)]
        bus = FakeBus(lambda msg: noise)
        payload = can_upload.REQUEST.encode({"Command": can_upload.DATA, "ImageOffset": 4,
                                             **{f"PayloadByte{index}": index for index in range(4)}})
        self.assertEqual(can_upload.Link(bus).request(payload, expected=8, timeout=0.01), 8)

    def test_refusals(self):
        for status, message in ((5, "erro na flash"), (6, "imagem invalida"), (9, "erro 9")):
            bus = FakeBus(lambda msg, status=status: [response(msg.data[0], status=status)])
            with self.subTest(status=status), self.assertRaisesRegex(can_upload.UploadError, message):
                can_upload.Link(bus).request(can_upload.command(can_upload.BEGIN, 1000))
        bus = FakeBus(lambda msg: [response(msg.data[0], protocol=2)])
        with self.assertRaisesRegex(can_upload.UploadError, "protocolo"):
            can_upload.Link(bus).request(can_upload.command(can_upload.INFO))

    def test_diagnostics_are_shown_once(self):
        error = can_upload.DATABASE.get_message_by_name("BCI_BOOT_ERROR")
        status = can_upload.DATABASE.get_message_by_name("BCI_BOOT_UPLOAD_STATUS")
        frames = [can.Message(arbitration_id=0x581, is_extended_id=False, data=bytes([0x55, 1, 0, 0, 0, 1, 2, 3])),
                  can.Message(arbitration_id=0x581, is_extended_id=False, data=bytes([0x55, 2, 0, 0, 0, 1, 2, 3])),
                  can.Message(arbitration_id=error.frame_id, is_extended_id=False, data=bytes([16, 0, 0, 4, 0x21, 0, 0, 0])),
                  can.Message(arbitration_id=error.frame_id, is_extended_id=False, data=bytes([16, 0, 0, 4, 0x21, 0, 0, 0])),
                  can.Message(arbitration_id=status.frame_id, is_extended_id=False, data=bytes(8)),
                  can.Message(arbitration_id=0x123, is_extended_id=False, data=bytes(8)),
                  response(0, value=0x30000)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            can_upload.Link(FakeBus(lambda msg: frames)).request(can_upload.command(can_upload.INFO), timeout=0.01)
        text = output.getvalue()
        self.assertEqual(text.count("bootloader 1.2.3 ativo"), 1)
        self.assertEqual(text.count("[BCI_BOOT_ERROR]"), 1)
        self.assertNotIn("BCI_BOOT_UPLOAD_STATUS", text)   # o progresso so com -v
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            link = can_upload.Link(FakeBus(lambda msg: frames[4:]), verbose=True)
            link.request(can_upload.command(can_upload.INFO), timeout=0.01)
        self.assertIn("BCI_BOOT_UPLOAD_STATUS", output.getvalue())

    def test_bootloader_never_answers(self):
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(TimeoutError, "nao respondeu"):
            can_upload.enter_bootloader(can_upload.Link(FakeBus()), "none", 0.05)

    def test_upload_limits_and_reset_without_answer(self):
        image = can_upload.package(make_app(2000, 1, 0, 1), (1, 0, 0, 1))
        with self.assertRaisesRegex(can_upload.UploadError, "no maximo 100"):
            can_upload.upload(can_upload.Link(FakeBus()), image, 100)

        def device(msg):
            command = msg.data[0]
            if command == can_upload.BEGIN:
                return [response(1)]
            if command == can_upload.DATA:
                offset = msg.data[1] | (msg.data[2] << 8) | (msg.data[3] << 16)
                return [response(2, value=min(offset + 4, len(image)))]
            if command == can_upload.END:
                return [response(3, value=len(image), state=2)]
            return []   # o RESET perde-se
        output = io.StringIO()
        bus = FakeBus(device)
        with contextlib.redirect_stdout(output):
            can_upload.upload(can_upload.Link(bus), image, 0x30000)
        self.assertIn("RESET nao teve resposta", output.getvalue())
        data = b"".join(bytes(msg.data[4:8]) for msg in bus.sent if msg.data[0] == can_upload.DATA)
        self.assertEqual(data[:len(image)], image)
        self.assertEqual(bus.sent[-2].data[0], can_upload.END)
        self.assertEqual(struct.unpack_from("<I", bytes(bus.sent[-2].data), 1)[0], zlib.crc32(image))


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

    def test_status_and_reset_only(self):
        self.flash_v1()
        device = Device(self.library, "bms-status")
        device.start()
        output = io.StringIO()
        try:
            with contextlib.redirect_stdout(output):
                can_upload.main(["--status", "--interface", "virtual", "--channel", "bms-status", "--wait", "5"])
                self.assertTrue(device.in_bootloader)
                can_upload.main(["--reset", "--interface", "virtual", "--channel", "bms-status"])
        finally:
            device.stop()
        self.assertIn("Fica no bootloader", output.getvalue())
        self.assertIn("RESET enviado", output.getvalue())
        self.assertEqual(device.resets, 1)

    def test_command_line_errors(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                can_upload.main(["--interface", "virtual"])              # sem imagem
            with self.assertRaises(SystemExit):
                can_upload.main([str(self.write("app.bin", make_app(3000, 1, 0, 1)))])   # sem --channel
        # pela linha de comando: erros viram "ERRO: ..." e codigo 1
        result = subprocess.run([sys.executable, str(TOOLS / "can_upload.py"), str(self.path / "nao_existe.bin"), "--channel", "x"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("ERRO:", result.stderr)

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
