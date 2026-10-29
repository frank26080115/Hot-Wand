"""Export ESP32-C3 factory images and standalone flashing release packages."""

from pathlib import Path
import hashlib
import json
import re
import subprocess
import zipfile

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
build = Path(env.subst("$BUILD_DIR"))
# Ask the compiler for the effective pin-map selection, including build overrides.
compiler = env.WhereIs(env.subst("$CXX"))
flags = [str(flag) for flag in env.subst_list("$_CPPDEFFLAGS $_CPPINCFLAGS")[0]]
macros = subprocess.check_output(
    [compiler, *flags, "-dM", "-E", "-x", "c++", "-"],
    input=('#include "' + (project / "inc/pins.h").as_posix() + '"\n').encode(), env=env["ENV"],
).decode()
pcb_match = re.search(r"^#define HOTWANDLITE_PCB_VERSION\s+(-?\d+)\s*$", macros, re.M)
if pcb_match is None:
    raise ValueError("Cannot determine the compiled PCB version")
pcb = int(pcb_match[1])
version_match = re.search(
    r'firmware_version\s*\[\s*\]\s*=\s*("(?:[^"\\]|\\.)*")',
    (project / "src/version.c").read_text(encoding="utf-8"),
)
if version_match is None:
    raise ValueError("Cannot determine the firmware version")
version = json.loads(version_match[1])
safe_version = re.sub(r"[^A-Za-z0-9._-]+", "-", version).strip(".-")
if not safe_version:
    raise ValueError("Firmware version must contain a filename-safe identifier")
module = "xiao-esp32c3" if env.BoardConfig().id == "seeed_xiao_esp32c3" and "HOTWANDLITE_TARGET_ESP32C3_SUPERMINI" not in macros else "esp32c3-supermini"
pcb_label = "pcb-minus1-reworked" if pcb == -1 else "pcb%d" % pcb
name = "hotwandlite-%s-%s-%s" % (module, pcb_label, safe_version)
factory = build / (name + ".factory.bin")
archive = build / (name + ".zip")
esptool = Path(env.PioPlatform().get_package_dir("tool-esptoolpy")) / "esptool.py"
images = [(int(env.subst(str(offset)), 0), Path(env.subst(str(path))))
          for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]
images.append((int(env.subst("$ESP32_APP_OFFSET"), 0), build / env.subst("${PROGNAME}.bin")))
images.sort()
flash_mode = env.subst("${__get_board_flash_mode(__env__)}")
flash_freq = env.subst("${__get_board_f_image(__env__)}")
flash_size = env.BoardConfig().get("upload.flash_size", "4MB")


def package_release(source, target, env):
    end = 0
    for offset, path in images:
        if offset < end:
            raise ValueError("Overlapping factory image: %s" % path)
        end = offset + path.stat().st_size
    command = [env.subst("$PYTHONEXE"), str(esptool), "--chip", "esp32c3",
               "merge_bin", "--flash_mode", flash_mode, "--flash_freq", flash_freq,
               "--flash_size", flash_size, "--target-offset", "0x0", "-o", str(factory)]
    for offset, path in images:
        command.extend([hex(offset), str(path)])
    subprocess.run(command, check=True, env=env["ENV"])
    sha256 = hashlib.sha256(factory.read_bytes()).hexdigest()
    instructions = """Hot Wand Lite - ESP32-C3 factory firmware

Module: {module}
Compiled PCB version: {pcb}
Firmware version: {version}
PlatformIO environment: {environment}
Factory image: {filename}
Flash address: 0x0
Flash settings: {flash_mode}, {flash_freq}, {flash_size}
SHA-256: {sha256}

This package contains a complete installation image: bootloader, partition
table, boot-app initialization data, and the application. Use it only with
the module and PCB wiring listed above. Factory installation can replace
existing settings and data in the programmed flash range.

1. Disconnect the Lite board's main power and remove the module from the PCB.
2. Connect the module using a USB data cable. Close serial monitors.
3. Open https://espressif.github.io/esptool-js/ in Chrome or Edge.
4. Connect to the module's USB serial port. If it will not connect, hold BOOT,
   press and release RESET (or reconnect USB), then release BOOT and retry.
5. Select the factory BIN from this ZIP and set its flash address to 0x0.
6. Program the image. After completion, reset or reconnect the module.
7. The serial command 'version' reports the firmware and compiled PCB version.

Command-line alternative (requires Python and esptool):
  python -m pip install esptool
  python -m esptool --chip esp32c3 --port COM7 write-flash 0x0 "{filename}"
Replace COM7 with the module's actual port. Older esptool 4.x uses write_flash
instead of write-flash. Do not flash the application-only firmware.bin at 0x0.
""".format(module=module, pcb=pcb, version=version, environment=env.subst("$PIOENV"),
           filename=factory.name, flash_mode=flash_mode, flash_freq=flash_freq,
           flash_size=flash_size, sha256=sha256)
    metadata = {"module": module, "pcb_version": pcb, "firmware_version": version,
                "environment": env.subst("$PIOENV"), "flash_address": "0x0",
                "flash_mode": flash_mode, "flash_frequency": flash_freq,
                "flash_size": flash_size, "sha256": sha256,
                "images": [{"offset": hex(offset), "file": path.name,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                           for offset, path in images]}
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        output.write(factory, factory.name)
        output.writestr("FLASHING.txt", instructions)
        output.writestr("build-info.json", json.dumps(metadata, indent=2) + "\n")
        output.writestr("SHA256SUMS.txt", "%s  %s\n" % (sha256, factory.name))


deps = [str(path) for _, path in images]
deps += [str(project / "src/version.c"), str(project / "tools/package_esp32c3.py")]
# Values also invalidate the package if options change without changing image bytes.
deps.append(env.Value(json.dumps([module, pcb, version, flash_mode, flash_freq, flash_size])))
release = env.Command([str(factory), str(archive)], deps,
                      env.VerboseAction(package_release, "Packaging ESP32-C3 factory firmware"))
env.Depends(release, "checkprogsize")
env.Alias("buildprog", release)
env.Default(release)
