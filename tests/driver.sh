#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK="$ROOT/build/driver-tests"
rm -rf "$WORK"
mkdir -p "$WORK"

validate_image() {
    python3 - "$1" <<'PY'
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
if len(data) < 32:
    raise SystemExit("image is too small")
magic, flags, checksum, header_addr, load_addr, load_end, bss_end, entry = struct.unpack_from("<8I", data, 0)
assert magic == 0x1BADB002
assert (magic + flags + checksum) & 0xFFFFFFFF == 0
assert flags & 0x00010000
assert header_addr == load_addr
assert load_end - load_addr == len(data)
assert bss_end >= load_end
assert load_addr <= entry < load_end
PY
}

cd "$WORK"

# Derive the version from the compiler rather than hardcoding it, so a release
# bump does not require editing this file. What matters is that the three
# binaries agree.
BAF_VERSION=$("$ROOT/build/bafc" --version | sed 's/^BackAndForth compiler //')
[ -n "$BAF_VERSION" ]
[ "$("$ROOT/baf" --version)" = "BackAndForth $BAF_VERSION" ]
[ "$("$ROOT/bafb" --version)" = "BackAndForthBootable $BAF_VERSION" ]
printf 'PASS version_reporting\n'

"$ROOT/baf" "$ROOT/examples/hello.baf" >/dev/null
[ -s a.ll ]
grep -q 'define.*@main' a.ll
printf 'PASS baf_default_ir\n'

"$ROOT/baf" "$ROOT/examples/hello.baf" -o custom.ll >/dev/null
[ -s custom.ll ]
printf 'PASS baf_custom_ir\n'

"$ROOT/baf" "$ROOT/examples/kernel32_hello.baf" --osDev >/dev/null
[ -s a.o ]
validate_image a.o
printf 'PASS baf_osdev_default_image\n'

"$ROOT/baf" "$ROOT/examples/kernel32_hello.baf" --osDev -o custom.o >/dev/null
[ -s custom.o ]
validate_image custom.o
printf 'PASS baf_osdev_custom_image\n'

cat > fake-qemu <<'SH'
#!/bin/sh
printf '%s\n' "$@" > "${FAKE_QEMU_LOG:?}"
SH
chmod +x fake-qemu
FAKE_QEMU_LOG="$WORK/qemu.args" QEMU="$WORK/fake-qemu" \
    "$ROOT/baf" custom.o --run

grep -qx -- '-kernel' qemu.args
grep -qx -- 'custom.o' qemu.args
grep -qx -- '-display' qemu.args
grep -qx -- 'gtk' qemu.args
printf 'PASS baf_run_image\n'


truncate -s 1048576 "$WORK/test-disk.img"
FAKE_QEMU_LOG="$WORK/qemu-disk.args" QEMU="$WORK/fake-qemu" \
    "$ROOT/baf" custom.o --run --disk "$WORK/test-disk.img"
grep -q -- 'if=ide' "$WORK/qemu-disk.args"
printf 'PASS baf_run_ide_disk
'

FAKE_QEMU_LOG="$WORK/qemu-ahci.args" QEMU="$WORK/fake-qemu" \
    "$ROOT/baf" custom.o --run --disk "$WORK/test-disk.img" --ahci
grep -q -- 'ich9-ahci' "$WORK/qemu-ahci.args"
grep -q -- 'bafahci.0' "$WORK/qemu-ahci.args"
printf 'PASS baf_run_ahci_disk
'


# --exe: hosted native linking. Uses a fake clang so the test does not depend
# on a real LLVM toolchain being present; the real link is covered by
# tests/run.sh and tests/hosted/runtime_test.py.
cat > "$WORK/fake-clang" <<'SH'
#!/bin/sh
printf '%s\n' "$@" > "$FAKE_CLANG_LOG"
# Emit something at -o so the driver's success path is exercised.
while [ "$#" -gt 0 ]; do
    if [ "$1" = "-o" ]; then printf '#!/bin/sh\necho linked\n' > "$2"; chmod +x "$2"; fi
    shift
done
SH
chmod +x "$WORK/fake-clang"

printf 'begin {\n    putsc("exe")\n}\n' > "$WORK/exe.baf"

FAKE_CLANG_LOG="$WORK/clang.args" CLANG="$WORK/fake-clang" \
    "$ROOT/baf" "$WORK/exe.baf" --exe -o "$WORK/exe.bin" > "$WORK/exe.out"

grep -q 'wrote' "$WORK/exe.out"
[ -x "$WORK/exe.bin" ]
grep -q 'runtime/posix.ll' "$WORK/clang.args"
grep -q 'program.ll' "$WORK/clang.args"
# The temporary IR path must not leak into the driver's own output.
if grep -q 'program.ll' "$WORK/exe.out"; then
    echo 'FAIL baf_exe (temporary IR path leaked to stdout)' >&2
    exit 1
fi
printf 'PASS baf_exe\n'

# --exe and --osDev are mutually exclusive.
if "$ROOT/baf" "$WORK/exe.baf" --exe --osDev > "$WORK/both.out" 2>&1; then
    echo 'FAIL baf_exe_osdev_rejected (driver accepted both flags)' >&2
    exit 1
fi
grep -q 'mutually exclusive' "$WORK/both.out"
printf 'PASS baf_exe_osdev_rejected\n'

# --exe --run executes the linked program instead of booting QEMU.
FAKE_CLANG_LOG="$WORK/clang2.args" CLANG="$WORK/fake-clang" \
    "$ROOT/baf" "$WORK/exe.baf" --exe --run -o "$WORK/exe2.bin" > "$WORK/run.out"
grep -q 'linked' "$WORK/run.out"
printf 'PASS baf_exe_run\n'
