#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$ROOT/build/bafb-tests
rm -rf "$WORK"
mkdir -p "$WORK/bin"

INPUT=$WORK/bafOS.o
printf 'BAF-OS-IMAGE\n' > "$INPUT"

cat > "$WORK/bin/fake-grub-mkrescue" <<'FAKE'
#!/bin/sh
set -eu
OUT=
ROOT=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -o)
            OUT=$2
            shift 2
            ;;
        *)
            ROOT=$1
            shift
            ;;
    esac
done
[ -n "$OUT" ]
[ -n "$ROOT" ]
[ -f "$ROOT/boot/bafOS.o" ]
[ -f "$ROOT/boot/grub/grub.cfg" ]
grep -q 'multiboot /boot/bafOS.o' "$ROOT/boot/grub/grub.cfg"
cp "$ROOT/boot/grub/grub.cfg" "${BAFB_CAPTURE_CFG:?}"
printf 'FAKE ISO\n' > "$OUT"
FAKE
chmod +x "$WORK/bin/fake-grub-mkrescue"

OUTPUT=$WORK/bafOS.iso
BAFB_CAPTURE_CFG=$WORK/grub.cfg \
GRUB_MKRESCUE=$WORK/bin/fake-grub-mkrescue \
    "$ROOT/bafb" "$INPUT" -o "$OUTPUT" > "$WORK/output.txt"

[ -f "$OUTPUT" ]
[ -f "$WORK/grub.cfg" ]
grep -q "wrote $OUTPUT" "$WORK/output.txt"

if GRUB_MKRESCUE=$WORK/bin/fake-grub-mkrescue \
    "$ROOT/bafb" "$INPUT" > "$WORK/missing-output.txt" 2>&1; then
    echo 'FAIL bafb_missing_output' >&2
    exit 1
fi

if GRUB_MKRESCUE=$WORK/bin/fake-grub-mkrescue \
    "$ROOT/bafb" "$INPUT" -o "$WORK/not-an-iso.img" > "$WORK/bad-extension.txt" 2>&1; then
    echo 'FAIL bafb_extension' >&2
    exit 1
fi

printf 'PASS bafb\n'
