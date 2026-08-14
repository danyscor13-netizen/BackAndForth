"""Unit tests for the hosted runtimes (runtime/windows.ll and runtime/posix.ll).

These runtimes are hand-written LLVM IR, so nothing in the normal build type
checks them. This harness JIT-compiles each one with llvmlite and calls the
`@baf.*` entry points directly, which means the Windows runtime can be tested
on any platform: kernel32 is replaced by tests/hosted/mock-kernel32.c.

Run with `make test-hosted`, or directly:

    pip install llvmlite
    python3 tests/hosted/runtime_test.py
"""

import ctypes
import os
import subprocess
import sys
import warnings

warnings.simplefilter("ignore")

try:
    import llvmlite.binding as llvm
except ImportError:
    print("SKIP hosted runtime tests (pip install llvmlite to enable)")
    sys.exit(0)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BUILD = os.path.join(ROOT, "build", "tests")

llvm.initialize_native_target()
llvm.initialize_native_asmprinter()

FAILURES = []


def check(label, got, want):
    if got == want:
        print("PASS %s" % label)
    else:
        FAILURES.append(label)
        print("FAIL %s\n  expected: %r\n  actual:   %r" % (label, want, got))


def load(path):
    module = llvm.parse_assembly(open(path).read())
    module.verify()
    machine = llvm.Target.from_default_triple().create_target_machine()
    engine = llvm.create_mcjit_compiler(module, machine)
    engine.finalize_object()
    return engine


class BafStr(ctypes.Structure):
    _fields_ = [("ptr", ctypes.c_void_p), ("len", ctypes.c_int64)]


def make_str(text):
    raw = text.encode()
    buf = ctypes.create_string_buffer(raw)
    return BafStr(ctypes.cast(buf, ctypes.c_void_p).value, len(raw)), buf


# --------------------------------------------------------------------------
# runtime/windows.ll, against a mock kernel32
# --------------------------------------------------------------------------

def test_windows():
    print("\n== runtime/windows.ll ==")
    os.makedirs(BUILD, exist_ok=True)
    mock_path = os.path.join(BUILD, "mock-kernel32.so")
    subprocess.check_call([
        os.environ.get("CC", "cc"), "-shared", "-fPIC", "-O1",
        os.path.join(HERE, "mock-kernel32.c"), "-o", mock_path,
    ])
    mock = ctypes.CDLL(mock_path)

    for name in ("GetStdHandle", "WriteFile", "ReadFile",
                 "SetConsoleTextAttribute", "GetConsoleScreenBufferInfo",
                 "FillConsoleOutputCharacterA", "FillConsoleOutputAttribute",
                 "SetConsoleCursorPosition"):
        addr = ctypes.cast(getattr(mock, name), ctypes.c_void_p).value
        llvm.add_symbol(name, addr)

    engine = load(os.path.join(ROOT, "runtime", "windows.ll"))

    def fn(name, restype, argtypes):
        return ctypes.CFUNCTYPE(restype, *argtypes)(
            engine.get_function_address(name))

    putsc = fn("baf.putsc", None, [BafStr])
    putl = fn("baf.putl", None, [BafStr])
    put_int = fn("baf.put.int", None, [ctypes.c_int64])
    put_bool = fn("baf.put.bool", None, [ctypes.c_bool])
    read = fn("baf.input.read", BafStr, [ctypes.c_void_p, ctypes.c_int64])
    str_eq = fn("baf.str.eq", ctypes.c_bool, [BafStr, BafStr])
    set_fg = fn("baf.console.set_text_color", None, [ctypes.c_int64])
    set_bg = fn("baf.console.set_background_color", None, [ctypes.c_int64])
    clear = fn("baf.console.clear", None, [])

    def output():
        buf = ctypes.create_string_buffer(65536)
        # .raw snapshots a copy, so the fill must happen on its own line.
        length = mock.mock_out(buf)
        return buf.raw[:length].decode()

    mock.mock_reset(); text, _keep = make_str("Hello, World!"); putsc(text)
    check("win putsc appends newline", output(), "Hello, World!\n")

    mock.mock_reset(); text, _keep = make_str("bare"); putl(text)
    check("win putl omits newline", output(), "bare")

    mock.mock_reset(); put_int(42); put_int(-7); put_int(0)
    check("win put.int", output(), "42-70")

    mock.mock_reset(); put_int(-9223372036854775808)
    check("win put.int int64 min", output(), "-9223372036854775808")

    mock.mock_reset(); put_bool(True); put_bool(False)
    check("win put.bool", output(), "truefalse")

    mock.mock_reset(); text, _keep = make_str(""); putsc(text)
    check("win putsc empty string", output(), "\n")

    def read_line(data, capacity=256):
        mock.mock_reset()
        mock.mock_set_input(data, len(data))
        buf = ctypes.create_string_buffer(capacity)
        result = read(ctypes.cast(buf, ctypes.c_void_p), capacity)
        return buf.raw[:result.len].decode(), result.len

    check("win inpt LF", read_line(b"hello\nworld\n"), ("hello", 5))
    check("win inpt CRLF", read_line(b"hello\r\nworld\r\n"), ("hello", 5))
    check("win inpt drops bare CR", read_line(b"he\rllo\r\n"), ("hello", 5))
    check("win inpt EOF without newline", read_line(b"tail"), ("tail", 4))
    check("win inpt empty line", read_line(b"\n"), ("", 0))
    check("win inpt empty CRLF", read_line(b"\r\n"), ("", 0))
    check("win inpt immediate EOF", read_line(b""), ("", 0))
    check("win inpt truncates at capacity", read_line(b"abcdefghij\n", 5),
          ("abcd", 4))
    check("win inpt CR near capacity", read_line(b"ab\r\rcd\n", 4), ("abc", 3))

    mock.mock_reset(); mock.mock_set_input(b"xy\r\n", 4)
    buf = ctypes.create_string_buffer(16); buf.raw = b"Z" * 16
    read(ctypes.cast(buf, ctypes.c_void_p), 16)
    check("win inpt NUL terminates", buf.raw[:4], b"xy\x00Z")

    # The 0.6.0 headline fix, checked on the hosted side.
    mock.mock_reset(); mock.mock_set_input(b"name\ncontent\n", 13)
    first = ctypes.create_string_buffer(256)
    second = ctypes.create_string_buffer(256)
    r1 = read(ctypes.cast(first, ctypes.c_void_p), 256)
    r2 = read(ctypes.cast(second, ctypes.c_void_p), 256)
    check("win independent inpt buffers",
          (first.raw[:r1.len], second.raw[:r2.len]), (b"name", b"content"))

    a, _1 = make_str("abc"); b, _2 = make_str("abc")
    c, _3 = make_str("abd"); d, _4 = make_str("ab")
    e, _5 = make_str(""); f, _6 = make_str("")
    check("win str.eq equal", str_eq(a, b), True)
    check("win str.eq differing byte", str_eq(a, c), False)
    check("win str.eq differing length", str_eq(a, d), False)
    check("win str.eq empty", str_eq(e, f), True)

    mock.mock_reset(); set_fg(10)
    check("win fg 10", mock.mock_attr(), 10)
    set_bg(1)
    check("win bg survives fg", mock.mock_attr(), (1 << 4) | 10)
    set_fg(15)
    check("win fg change keeps bg", mock.mock_attr(), (1 << 4) | 15)
    set_bg(0); set_fg(7)
    check("win default attribute", mock.mock_attr(), 7)
    set_fg(255)
    check("win fg masked to 4 bits", mock.mock_attr(), 15)
    set_bg(0xF0)
    check("win bg masked to 4 bits", mock.mock_attr(), 15)

    mock.mock_reset(); set_fg(2); set_bg(4); clear()
    check("win clear wipes 80x25", mock.mock_cleared(), 2000)
    check("win clear homes cursor", mock.mock_homed(), 1)
    check("win clear preserves colour", mock.mock_attr(), (4 << 4) | 2)

    mock.mock_reset(); mock.mock_fail_csbi(1); clear()
    check("win clear no-ops when redirected",
          (mock.mock_cleared(), mock.mock_homed()), (0, 0))


# --------------------------------------------------------------------------
# runtime/posix.ll ANSI colour mapping
# --------------------------------------------------------------------------

def test_posix_colors():
    print("\n== runtime/posix.ll ==")
    engine = load(os.path.join(ROOT, "runtime", "posix.ll"))
    set_fg = ctypes.CFUNCTYPE(None, ctypes.c_int64)(
        engine.get_function_address("baf.console.set_text_color"))
    set_bg = ctypes.CFUNCTYPE(None, ctypes.c_int64)(
        engine.get_function_address("baf.console.set_background_color"))
    clear = ctypes.CFUNCTYPE(None)(
        engine.get_function_address("baf.console.clear"))

    def capture(func, *args):
        read_fd, write_fd = os.pipe()
        saved = os.dup(1)
        os.dup2(write_fd, 1)
        try:
            func(*args)
        finally:
            os.dup2(saved, 1)
            os.close(write_fd)
            os.close(saved)
        data = os.read(read_fd, 4096)
        os.close(read_fd)
        return data

    esc = b"\x1b["
    # VGA numbering is not ANSI numbering; these pin the translation table.
    check("posix fg 0 black", capture(set_fg, 0), esc + b"30m")
    check("posix fg 1 blue -> 34", capture(set_fg, 1), esc + b"34m")
    check("posix fg 2 green -> 32", capture(set_fg, 2), esc + b"32m")
    check("posix fg 4 red -> 31", capture(set_fg, 4), esc + b"31m")
    check("posix fg 7 light grey -> 37", capture(set_fg, 7), esc + b"37m")
    check("posix fg 10 light green -> 92", capture(set_fg, 10), esc + b"92m")
    check("posix fg 14 yellow -> 93", capture(set_fg, 14), esc + b"93m")
    check("posix fg 15 white -> 97", capture(set_fg, 15), esc + b"97m")
    check("posix bg 1 blue -> 44", capture(set_bg, 1), esc + b"44m")
    check("posix bg 4 red -> 41", capture(set_bg, 4), esc + b"41m")
    check("posix bg 15 -> 107", capture(set_bg, 15), esc + b"107m")
    check("posix fg masked to 4 bits", capture(set_fg, 255), esc + b"97m")
    check("posix clear unchanged", capture(clear), esc + b"2J" + esc + b"H")


def main():
    test_windows()
    test_posix_colors()
    print("\n%d failure(s)" % len(FAILURES))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
