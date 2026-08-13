import os

Import("env")


LLVM_BIN = r"C:\Program Files\LLVM\bin"

LLVM_TOOLS = {
    "CC": '"%s"' % os.path.join(LLVM_BIN, "clang.exe"),
    "CXX": '"%s"' % os.path.join(LLVM_BIN, "clang++.exe"),
    "AR": '"%s"' % os.path.join(LLVM_BIN, "llvm-ar.exe"),
    "RANLIB": '"%s"' % os.path.join(LLVM_BIN, "llvm-ranlib.exe"),
}

# PlatformIO's native builder loads its GCC-flavoured SCons tools after pre
# scripts have run. Wrap that load so the command-line shape still comes from
# SCons while the actual executables are replaced before the build environment
# is cloned.
load_scons_tool = env.Tool


def load_scons_tool_with_llvm(tool, toolpath=None, **kwargs):
    result = load_scons_tool(tool, toolpath=toolpath, **kwargs)
    if tool in ("gcc", "g++"):
        env.Replace(**LLVM_TOOLS)
    return result


env.Tool = load_scons_tool_with_llvm
