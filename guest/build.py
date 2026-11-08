#!/usr/bin/env python3
"""Builds the guest-side (arm64 Mach-O) runtime libraries with zig's clang.

Outputs go to <out>/  (default: ../out/guest). Each library is a .dylib that the
host emulator maps into the guest address space next to the game binary.

    py guest/build.py [--out DIR] [--jobs N] [lib ...]
"""
import argparse
import concurrent.futures as cf
import glob
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

try:
    import ziglang  # noqa: F401
    ZIGDIR = os.path.dirname(ziglang.__file__)
except ImportError:  # pragma: no cover
    print("ziglang not found; installing it with pip ...", flush=True)
    if subprocess.call([sys.executable, "-m", "pip", "install", "ziglang"]) != 0:
        sys.exit("ziglang is required:  py -m pip install ziglang")
    import ziglang  # noqa: F401
    ZIGDIR = os.path.dirname(ziglang.__file__)

ZIG = os.path.join(ZIGDIR, "zig.exe" if os.name == "nt" else "zig")
ZLIB = os.path.join(ZIGDIR, "lib")
TARGET = "aarch64-macos.11.0"

COMMON = [
    "-target", TARGET, "-mcpu=apple_a7", "-fPIC", "-O2", "-g0",
    "-fno-stack-protector", "-fno-sanitize=all",
    "-Wno-everything",
    "-I", os.path.join(HERE, "include"),
]

CXX_LIB_FLAGS = [
    "-nostdinc++", "-std=c++23",
    "-include", os.path.join(HERE, "libcxx", "config_site.h"),
    "-I", os.path.join(ZLIB, "libcxx", "include"),
    "-I", os.path.join(ZLIB, "libcxxabi", "include"),
    "-I", os.path.join(ZLIB, "libunwind", "include"),
    "-I", os.path.join(ZLIB, "libcxx", "src"),
    "-I", os.path.join(ZLIB, "libcxx", "libc"),
    "-D_LIBCPP_BUILDING_LIBRARY",
    "-DLIBCXX_BUILDING_LIBCXXABI",
    "-D_LIBCXXABI_BUILDING_LIBRARY",
    "-D_LIBCPP_DISABLE_DEPRECATION_WARNINGS",
    "-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES",
    "-DNDEBUG",
    "-fvisibility-inlines-hidden",
    "-faligned-allocation",
    "-fexceptions", "-frtti",
]

OBJC_FLAGS = ["-fobjc-runtime=macosx-10.12", "-fno-objc-arc", "-fblocks", "-DOS_OBJECT_USE_OBJC=0"]


def libcxx_sources():
    src = []
    skip = {"print.cpp"}
    for f in sorted(glob.glob(os.path.join(ZLIB, "libcxx", "src", "*.cpp"))):
        if os.path.basename(f) in skip:
            continue
        src.append((f, CXX_LIB_FLAGS))
    for f in sorted(glob.glob(os.path.join(ZLIB, "libcxx", "src", "ryu", "*.cpp"))):
        src.append((f, CXX_LIB_FLAGS))
    for f in sorted(glob.glob(os.path.join(ZLIB, "libcxx", "src", "filesystem", "*.cpp"))):
        src.append((f, CXX_LIB_FLAGS + ["-I", os.path.join(ZLIB, "libcxx", "src", "filesystem")]))
    abi_skip = {"cxa_noexception.cpp"}
    for f in sorted(glob.glob(os.path.join(ZLIB, "libcxxabi", "src", "*.cpp"))):
        if os.path.basename(f) in abi_skip:
            continue
        src.append((f, CXX_LIB_FLAGS + ["-DHAVE___CXA_THREAD_ATEXIT_IMPL=0"]))
    uw = os.path.join(ZLIB, "libunwind", "src")
    uwflags = ["-I", os.path.join(ZLIB, "libunwind", "include"), "-D_LIBUNWIND_IS_NATIVE_ONLY",
               "-DNDEBUG", "-funwind-tables"]
    src.append((os.path.join(uw, "libunwind.cpp"), uwflags + ["-nostdinc++", "-std=c++17", "-fno-exceptions", "-fno-rtti",
                                                             "-include", os.path.join(HERE, "libcxx", "config_site.h"),
                                                             "-I", os.path.join(ZLIB, "libcxx", "include")]))
    for f in ("UnwindLevel1.c", "UnwindLevel1-gcc-ext.c"):
        src.append((os.path.join(uw, f), uwflags + ["-std=c11"]))
    for f in ("UnwindRegistersSave.S", "UnwindRegistersRestore.S"):
        src.append((os.path.join(uw, f), uwflags))
    src.append((os.path.join(HERE, "libcxx", "compat.cpp"), CXX_LIB_FLAGS))
    return src


def tree(sub, exts=(".c", ".m", ".cpp", ".mm", ".S", ".s"), extra=()):
    out = []
    d = os.path.join(HERE, sub)
    for dp, _, fs in os.walk(d):
        for f in sorted(fs):
            if os.path.splitext(f)[1] in exts:
                p = os.path.join(dp, f)
                fl = []
                if f.endswith((".m", ".mm")):
                    fl += OBJC_FLAGS
                fl += list(extra)
                if f.endswith((".cpp", ".mm")):
                    fl += ["-std=c++20", "-fno-exceptions", "-fno-rtti"]
                if f.endswith((".c", ".m")):
                    fl += ["-std=gnu11"]
                out.append((p, fl))
    return out


def zlib_sources():
    zd = os.path.join(ROOT, "external", "zlib")
    names = ["adler32", "compress", "crc32", "deflate", "infback", "inffast", "inflate", "inftrees", "trees", "uncompr", "zutil",
             "gzclose", "gzlib", "gzread", "gzwrite"]
    return [(os.path.join(zd, n + ".c"), ["-std=gnu11", "-DHAVE_UNISTD_H", "-DHAVE_STDARG_H", "-I", zd]) for n in names]


# name -> (install name, sources fn, link flags, dependencies)
LIBS = {
    "libc++": ("/usr/lib/libc++.1.dylib", libcxx_sources, []),
    "libobjc": ("/usr/lib/libobjc.A.dylib", lambda: tree("libobjc", extra=["-fno-objc-arc"]), []),
    "libz": ("/usr/lib/libz.1.dylib", zlib_sources, []),
    "libdispatch": ("/usr/lib/system/libdispatch.dylib", lambda: tree("libdispatch", extra=["-fblocks"]), []),
    "Foundation": ("/System/Library/Frameworks/Foundation.framework/Foundation", lambda: tree("Foundation"), []),
    "UIKit": ("/System/Library/Frameworks/UIKit.framework/UIKit", lambda: tree("UIKit"), []),
}
ORDER = ["libc++", "libobjc", "libz", "libdispatch", "Foundation", "UIKit"]


def run(cmd):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    return p.returncode, p.stdout


def compile_one(args):
    src, flags, objdir = args
    h = hashlib.sha1((src + "|" + " ".join(COMMON + flags)).encode()).hexdigest()[:10]
    obj = os.path.join(objdir, os.path.splitext(os.path.basename(src))[0] + "_" + h + ".o")
    dep_ok = os.path.exists(obj) and os.path.getmtime(obj) >= os.path.getmtime(src)
    if dep_ok and not src.startswith(HERE):
        return obj, 0, ""
    if dep_ok:
        # header changes inside guest/ invalidate everything in guest/ (cheap & safe)
        newest = max(os.path.getmtime(p) for p in glob.glob(os.path.join(HERE, "**", "*.h"), recursive=True))
        if os.path.getmtime(obj) >= newest:
            return obj, 0, ""
    lang = []
    if src.endswith(".cpp"):
        lang = ["-x", "c++"]
    elif src.endswith(".mm"):
        lang = ["-x", "objective-c++"]
    cmd = [ZIG, "cc"] + COMMON + lang + flags + ["-c", src, "-o", obj]
    rc, out = run(cmd)
    return obj, rc, out


def build(name, outdir, jobs):
    install, srcfn, ldflags = LIBS[name]
    srcs = srcfn()
    if not srcs:
        print(f"[{name}] no sources, skipped")
        return True
    objdir = os.path.join(outdir, "obj", name)
    os.makedirs(objdir, exist_ok=True)
    objs, ok = [], True
    with cf.ThreadPoolExecutor(jobs) as ex:
        for obj, rc, out in ex.map(compile_one, [(s, f, objdir) for s, f in srcs]):
            if rc != 0:
                ok = False
                print(out)
            elif out.strip():
                print(out.strip()[:2000])
            objs.append(obj)
    if not ok:
        print(f"[{name}] compile FAILED")
        return False
    dylib = os.path.join(outdir, name + ".dylib")
    cmd = [ZIG, "cc", "-target", TARGET, "-mcpu=apple_a7", "-shared", "-nostdlib++",
           "-Wl,-undefined,dynamic_lookup", "-Wl,-install_name," + install,
           "-o", dylib] + objs + ldflags
    rc, out = run(cmd)
    if rc != 0:
        print(out)
        print(f"[{name}] link FAILED")
        return False
    if out.strip():
        print(out.strip()[:3000])
    print(f"[{name}] -> {dylib}")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "out", "guest"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("libs", nargs="*")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    libs = a.libs or ORDER
    for n in libs:
        if not build(n, a.out, a.jobs):
            sys.exit(1)


if __name__ == "__main__":
    main()
