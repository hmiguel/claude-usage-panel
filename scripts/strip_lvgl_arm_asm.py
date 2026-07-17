"""
LVGL ships ARM-only NEON/Helium SIMD assembly under
src/draw/sw/blend/{neon,helium}/. These .S files aren't guarded correctly for
non-ARM toolchains and fail to assemble on Xtensa with errors like
"unknown opcode or format name 'typedef'". They're never used on ESP32-S3
(no ARM SIMD there), so this pre-build step removes them before compiling.
Checks both the Arduino-library-registry location (libdeps/) and the
ESP-IDF-component-manager location (managed_components/), since lvgl may
arrive via either path depending on the framework configuration.
"""

Import("env")
import os
import shutil
import glob

candidates = []

libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
pioenv = env.subst("$PIOENV")
candidates.append(os.path.join(libdeps_dir, pioenv, "lvgl", "src", "draw", "sw", "blend"))

project_dir = env.subst("$PROJECT_DIR")
for managed in glob.glob(os.path.join(project_dir, "managed_components", "lvgl*lvgl*")):
    candidates.append(os.path.join(managed, "src", "draw", "sw", "blend"))

for blend_dir in candidates:
    for arch_dir in ("neon", "helium", "arm2d"):
        path = os.path.join(blend_dir, arch_dir)
        if os.path.isdir(path):
            shutil.rmtree(path)
            print("strip_lvgl_arm_asm: removed", path)
