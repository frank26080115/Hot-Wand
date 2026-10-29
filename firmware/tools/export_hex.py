"""Generate an address-bearing Intel HEX alongside the standard BIN image."""

Import("env")

hex_image = env.ElfToHex("$BUILD_DIR/${PROGNAME}.hex", "$BUILD_DIR/${PROGNAME}.elf")
env.Depends(hex_image, "checkprogsize")
env.Alias("buildprog", hex_image)
env.Default(hex_image)
