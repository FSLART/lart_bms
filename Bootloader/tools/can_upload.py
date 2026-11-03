"""Grava o firmware do BMS master pelo CAN (bootloader em Bootloader/, BOOTLOADER.md).

Aceita o .bin do CubeIDE (Firmware/Debug/lart_bms.bin, ligado a 0x08010200),
o .hex, ou uma imagem MCUboot ja empacotada. Empacota (cabecalho de 512 B com
a versao do descritor da aplicacao + TLV com o SHA-256), pede ao BMS para
entrar no bootloader e envia pelo protocolo do bootloader da ACU (0x602/0x580).

  python can_upload.py Firmware/Debug/lart_bms.bin --interface pcan --channel PCAN_USBBUS1
  python can_upload.py lart_bms.bin --interface slcan --channel COM5 --bitrate 500000
  python can_upload.py lart_bms.bin --package-only lart_bms.mcuboot.bin
  python can_upload.py --status --interface socketcan --channel can0
"""
import argparse
from pathlib import Path
import hashlib
import struct
import sys
import time
import zlib

import cantools

DATABASE = cantools.database.load_file(Path(__file__).resolve().parents[1] / "bootloader.dbc", strict=True)
REQUEST = DATABASE.get_message_by_name("BootloaderRequest")
RESPONSE = DATABASE.get_message_by_name("BootloaderResponse")
ENTER_BOOT = DATABASE.get_message_by_name("ENTER_BOOT")
HEARTBEAT = DATABASE.get_message_by_name("BCI_BOOT_HEARTBEAT")
DIAGNOSTICS = {DATABASE.get_message_by_name(name).frame_id: DATABASE.get_message_by_name(name) for name in (
    "BCI_BOOT_INFO", "BCI_BOOT_FIRMWARE_VERSION", "BCI_BOOT_DECISION", "BCI_BOOT_UPLOAD_STATUS",
    "BCI_BOOT_ERROR", "BCI_BOOT_ROLLBACK", "BCI_BOOT_FLASH_ERROR", "BCI_BOOT_REJECTED_VERSION")}
START_PROGRAMMER_ID = 0x0DC   # powertrain_t26 Start_Programmer, programming_request = 1

INFO, BEGIN, DATA, END, RESET = range(5)
ERRORS = ("OK", "trama errada", "estado errado", "tamanho fora dos limites", "offset errado",
          "erro na flash", "imagem invalida (CRC, SHA-256, vetores ou placa)")

# Firmware/Core/boot/boot_layout.h
IMAGE_MAGIC = 0x96F3B83D
TLV_INFO_MAGIC = 0x6907
TLV_SHA256 = 0x10
HEADER_SIZE = 0x200
APP_ADDRESS = 0x08010200
RAM_START = 0x20000000
STACK_TOP = 0x2003FFE0          # os ultimos 32 B da RAM sao do bootloader
APP_INFO_OFFSET = 0x200
APP_INFO_MAGIC = 0x4F464E49     # "INFO"
BOARD_ID = 0x31534D42           # "BMS1"
IMAGE_MAX = 0x30000             # EXEC: 192 KB com cabecalho e TLV


class UploadError(Exception):
    pass


# ================== IMAGEM ==================

def read_hex(path):
    """Intel HEX -> bytes a partir de 0x08010200 (o resto e ignorado)."""
    memory = {}
    base = 0
    for number, line in enumerate(path.read_text().splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise UploadError(f"{path}:{number}: linha HEX invalida")
        record = bytes.fromhex(line[1:])
        count, address, kind = record[0], (record[1] << 8) | record[2], record[3]
        if sum(record) & 0xFF:
            raise UploadError(f"{path}:{number}: checksum errado")
        data = record[4:4 + count]
        if kind == 0:
            for index, byte in enumerate(data):
                memory[base + address + index] = byte
        elif kind == 4:
            base = ((data[0] << 8) | data[1]) << 16
        elif kind == 2:
            base = ((data[0] << 8) | data[1]) << 4
        elif kind == 1:
            break
    addresses = [address for address in memory if address >= APP_ADDRESS]
    if not addresses:
        raise UploadError(f"{path}: nada a partir de 0x{APP_ADDRESS:08X} (aplicacao ligada a 0x08000000?)")
    end = max(addresses) + 1
    return bytes(memory.get(address, 0xFF) for address in range(APP_ADDRESS, end))


def check_application(payload):
    """Vetores e descritor: o que o bootloader vai exigir (boot_image.c)."""
    if len(payload) < APP_INFO_OFFSET + 32:
        raise UploadError("A aplicacao e pequena demais para ter vetores e descritor")
    sp, pc = struct.unpack_from("<II", payload)
    if not (RAM_START < sp <= STACK_TOP) or sp & 7:
        raise UploadError(f"SP inicial 0x{sp:08X} invalido: o linker tem de acabar a RAM em 0x{STACK_TOP:08X} (256K - 32)")
    if not pc & 1 or not APP_ADDRESS <= (pc & ~1) < APP_ADDRESS + len(payload):
        raise UploadError(f"Reset_Handler 0x{pc:08X} fora da imagem: o .bin tem de estar ligado a 0x{APP_ADDRESS:08X} "
                          "(STM32F412RETX_FLASH.ld do Firmware); o firmware antigo (0x08000000) nao serve")
    magic, board, major, minor, revision, build, features = struct.unpack_from("<IIBBHII", payload, APP_INFO_OFFSET)
    if magic != APP_INFO_MAGIC:
        raise UploadError("Sem descritor da aplicacao em 0x08010400 (secao .app_info do linker)")
    if board != BOARD_ID:
        raise UploadError(f"Firmware de outra placa ({struct.pack('<I', board)!r}), nao do BMS")
    return (major, minor, revision, build), features


def check_image(image):
    """Imagem MCUboot completa: cabecalho, TLV e SHA-256 (devolve o SHA em hex)."""
    if len(image) < HEADER_SIZE + 8 + 40:
        raise UploadError("Imagem MCUboot curta demais")
    magic, _, header, protected, size, flags = struct.unpack_from("<IIHHII", image)
    if magic != IMAGE_MAGIC or header != HEADER_SIZE or protected or flags or size < 8:
        raise UploadError("Cabecalho MCUboot nao suportado (512 B, sem TLVs protegidos nem flags)")
    end = header + size
    if end + 4 > len(image):
        raise UploadError("Imagem truncada")
    tlv_magic, total = struct.unpack_from("<HH", image, end)
    if tlv_magic != TLV_INFO_MAGIC or total < 4 or end + total != len(image):
        raise UploadError("Bloco de TLV invalido (nao usar o imgtool --pad nem --confirm)")
    expected = hashlib.sha256(image[:end]).digest()
    found = 0
    position = end + 4
    while position < len(image):
        if position + 4 > len(image):
            raise UploadError("TLV truncado")
        kind, length = struct.unpack_from("<HH", image, position)
        position += 4
        if position + length > len(image):
            raise UploadError("TLV passa do fim da imagem")
        if kind == TLV_SHA256:
            found += 1
            if length != 32 or image[position:position + length] != expected:
                raise UploadError("O SHA-256 do ficheiro nao confere com o TLV")
        position += length
    if found != 1:
        raise UploadError("Tem de haver exatamente um TLV SHA-256")
    check_application(image[HEADER_SIZE:end])
    return expected.hex()


def package(payload, version):
    major, minor, revision, build = version
    header = struct.pack("<IIHHIIBBHII", IMAGE_MAGIC, 0, HEADER_SIZE, 0, len(payload), 0,
                         major, minor, revision, build, 0).ljust(HEADER_SIZE, b"\0")
    body = header + payload
    return body + struct.pack("<HHHH", TLV_INFO_MAGIC, 40, TLV_SHA256, 32) + hashlib.sha256(body).digest()


def parse_version(text):
    try:
        main, _, build = text.partition("+")
        major, minor, revision = (int(part) for part in main.split("."))
        return major, minor, revision, int(build) if build else None
    except ValueError as error:
        raise UploadError(f"Versao '{text}' invalida: usar major.minor.revision[+build]") from error


def prepare_image(path, version_text=None, build=None):
    """Devolve (imagem MCUboot, versao, se ja vinha empacotada, SHA-256 em hex)."""
    if path.suffix.lower() == ".elf":
        raise UploadError("Usar o .bin (CubeIDE: Debug/lart_bms.bin) ou o .hex, nao o .elf")
    data = read_hex(path) if path.suffix.lower() == ".hex" else path.read_bytes()
    if len(data) >= 4 and struct.unpack_from("<I", data)[0] == IMAGE_MAGIC:
        digest = check_image(data)
        major, minor, revision, header_build = struct.unpack_from("<BBHI", data, 20)
        return data, (major, minor, revision, header_build), True, digest
    descriptor_version, _ = check_application(data)
    major, minor, revision, descriptor_build = descriptor_version
    if version_text:
        major, minor, revision, text_build = parse_version(version_text)
        build = text_build if text_build is not None else build
    if build is None:
        # sem build no descritor: a data do ficheiro (AAMMDDhhmm) distingue duas imagens com a mesma versao
        build = descriptor_build or int(time.strftime("%y%m%d%H%M", time.localtime(path.stat().st_mtime)))
    version = (major, minor, revision, build)
    image = package(data, version)
    if len(image) > IMAGE_MAX:
        raise UploadError(f"Imagem com {len(image)} B: o EXEC so leva {IMAGE_MAX} B. Compilar com -Og/-Os")
    return image, version, False, check_image(image)


# ================== CAN ==================

class Link:
    def __init__(self, bus, verbose=False):
        self.bus = bus
        self.verbose = verbose
        self.seen = {}

    def note(self, msg):
        """Heartbeat e diagnosticos que passem: mostra o que mudou."""
        if msg.arbitration_id in DIAGNOSTICS and len(msg.data) == 8:
            decoded = DIAGNOSTICS[msg.arbitration_id].decode(msg.data, decode_choices=True)
            text = ", ".join(f"{key}={value}" for key, value in decoded.items())
            name = DIAGNOSTICS[msg.arbitration_id].name
            if self.seen.get(name) != text and (self.verbose or name != "BCI_BOOT_UPLOAD_STATUS"):
                print(f"\n  [{name}] {text}", flush=True)
            self.seen[name] = text
        elif msg.arbitration_id == HEARTBEAT.frame_id and len(msg.data) == 8 and msg.data[0] == 0x55:
            if "heartbeat" not in self.seen:
                print(f"  bootloader {msg.data[5]}.{msg.data[6]}.{msg.data[7]} ativo (heartbeat 0x581)", flush=True)
            self.seen["heartbeat"] = True

    def send(self, arbitration_id, data):
        import can
        self.bus.send(can.Message(arbitration_id=arbitration_id, is_extended_id=False, data=data), timeout=1.0)

    def request(self, payload, expected=None, timeout=1.0, retries=3):
        for _ in range(retries):
            self.send(REQUEST.frame_id, payload)
            deadline = time.monotonic() + timeout
            while (remaining := deadline - time.monotonic()) > 0:
                msg = self.bus.recv(remaining)
                if msg is None:
                    break
                if msg.is_extended_id or msg.is_remote_frame or msg.is_error_frame or len(msg.data) != 8:
                    continue
                if msg.arbitration_id != RESPONSE.frame_id:
                    self.note(msg)
                    continue
                response = RESPONSE.decode(msg.data, decode_choices=False)
                if response["ReplyCommand"] != payload[0] | 0x80:
                    continue
                if response["ProtocolVersion"] != 1:
                    raise UploadError("Versao do protocolo do bootloader desconhecida")
                status = response["Status"]
                if status:
                    error = ERRORS[status] if status < len(ERRORS) else f"erro {status}"
                    raise UploadError(f"Comando {payload[0]} recusado: {error}")
                value = response["ValueBytes"]
                if expected is None or value == expected:
                    return value
        raise TimeoutError(f"Sem resposta ao comando {payload[0]}")


def command(cmd, value=0):
    field = {INFO: "InfoReserved", BEGIN: "ImageLength", END: "ImageCRC32", RESET: "ResetReserved"}[cmd]
    return REQUEST.encode({"Command": cmd, field: value})


def enter_bootloader(link, entry, wait_s):
    if entry == "programmer":
        link.send(START_PROGRAMMER_ID, bytes([1]))
        print("Enviado 0x0DC START_PROGRAMMER (programming_request = 1).")
    elif entry == "enter-boot":
        link.send(ENTER_BOOT.frame_id, ENTER_BOOT.encode({"BCI_BOOT": 1}))
        print("Enviado 0x601 ENTER_BOOT (so o CAN2 do BMS o aceita).")
    if entry != "none":
        print("O BMS so aceita com a HV desligada e fora de um carregamento.")
        time.sleep(0.4)   # a aplicacao espera 200 ms e o bootloader verifica as imagens
    print(f"A espera do bootloader (ate {wait_s} s; tambem apanha um reset/power-cycle manual)...")
    deadline = time.monotonic() + wait_s
    while True:
        try:
            return link.request(command(INFO), timeout=0.3, retries=1)
        except TimeoutError:
            if time.monotonic() >= deadline:
                raise TimeoutError("O bootloader nao respondeu: confirmar bus, bitrate, e se o BMS aceitou o pedido") from None


def progress(done, total):
    filled = 30 * done // total
    print(f"\rA gravar [{'#' * filled}{'-' * (30 - filled)}] {100 * done // total:3}% ({done}/{total} B)", end="", flush=True)


def upload(link, image, maximum, reset=True):
    digest = check_image(image)
    if len(image) > maximum:
        raise UploadError(f"Imagem com {len(image)} B; o bootloader aceita no maximo {maximum}")
    print("BEGIN: o BMS guarda o firmware atual no backup e apaga o EXEC (alguns segundos)...")
    link.request(command(BEGIN, len(image)), expected=0, timeout=20)
    started = time.monotonic()
    progress(0, len(image))
    last_percent = 0
    for offset in range(0, len(image), 4):
        chunk = image[offset:offset + 4].ljust(4, b"\xff")
        payload = REQUEST.encode({"Command": DATA, "ImageOffset": offset,
                                  **{f"PayloadByte{index}": byte for index, byte in enumerate(chunk)}})
        done = min(offset + 4, len(image))
        link.request(payload, expected=done)
        if 100 * done // len(image) != last_percent:
            last_percent = 100 * done // len(image)
            progress(done, len(image))
    elapsed = time.monotonic() - started
    print(f"\nEnviado em {elapsed:.0f} s ({len(image) / max(elapsed, 0.001) / 1024:.1f} KB/s). A validar...")
    link.request(command(END, zlib.crc32(image)), expected=len(image), timeout=20)
    print(f"Instalado e validado pelo BMS (SHA-256 {digest[:16]}...).")
    if not reset:
        print("Fica no bootloader (--no-reset): o RESET ou 30 s sem comandos arrancam a imagem nova.")
        return
    try:
        link.request(command(RESET), timeout=1, retries=1)
        print("RESET: o BMS arranca a imagem nova em teste. Se nao chegar a confirmar-se "
              "(boot_confirm_s a correr), volta sozinho ao firmware anterior.")
    except TimeoutError:
        print("Imagem instalada, mas o RESET nao teve resposta: reiniciar o BMS para a usar.")


def listen(link, seconds):
    deadline = time.monotonic() + seconds
    while (remaining := deadline - time.monotonic()) > 0:
        msg = link.bus.recv(remaining)
        if msg is not None and not msg.is_extended_id and len(msg.data) == 8:
            link.note(msg)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", type=Path, nargs="?", help=".bin/.hex da aplicacao ligada a 0x08010200, ou imagem MCUboot")
    parser.add_argument("--interface", default="slcan", help="backend do python-can (slcan, pcan, socketcan, kvaser, ...)")
    parser.add_argument("--channel", help="COM5, PCAN_USBBUS1, can0, ...")
    parser.add_argument("--bitrate", type=int, default=500000, help="bitrate do bus (por omissao 500000, o can1/can2_kbps da config)")
    parser.add_argument("--entry", choices=("programmer", "enter-boot", "none"), default="programmer",
                        help="como pedir o bootloader: 0x0DC (CAN1 ou CAN2), 0x601 (so CAN2), ou nada (ja esta no bootloader)")
    parser.add_argument("--wait", type=float, default=30.0, help="segundos a espera do bootloader")
    parser.add_argument("--version", dest="version_text", help="versao no cabecalho (major.minor.revision[+build]); por omissao a do descritor")
    parser.add_argument("--build", type=int, help="numero de build no cabecalho (por omissao AAMMDDhhmm do ficheiro)")
    parser.add_argument("--package-only", type=Path, metavar="SAIDA", help="so empacota a imagem, sem CAN")
    parser.add_argument("--status", action="store_true", help="so entra no bootloader, mostra o diagnostico e fica")
    parser.add_argument("--reset", action="store_true", help="so manda RESET (sair do bootloader)")
    parser.add_argument("--no-reset", action="store_true", help="nao reinicia no fim")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)

    image = None
    if args.image is not None:
        image, version, packaged, digest = prepare_image(args.image, args.version_text, args.build)
        major, minor, revision, build = version
        print(f"{args.image}: {'imagem MCUboot' if packaged else 'aplicacao'} v{major}.{minor}.{revision}+{build}, "
              f"{len(image)} B empacotada (SHA-256 {digest[:16]}...)")
        if args.package_only:
            args.package_only.write_bytes(image)
            print(f"Escrito {args.package_only}")
            return
    elif not (args.status or args.reset):
        parser.error("falta a imagem (ou --status / --reset)")
    if not args.channel:
        parser.error("falta --channel")

    import can
    with can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate) as bus:
        link = Link(bus, args.verbose)
        if args.reset:
            link.request(command(RESET), timeout=1, retries=1)
            print("RESET enviado: o BMS arranca a aplicacao.")
            return
        maximum = enter_bootloader(link, args.entry, args.wait)
        print(f"Bootloader pronto: aceita ate {maximum} B.")
        if image is None:
            listen(link, 1.0)
            print("Fica no bootloader: --reset para voltar a aplicacao (ou 5 min sem nada).")
            return
        upload(link, image, maximum, reset=not args.no_reset)


if __name__ == "__main__":
    try:
        main()
    except (OSError, UploadError, TimeoutError) as exc:
        sys.exit(f"ERRO: {exc}")
