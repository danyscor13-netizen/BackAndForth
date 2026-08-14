CC ?= cc
CLANG ?= clang
LD_LLD ?= ld.lld
PYTHON ?= python3
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -g
CPPFLAGS ?= -Iinclude

SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))
COMPILER := build/bafc

.PHONY: all clean test test-driver test-bafb test-hosted test-language test-kernel32 check-kernel32 examples kernel32 run-kernel32 install install-windows uninstall path

all: $(COMPILER)

build:
	mkdir -p build

build/%.o: src/%.c include/baf.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(COMPILER): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@

examples: $(COMPILER)
	./$(COMPILER) examples/hello.baf -o build/hello.ll
	$(CLANG) -Wno-override-module build/hello.ll runtime/posix.ll -o build/hello
	./$(COMPILER) examples/named_args.baf -o build/named_args.ll
	$(CLANG) -Wno-override-module build/named_args.ll runtime/posix.ll -o build/named_args
	./$(COMPILER) examples/static_function.baf -o build/static_function.ll
	$(CLANG) -Wno-override-module build/static_function.ll runtime/posix.ll -o build/static_function
	./$(COMPILER) examples/variables.baf -o build/variables.ll
	$(CLANG) -Wno-override-module build/variables.ll runtime/posix.ll -o build/variables
	./$(COMPILER) examples/switch_demo.baf -o build/switch_demo.ll
	$(CLANG) -Wno-override-module build/switch_demo.ll runtime/posix.ll -o build/switch_demo
	./$(COMPILER) examples/control_flow.baf -o build/control_flow.ll
	$(CLANG) -Wno-override-module build/control_flow.ll runtime/posix.ll -o build/control_flow
	./$(COMPILER) examples/functions.baf -o build/functions.ll
	$(CLANG) -Wno-override-module build/functions.ll runtime/posix.ll -o build/functions
	./$(COMPILER) examples/strings.baf -o build/strings.ll
	$(CLANG) -Wno-override-module build/strings.ll runtime/posix.ll -o build/strings
	./$(COMPILER) examples/fizzbuzz.baf -o build/fizzbuzz.ll
	$(CLANG) -Wno-override-module build/fizzbuzz.ll runtime/posix.ll -o build/fizzbuzz

test: $(COMPILER)
	sh tests/run.sh
	sh tests/driver.sh
	sh tests/bafb.sh
	$(MAKE) test-hosted
	$(MAKE) test-language

# Type checks and unit tests the hand-written hosted runtimes, including the
# Windows one. Skips itself cleanly when llvmlite is not installed.
test-hosted:
	$(PYTHON) tests/hosted/runtime_test.py

# Compiles and JIT-runs the language test suite. Needs llvmlite, not clang.
test-language: $(COMPILER)
	$(PYTHON) tests/hosted/language_test.py

test-driver: $(COMPILER)
	sh tests/driver.sh

test-bafb:
	sh tests/bafb.sh

test-kernel32: $(COMPILER)
	CLANG="$(CLANG)" LD_LLD="$(LD_LLD)" sh tests/kernel32.sh

kernel32: $(COMPILER)
	CLANG="$(CLANG)" LD_LLD="$(LD_LLD)" ./scripts/baf-kernel32

check-kernel32: $(COMPILER)
	CLANG="$(CLANG)" LD_LLD="$(LD_LLD)" ./scripts/baf-check-i386

run-kernel32: $(COMPILER)
	CLANG="$(CLANG)" LD_LLD="$(LD_LLD)" ./scripts/baf-run-i386

clean:
	rm -rf build

PREFIX ?= /usr/local
LIBEXECDIR ?= $(PREFIX)/lib/backandforth
WINDIST ?= build/windows-dist
BINDIR ?= $(PREFIX)/bin

install: $(COMPILER)
	install -d "$(DESTDIR)$(LIBEXECDIR)/runtime" "$(DESTDIR)$(LIBEXECDIR)/arch/i386" "$(DESTDIR)$(BINDIR)"
	install -m 755 $(COMPILER) "$(DESTDIR)$(LIBEXECDIR)/bafc"
	install -m 644 runtime/i386-abi.ll runtime/i386-vga.ll runtime/i386-core.c runtime/i386-disk.c "$(DESTDIR)$(LIBEXECDIR)/runtime/"
	install -m 644 runtime/posix.ll runtime/windows.ll "$(DESTDIR)$(LIBEXECDIR)/runtime/"
	install -m 644 arch/i386/boot.S arch/i386/linker.ld "$(DESTDIR)$(LIBEXECDIR)/arch/i386/"
	install -d "$(DESTDIR)$(LIBEXECDIR)/editor"
	install -m 644 tools/editor/index.html "$(DESTDIR)$(LIBEXECDIR)/editor/"
	install -m 755 baf "$(DESTDIR)$(BINDIR)/baf"
	install -m 755 bafb "$(DESTDIR)$(BINDIR)/bafb"

# Stages a self-contained tree that can be copied to a Windows machine.
# Everything except bafc.exe, which must be built there with windows\make.bat.
install-windows:
	install -d "$(WINDIST)/runtime" "$(WINDIST)/arch/i386" \
	          "$(WINDIST)/src" "$(WINDIST)/include" \
	          "$(WINDIST)/windows"
	install -m 644 src/*.c "$(WINDIST)/src/"
	install -m 644 include/*.h "$(WINDIST)/include/"
	install -m 644 runtime/*.ll runtime/*.c "$(WINDIST)/runtime/"
	install -m 644 arch/i386/boot.S arch/i386/linker.ld "$(WINDIST)/arch/i386/"
	install -m 644 windows/* "$(WINDIST)/windows/"
	install -d "$(WINDIST)/installer" "$(WINDIST)/tools/editor" "$(WINDIST)/docs" "$(WINDIST)/examples"
	install -m 644 installer/* "$(WINDIST)/installer/"
	install -m 644 tools/editor/index.html "$(WINDIST)/tools/editor/"
	install -m 644 docs/*.md "$(WINDIST)/docs/"
	install -m 644 examples/*.baf "$(WINDIST)/examples/"
	install -m 644 README.md CHANGELOG.md LICENSE "$(WINDIST)/"
	@echo "Windows tree staged in $(WINDIST). On Windows run: windows\\make.bat"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/baf" "$(DESTDIR)$(BINDIR)/bafb"
	rm -rf "$(DESTDIR)$(LIBEXECDIR)"

path: all
	@sudo $(MAKE) install PREFIX=/usr/local
	@echo "BackAndForth installed in /usr/local. Run: hash -r"
