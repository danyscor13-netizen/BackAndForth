"""JIT-runs a compiled BackAndForth program against runtime/posix.ll.

This is how the hosted end-to-end tests work on machines that have llvmlite but
no clang: the generated module and the hand-written runtime are linked in
memory and `main` is called directly.

    python3 tests/hosted/jit_run.py build/program.ll
"""

import ctypes
import os
import sys
import warnings

warnings.simplefilter("ignore")

try:
    import llvmlite.binding as llvm
except ImportError:
    print("SKIP (pip install llvmlite to enable)")
    sys.exit(0)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

try:
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
except Exception:  # newer llvmlite initialises itself
    pass


def run(path):
    program = llvm.parse_assembly(open(path).read())
    program.verify()
    runtime = llvm.parse_assembly(open(os.path.join(ROOT, "runtime", "posix.ll")).read())
    runtime.verify()
    program.link_in(runtime)
    program.verify()
    machine = llvm.Target.from_default_triple().create_target_machine()
    engine = llvm.create_mcjit_compiler(program, machine)
    engine.finalize_object()
    main = ctypes.CFUNCTYPE(ctypes.c_int)(engine.get_function_address("main"))
    sys.stdout.flush()
    return main()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: jit_run.py <module.ll>", file=sys.stderr)
        sys.exit(2)
    sys.exit(run(sys.argv[1]))
