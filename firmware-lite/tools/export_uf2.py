"""Package the XIAO SAMD21 application BIN for its UF2 USB bootloader.

UF2 format and SAMD21 family ID: https://github.com/microsoft/uf2
"""

from pathlib import Path
import struct

Import("env")


def export_uf2(source, target, env):
    image = Path(source[0].get_abspath()).read_bytes()
    address = int(env.BoardConfig().get("upload.offset_address"), 0)
    payload_size = 256
    block_count = (len(image) + payload_size - 1) // payload_size
    if not image or address != 0x2000 or address + block_count * payload_size > 0x40000:
        raise ValueError("SAMD21 UF2 must fit above the 8 KiB bootloader in 256 KiB flash")

    with Path(target[0].get_abspath()).open("wb") as output:
        for index in range(block_count):
            payload = image[index * payload_size : (index + 1) * payload_size]
            header = struct.pack(
                "<8I", 0x0A324655, 0x9E5D5157, 0x2000,
                address + index * payload_size, payload_size,
                index, block_count, 0x68ED2B88,
            )
            output.write(header)
            output.write(payload.ljust(476, b"\x00"))
            output.write(struct.pack("<I", 0x0AB16F30))


uf2_image = env.Command(
    "$BUILD_DIR/${PROGNAME}.uf2",
    "$BUILD_DIR/${PROGNAME}.bin",
    env.VerboseAction(export_uf2, "Building $TARGET"),
)
env.Depends(uf2_image, "checkprogsize")
env.Alias("buildprog", uf2_image)
env.Default(uf2_image)
