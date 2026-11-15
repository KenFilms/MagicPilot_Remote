"""PlatformIO "merged" target: one flash image for the browser installer.

    pio run -e m5stack-core2 -t merged
    pio run -e m5stack-cores3 -t merged

Merges the bootloader, partition table, boot_app0 and the application into
docs/firmware/MagicPilot_Remote_<env>.bin. The image is written at offset 0x0,
which is what docs/firmware/manifest.json and ESP Web Tools expect. The flash
settings are the same ones PlatformIO uses for a regular USB upload.
"""

import os
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO)


def merge_firmware(target, source, env):
    board = env.BoardConfig()
    output_dir = os.path.join(env.subst("$PROJECT_DIR"), "docs", "firmware")
    os.makedirs(output_dir, exist_ok=True)
    output = os.path.join(output_dir, env.subst("MagicPilot_Remote_${PIOENV}.bin"))

    command = [
        env.subst("$PYTHONEXE"),
        os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py"),
        "--chip", board.get("build.mcu"),
        "merge_bin",
        "-o", output,
        "--flash_mode", env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash_freq", env.subst("${__get_board_f_image(__env__)}"),
        "--flash_size", board.get("upload.flash_size"),
    ]
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        command += [offset, env.subst(image)]
    command += [env.subst("$ESP32_APP_OFFSET"), str(source[0])]

    print("Merging firmware into %s" % os.path.relpath(output, env.subst("$PROJECT_DIR")))
    return subprocess.call(command)


env.AddCustomTarget(  # noqa: F821
    name="merged",
    dependencies="$BUILD_DIR/${PROGNAME}.bin",
    actions=merge_firmware,
    title="Merged firmware",
    description="Write a single flash image to docs/firmware for the browser installer",
)
