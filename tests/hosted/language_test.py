"""End-to-end tests for the language front end and hosted code generation.

Each case is a small BackAndForth program plus the output it should produce.
The program is compiled with build/bafc, linked in memory against
runtime/posix.ll and executed, so the whole pipeline is covered without
needing clang on the machine.

    make test-language
    python3 tests/hosted/language_test.py
"""

import io
import os
import subprocess
import sys
import tempfile
import warnings

warnings.simplefilter("ignore")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BAFC = os.path.join(ROOT, "build", "bafc")

sys.path.insert(0, HERE)

try:
    import llvmlite.binding as llvm  # noqa: F401
except ImportError:
    print("SKIP language tests (pip install llvmlite to enable)")
    sys.exit(0)

import ctypes

import jit_run

FAILURES = []

CASES = [
    (
        "if / elsif / else",
        """
        func -> sign(n: int) : str {
            if (n > 0) { return "+" } elsif (n < 0) { return "-" } else { return "0" }
        }
        begin { putsc(sign(5) + sign(-5) + sign(0)) }
        """,
        "+-0\n",
    ),
    (
        "else if spelling",
        """
        begin {
            int n = 2
            if (n == 1) { putsc("one") } else if (n == 2) { putsc("two") } else { putsc("other") }
        }
        """,
        "two\n",
    ),
    (
        "arithmetic and precedence",
        """
        begin { putsc(2 + 3 * 4 - 10 / 5 + 7 % 4) }
        """,
        "15\n",
    ),
    (
        "division by zero is defined as zero",
        """
        begin { putsc((9 / 0) + (9 % 0)) }
        """,
        "0\n",
    ),
    (
        "comparisons and logic short circuit",
        """
        func -> boom() : bool {
            putl("!")
            return true
        }
        begin {
            putsc(1 < 2 && 2 <= 2 && 3 > 2 && 3 >= 3 && !(1 == 2) && 1 != 2)
            bool ignored = false && boom()
            bool taken = true || boom()
            putsc(taken)
        }
        """,
        "true\ntrue\n",
    ),
    (
        "string concatenation converts operands",
        """
        begin { putsc("v" + 7 + "/" + true) }
        """,
        "v7/true\n",
    ),
    (
        "string equality compares contents",
        """
        begin {
            str a = "abc"
            putsc((a == "abc") + " " + (a != "abc"))
        }
        """,
        "true false\n",
    ),
    (
        "while with break and continue",
        """
        begin {
            int i = 0
            int total = 0
            while (true) {
                i += 1
                if (i > 10) { break }
                if (i % 2 == 0) { continue }
                total += i
            }
            putsc(total)
        }
        """,
        "25\n",
    ),
    (
        "for loop with compound assignment",
        """
        begin {
            int product = 1
            for (int i = 1; i <= 5; i *= 2) { product += i }
            putsc(product)
        }
        """,
        "8\n",
    ),
    (
        "recursion through a returning function",
        """
        func -> fib(n: int) : int {
            if (n < 2) { return n }
            return fib(n - 1) + fib(n - 2)
        }
        begin { putsc(fib(15)) }
        """,
        "610\n",
    ),
    (
        "inferred return type",
        """
        func -> double(n: int) { return n + n }
        begin { putsc(double(21)) }
        """,
        "42\n",
    ),
    (
        "string library",
        """
        begin {
            str s = "BackAndForth"
            putsc(Str.Length(s) + " " + Str.Sub(s, 0, 4) + " " + Str.ToInt("-15"))
            putsc(Str.Concat("a", "b") + Str.FromInt(-9) + Str.FromBool(false))
        }
        """,
        "12 Back -15\nab-9false\n",
    ),
    (
        "math library",
        """
        begin { putsc(Math.Abs(-4) + " " + Math.Min(3, 8) + " " + Math.Max(3, 8)) }
        """,
        "4 3 8\n",
    ),
    (
        "switch still works and break leaves the loop",
        """
        begin {
            for (int i = 1; i <= 4; i += 1) {
                switch (i) {
                    case 3 { break }
                    default { putl(i + " ") }
                }
            }
            putsc("")
        }
        """,
        "1 2 \n",
    ),
    (
        "block comments and negative literals",
        """
        begin {
            /* this
               spans lines */
            int n = -3
            putsc(n * -2)
        }
        """,
        "6\n",
    ),
    (
        "nested loops keep their own break target",
        """
        begin {
            for (int a = 0; a < 3; a += 1) {
                for (int b = 0; b < 3; b += 1) {
                    if (b == 1) { break }
                    putl(a + "" + b + " ")
                }
            }
            putsc("")
        }
        """,
        "00 10 20 \n",
    ),
]

REJECTED = [
    ("break outside a loop", "begin { break }"),
    ("value returned from an explicitly void function",
     "func -> f() : void { return 1 }\nbegin { f() }"),
    ("missing return", "func -> f(n: int) : int { if (n > 0) { return 1 } }\nbegin { putsc(f(1)) }"),
    ("comparing different types", 'begin { putsc(1 == "a") }'),
    ("calling a void function as a value", "func -> f() { putsc(\"x\") }\nbegin { int n = f() }"),
    ("condition that is not a bool", "begin { while (1) { } }"),
    ("arithmetic on a bool", "begin { int n = true * 2 }"),
]


def compile_source(source, extra_args=()):
    with tempfile.TemporaryDirectory() as directory:
        source_path = os.path.join(directory, "case.baf")
        output_path = os.path.join(directory, "case.ll")
        with open(source_path, "w") as handle:
            handle.write(source)
        result = subprocess.run(
            [BAFC, source_path, "-o", output_path, *extra_args],
            capture_output=True,
            text=True,
        )
        ir = None
        if os.path.exists(output_path):
            ir = open(output_path).read()
        return result, ir


def run_ir(ir):
    """Runs a compiled module and captures whatever it writes to stdout."""
    with tempfile.NamedTemporaryFile("w", suffix=".ll", delete=False) as handle:
        handle.write(ir)
        path = handle.name
    read_fd, write_fd = os.pipe()
    saved = os.dup(1)
    os.dup2(write_fd, 1)
    try:
        jit_run.run(path)
        libc = ctypes.CDLL(None)
        libc.fflush(None)
    finally:
        os.dup2(saved, 1)
        os.close(saved)
        os.close(write_fd)
        captured = io.FileIO(read_fd, "r").readall().decode()
        os.unlink(path)
    return captured


def check(label, got, want):
    if got == want:
        print("PASS %s" % label)
    else:
        FAILURES.append(label)
        print("FAIL %s\n  expected: %r\n  actual:   %r" % (label, want, got))


def main():
    if not os.path.exists(BAFC):
        print("build/bafc is missing; run make first", file=sys.stderr)
        return 2

    for label, source, expected in CASES:
        result, ir = compile_source(source)
        if result.returncode != 0 or ir is None:
            check(label, "compile error: " + result.stderr.strip(), expected)
            continue
        try:
            check(label, run_ir(ir), expected)
        except Exception as error:  # noqa: BLE001
            check(label, "runtime error: %s" % error, expected)

    for label, source in REJECTED:
        result, _ = compile_source(source, ("--check",))
        if result.returncode != 0 and result.stderr.strip():
            print("PASS rejects %s" % label)
        else:
            FAILURES.append("rejects " + label)
            print("FAIL rejects %s (the compiler accepted it)" % label)

    # The freestanding target must stay valid too, on the same sources.
    for label, source, _ in CASES:
        result, ir = compile_source(source, ("--target", "i386-freestanding"))
        if result.returncode != 0 or ir is None:
            FAILURES.append("i386 " + label)
            print("FAIL i386 %s" % label)
            continue
        try:
            module = llvm.parse_assembly(ir)
            module.verify()
            print("PASS i386 IR %s" % label)
        except Exception as error:  # noqa: BLE001
            FAILURES.append("i386 " + label)
            print("FAIL i386 IR %s: %s" % (label, error))

    print()
    if FAILURES:
        print("%d failure(s)" % len(FAILURES))
        return 1
    print("all language tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
