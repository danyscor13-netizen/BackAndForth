#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
mkdir -p build/tests

compile_and_run() {
    name=$1
    expected=$2
    ./build/bafc "examples/$name.baf" -o "build/tests/$name.ll" >/dev/null
    clang -Wno-override-module "build/tests/$name.ll" runtime/posix.ll -o "build/tests/$name"
    actual=$("build/tests/$name")
    if [ "$actual" != "$expected" ]; then
        printf 'FAIL %s\nexpected: [%s]\nactual:   [%s]\n' "$name" "$expected" "$actual" >&2
        exit 1
    fi
    printf 'PASS %s\n' "$name"
}

compile_and_run hello 'Hello, World!'
compile_and_run named_args 'arg 1!
arg 2!'
compile_and_run static_function 'hi'
compile_and_run variables 'integer math compiled successfully'
compile_and_run switch_demo '> Ok'
compile_and_run variadic_output 'answer=42 ready=true!'
compile_and_run include_demo 'shared include'
compile_and_run include_directory_demo 'shared include'

./build/bafc examples/variadic_input.baf -o build/tests/variadic_input.ll >/dev/null
clang -Wno-override-module build/tests/variadic_input.ll runtime/posix.ll -o build/tests/variadic_input
actual=$(printf 'hello\nworld\n' | build/tests/variadic_input)
expected='Name: Content: name=hello content=world'
if [ "$actual" != "$expected" ]; then
    printf 'FAIL variadic_input\nexpected: [%s]\nactual:   [%s]\n' "$expected" "$actual" >&2
    exit 1
fi
grep -q '@.baf.input.0' build/tests/variadic_input.ll
grep -q '@.baf.input.1' build/tests/variadic_input.ll
printf 'PASS variadic_input_buffers\n'

./build/bafc examples/disk_api.baf -o build/tests/disk_api.ll >/dev/null
clang -Wno-override-module build/tests/disk_api.ll runtime/posix.ll -o build/tests/disk_api
grep -q 'call void @baf.disk.scan' build/tests/disk_api.ll
grep -q 'call i64 @baf.disk.count' build/tests/disk_api.ll
grep -q 'call void @baf.disk.hex' build/tests/disk_api.ll
printf 'PASS disk_api
'

./build/bafc examples/disk_fs_api.baf -o build/tests/disk_fs_api.ll >/dev/null
clang -Wno-override-module build/tests/disk_fs_api.ll runtime/posix.ll -o build/tests/disk_fs_api
grep -q 'call i1 @baf.disk.write' build/tests/disk_fs_api.ll
grep -q 'call %baf.str @baf.disk.read' build/tests/disk_fs_api.ll
grep -q 'call i1 @baf.disk.rem' build/tests/disk_fs_api.ll
grep -q 'call void @baf.disk.files' build/tests/disk_fs_api.ll
grep -q 'call i1 @baf.disk.create_dir' build/tests/disk_fs_api.ll
grep -q 'call i1 @baf.disk.goto_dir' build/tests/disk_fs_api.ll
grep -q 'call %baf.str @baf.disk.get_dir' build/tests/disk_fs_api.ll
printf 'PASS disk_fs_api\n'

./build/bafc examples/kernel32_colors.baf -o build/tests/console_colors.ll >/dev/null
clang -Wno-override-module build/tests/console_colors.ll runtime/posix.ll -o build/tests/console_colors
grep -q 'call void @baf.console.set_text_color' build/tests/console_colors.ll
grep -q 'call void @baf.console.set_background_color' build/tests/console_colors.ll
printf 'PASS console_colors\n'

if ./build/bafc tests/variadic_named_rejected.baf --check >build/tests/reject.out 2>build/tests/reject.err; then
    echo 'FAIL variadic_named_rejected (compiler accepted invalid program)' >&2
    exit 1
fi
if ! grep -q "variadic function 'putsc' does not accept named arguments" build/tests/reject.err; then
    echo 'FAIL variadic_named_rejected (wrong diagnostic)' >&2
    cat build/tests/reject.err >&2
    exit 1
fi
printf 'PASS variadic_named_rejected
'

if ./build/bafc tests/include_cycle.baf --check >build/tests/cycle.out 2>build/tests/cycle.err; then
    echo 'FAIL include_cycle (compiler accepted circular include)' >&2
    exit 1
fi
if ! grep -q 'circular include detected' build/tests/cycle.err; then
    echo 'FAIL include_cycle (wrong diagnostic)' >&2
    cat build/tests/cycle.err >&2
    exit 1
fi
printf 'PASS include_cycle
'

./build/bafc examples/simple_editor.baf --target i386-freestanding \
    -o build/tests/simple_editor.ll >/dev/null
grep -q '@.baf.input.0' build/tests/simple_editor.ll
grep -q '@.baf.input.1' build/tests/simple_editor.ll
grep -q '@.baf.input.2' build/tests/simple_editor.ll
printf 'PASS simple_editor_input_lifetimes\n'
