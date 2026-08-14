; BackAndForth hosted Windows runtime.
;
; Implements the same 27 `@baf.*` entry points as runtime/posix.ll, but on top
; of kernel32 and the UCRT instead of the POSIX system calls.
;
; Target: x86_64-pc-windows (msvc or gnu). The Win32 entry points below use the
; default C calling convention, which is correct on x64. A 32-bit Windows build
; would need `x86_stdcallcc` on every kernel32 declaration.
;
; Differences from the POSIX runtime, all deliberate:
;   * Console.SetTextColor / SetTextBackgroundColor are really implemented.
;     VGA colour numbers 0-15 map 1:1 onto Windows console attributes, so the
;     same BAF program produces the same colours hosted and on bafOS.
;   * Console.Clear clears the real screen buffer instead of emitting an ANSI
;     escape that a plain conhost window would print literally.
;   * inpt() strips CR, so a CRLF line ending does not leave a stray '\r' at
;     the end of every string the program reads.
;   * The console attribute is restored before exit, so a program that dies
;     mid-colour does not leave the user's shell recoloured.

%baf.str = type { ptr, i64 }

; CONSOLE_SCREEN_BUFFER_INFO, 22 bytes:
;   COORD dwSize; COORD dwCursorPosition; WORD wAttributes;
;   SMALL_RECT srWindow; COORD dwMaximumWindowSize;
%baf.win.csbi = type { i16, i16, i16, i16, i16, i16, i16, i16, i16, i16, i16 }

@baf.host.newline = private constant [1 x i8] c"\0A"
@baf.host.disk.unavailable = private constant [30 x i8] c"Disk drivers require --osDev.\0A"
@baf.host.int.format = private constant [5 x i8] c"%lld\00"
@baf.host.int.buffer = internal global [32 x i8] zeroinitializer, align 1
@baf.host.true = private constant [4 x i8] c"true"
@baf.host.false = private constant [5 x i8] c"false"

; Scratch cell for the lpNumberOfBytes out-parameters. The language is
; single-threaded, so a shared slot is safe.
@baf.win.scratch = internal global i32 0, align 4

; Current colour state. 7 on 0 is the conhost default (light grey on black).
@baf.win.fg = internal global i16 7, align 2
@baf.win.bg = internal global i16 0, align 2

declare ptr @GetStdHandle(i32)
declare i32 @WriteFile(ptr, ptr, i32, ptr, ptr)
declare i32 @ReadFile(ptr, ptr, i32, ptr, ptr)
declare i32 @SetConsoleTextAttribute(ptr, i16)
declare i32 @GetConsoleScreenBufferInfo(ptr, ptr)
declare i32 @FillConsoleOutputCharacterA(ptr, i8, i32, i32, ptr)
declare i32 @FillConsoleOutputAttribute(ptr, i16, i32, i32, ptr)
declare i32 @SetConsoleCursorPosition(ptr, i32)
declare void @exit(i32) noreturn
declare i32 @snprintf(ptr, i64, ptr, ...)

; ---------------------------------------------------------------- primitives

define internal void @baf.win.write(ptr %data, i64 %len) {
entry:
  %handle = call ptr @GetStdHandle(i32 -11)
  %len32 = trunc i64 %len to i32
  %ignored = call i32 @WriteFile(
      ptr %handle, ptr %data, i32 %len32, ptr @baf.win.scratch, ptr null)
  ret void
}

; Push the cached foreground/background pair to the console.
define internal void @baf.win.apply_color() {
entry:
  %handle = call ptr @GetStdHandle(i32 -11)
  %fg = load i16, ptr @baf.win.fg, align 2
  %bg = load i16, ptr @baf.win.bg, align 2
  %bg.shifted = shl i16 %bg, 4
  %attribute = or i16 %bg.shifted, %fg
  %ignored = call i32 @SetConsoleTextAttribute(ptr %handle, i16 %attribute)
  ret void
}

; ------------------------------------------------------------------- output

define void @baf.putl(%baf.str %value) {
entry:
  %ptr = extractvalue %baf.str %value, 0
  %len = extractvalue %baf.str %value, 1
  call void @baf.win.write(ptr %ptr, i64 %len)
  ret void
}

define void @baf.putsc(%baf.str %value) {
entry:
  call void @baf.putl(%baf.str %value)
  call void @baf.win.write(ptr @baf.host.newline, i64 1)
  ret void
}

define void @baf.put.int(i64 %value) {
entry:
  %length32 = call i32 (ptr, i64, ptr, ...) @snprintf(
      ptr @baf.host.int.buffer, i64 32, ptr @baf.host.int.format, i64 %value)
  %length = sext i32 %length32 to i64
  call void @baf.win.write(ptr @baf.host.int.buffer, i64 %length)
  ret void
}

define void @baf.put.bool(i1 %value) {
entry:
  br i1 %value, label %yes, label %no
yes:
  call void @baf.win.write(ptr @baf.host.true, i64 4)
  ret void
no:
  call void @baf.win.write(ptr @baf.host.false, i64 5)
  ret void
}

define void @baf.put.newline() {
entry:
  call void @baf.win.write(ptr @baf.host.newline, i64 1)
  ret void
}

; -------------------------------------------------------------------- input

; Reads one line into %buffer. Stops at LF, drops CR entirely so that CRLF
; input does not produce a trailing '\r'. Always NUL-terminates.
define %baf.str @baf.input.read(ptr %buffer, i64 %capacity) {
entry:
  %has.space = icmp ugt i64 %capacity, 0
  br i1 %has.space, label %loop, label %empty

loop:
  %index = phi i64 [ 0, %entry ], [ %next, %keep ], [ %index.carry, %skip ]
  %limit = sub i64 %capacity, 1
  %full = icmp uge i64 %index, %limit
  br i1 %full, label %done.loop, label %read.one

read.one:
  %handle = call ptr @GetStdHandle(i32 -10)
  %slot = getelementptr inbounds i8, ptr %buffer, i64 %index
  %ok = call i32 @ReadFile(
      ptr %handle, ptr %slot, i32 1, ptr @baf.win.scratch, ptr null)
  %failed = icmp eq i32 %ok, 0
  br i1 %failed, label %done.read, label %check.count

check.count:
  %count = load i32, ptr @baf.win.scratch, align 4
  %eof = icmp eq i32 %count, 0
  br i1 %eof, label %done.read, label %check

check:
  %character = load i8, ptr %slot, align 1
  %newline = icmp eq i8 %character, 10
  br i1 %newline, label %done.check, label %check.cr

check.cr:
  %carriage = icmp eq i8 %character, 13
  br i1 %carriage, label %skip, label %keep

; CR: leave %index where it is so the byte is overwritten by the next read.
skip:
  %index.carry = phi i64 [ %index, %check.cr ]
  br label %loop

keep:
  %next = add i64 %index, 1
  br label %loop

done.loop:
  br label %done
done.read:
  br label %done
done.check:
  br label %done

done:
  %length = phi i64 [ %index, %done.loop ], [ %index, %done.read ], [ %index, %done.check ]
  %end = getelementptr inbounds i8, ptr %buffer, i64 %length
  store i8 0, ptr %end, align 1
  %result.0 = insertvalue %baf.str zeroinitializer, ptr %buffer, 0
  %result.1 = insertvalue %baf.str %result.0, i64 %length, 1
  ret %baf.str %result.1

empty:
  ret %baf.str zeroinitializer
}

; ------------------------------------------------------------------ strings

define i1 @baf.str.eq(%baf.str %left, %baf.str %right) {
entry:
  %left.ptr = extractvalue %baf.str %left, 0
  %left.len = extractvalue %baf.str %left, 1
  %right.ptr = extractvalue %baf.str %right, 0
  %right.len = extractvalue %baf.str %right, 1
  %same.len = icmp eq i64 %left.len, %right.len
  br i1 %same.len, label %loop, label %no

loop:
  %index = phi i64 [ 0, %entry ], [ %next, %equal ]
  %done = icmp uge i64 %index, %left.len
  br i1 %done, label %yes, label %compare

compare:
  %left.slot = getelementptr inbounds i8, ptr %left.ptr, i64 %index
  %right.slot = getelementptr inbounds i8, ptr %right.ptr, i64 %index
  %left.byte = load i8, ptr %left.slot, align 1
  %right.byte = load i8, ptr %right.slot, align 1
  %same = icmp eq i8 %left.byte, %right.byte
  br i1 %same, label %equal, label %no

equal:
  %next = add i64 %index, 1
  br label %loop

yes:
  ret i1 true
no:
  ret i1 false
}

; ------------------------------------------------------------------ console

define void @baf.console.clear() {
entry:
  %handle = call ptr @GetStdHandle(i32 -11)
  %info = alloca %baf.win.csbi, align 2
  %ok = call i32 @GetConsoleScreenBufferInfo(ptr %handle, ptr %info)
  %failed = icmp eq i32 %ok, 0
  ; Output is redirected to a file or pipe: there is no screen to clear.
  br i1 %failed, label %skip, label %wipe

wipe:
  %width.slot = getelementptr inbounds %baf.win.csbi, ptr %info, i32 0, i32 0
  %height.slot = getelementptr inbounds %baf.win.csbi, ptr %info, i32 0, i32 1
  %width = load i16, ptr %width.slot, align 2
  %height = load i16, ptr %height.slot, align 2
  %width32 = sext i16 %width to i32
  %height32 = sext i16 %height to i32
  %cells = mul i32 %width32, %height32

  %fg = load i16, ptr @baf.win.fg, align 2
  %bg = load i16, ptr @baf.win.bg, align 2
  %bg.shifted = shl i16 %bg, 4
  %attribute = or i16 %bg.shifted, %fg

  ; COORD is a 4-byte struct passed by value, lowered to i32 on Win64.
  %filled = call i32 @FillConsoleOutputCharacterA(
      ptr %handle, i8 32, i32 %cells, i32 0, ptr @baf.win.scratch)
  %recolored = call i32 @FillConsoleOutputAttribute(
      ptr %handle, i16 %attribute, i32 %cells, i32 0, ptr @baf.win.scratch)
  %homed = call i32 @SetConsoleCursorPosition(ptr %handle, i32 0)
  ret void

skip:
  ret void
}

define void @baf.console.set_text_color(i64 %color) {
entry:
  %narrow = trunc i64 %color to i16
  %masked = and i16 %narrow, 15
  store i16 %masked, ptr @baf.win.fg, align 2
  call void @baf.win.apply_color()
  ret void
}

define void @baf.console.set_background_color(i64 %color) {
entry:
  %narrow = trunc i64 %color to i16
  %masked = and i16 %narrow, 15
  store i16 %masked, ptr @baf.win.bg, align 2
  call void @baf.win.apply_color()
  ret void
}

; -------------------------------------------------------------------- power

; Hosted builds have no real power control, so both calls end the process.
; The console attribute is reset first so the user's shell is left as found.
define void @baf.power.shutdown() {
entry:
  %handle = call ptr @GetStdHandle(i32 -11)
  %ignored = call i32 @SetConsoleTextAttribute(ptr %handle, i16 7)
  call void @exit(i32 0)
  unreachable
}

define void @baf.power.reboot() {
entry:
  %handle = call ptr @GetStdHandle(i32 -11)
  %ignored = call i32 @SetConsoleTextAttribute(ptr %handle, i16 7)
  call void @exit(i32 0)
  unreachable
}

; --------------------------------------------------------------------- disk
;
; The BAFS1 drivers are freestanding-only. These stubs keep hosted builds
; linkable and match runtime/posix.ll exactly.

define void @baf.disk.scan() {
entry:
  ret void
}

define i64 @baf.disk.count() {
entry:
  ret i64 0
}

define void @baf.disk.list() {
entry:
  call void @baf.win.write(ptr @baf.host.disk.unavailable, i64 30)
  ret void
}

define void @baf.disk.hex(i64 %disk, i64 %lba) {
entry:
  call void @baf.win.write(ptr @baf.host.disk.unavailable, i64 30)
  ret void
}

define i1 @baf.disk.select(i64 %disk) {
entry:
  ret i1 false
}

define i1 @baf.disk.format() {
entry:
  ret i1 false
}

define void @baf.disk.files() {
entry:
  call void @baf.win.write(ptr @baf.host.disk.unavailable, i64 30)
  ret void
}

define i1 @baf.disk.write(%baf.str %name, %baf.str %content) {
entry:
  ret i1 false
}

define %baf.str @baf.disk.read(%baf.str %name) {
entry:
  ret %baf.str zeroinitializer
}

define i1 @baf.disk.rem(%baf.str %name) {
entry:
  ret i1 false
}

define i1 @baf.disk.exists(%baf.str %name) {
entry:
  ret i1 false
}

define i64 @baf.disk.size(%baf.str %name) {
entry:
  ret i64 0
}

define void @baf.disk.info() {
entry:
  call void @baf.win.write(ptr @baf.host.disk.unavailable, i64 30)
  ret void
}

define i1 @baf.disk.create_dir(%baf.str %name) {
entry:
  ret i1 false
}

define i1 @baf.disk.goto_dir(%baf.str %name) {
entry:
  ret i1 false
}

define %baf.str @baf.disk.get_dir() {
entry:
  ret %baf.str zeroinitializer
}

; ---------------------------------------------------------------------------
; BackAndForth 0.7 string library.
;
; Strings are immutable { ptr, len } pairs. Anything built at run time (concat,
; substrings, int/bool conversions) is allocated from a fixed bump arena that
; wraps around when it fills up, so there is no allocator dependency and the
; same code works hosted and freestanding. Keep long-lived results in mind:
; after ARENA_BYTES of fresh strings, the oldest ones are overwritten.
; ---------------------------------------------------------------------------

@baf.strlib.arena = internal global [65536 x i8] zeroinitializer, align 8
@baf.strlib.arena.next = internal global i64 0, align 8
@baf.strlib.true = private constant [4 x i8] c"true"
@baf.strlib.false = private constant [5 x i8] c"false"

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

define internal ptr @baf.strlib.alloc(i64 %size) {
entry:
  %too.big = icmp ugt i64 %size, 65536
  %clamped = select i1 %too.big, i64 65536, i64 %size
  %next = load i64, ptr @baf.strlib.arena.next, align 8
  %end = add i64 %next, %clamped
  %overflow = icmp ugt i64 %end, 65536
  %base = select i1 %overflow, i64 0, i64 %next
  %new.next = add i64 %base, %clamped
  store i64 %new.next, ptr @baf.strlib.arena.next, align 8
  %ptr = getelementptr inbounds [65536 x i8], ptr @baf.strlib.arena, i64 0, i64 %base
  ret ptr %ptr
}

define %baf.str @baf.str.concat(%baf.str %left, %baf.str %right) {
entry:
  %lp = extractvalue %baf.str %left, 0
  %ll = extractvalue %baf.str %left, 1
  %rp = extractvalue %baf.str %right, 0
  %rl = extractvalue %baf.str %right, 1
  %total = add i64 %ll, %rl
  %need = add i64 %total, 1
  %dst = call ptr @baf.strlib.alloc(i64 %need)
  call void @llvm.memcpy.p0.p0.i64(ptr %dst, ptr %lp, i64 %ll, i1 false)
  %tail = getelementptr inbounds i8, ptr %dst, i64 %ll
  call void @llvm.memcpy.p0.p0.i64(ptr %tail, ptr %rp, i64 %rl, i1 false)
  %term = getelementptr inbounds i8, ptr %dst, i64 %total
  store i8 0, ptr %term, align 1
  %r0 = insertvalue %baf.str undef, ptr %dst, 0
  %r1 = insertvalue %baf.str %r0, i64 %total, 1
  ret %baf.str %r1
}

define %baf.str @baf.str.sub(%baf.str %text, i64 %start, i64 %count) {
entry:
  %p = extractvalue %baf.str %text, 0
  %n = extractvalue %baf.str %text, 1
  %s.neg = icmp slt i64 %start, 0
  %s0 = select i1 %s.neg, i64 0, i64 %start
  %s.big = icmp sgt i64 %s0, %n
  %s1 = select i1 %s.big, i64 %n, i64 %s0
  %avail = sub i64 %n, %s1
  %c.neg = icmp slt i64 %count, 0
  %c0 = select i1 %c.neg, i64 0, i64 %count
  %c.big = icmp sgt i64 %c0, %avail
  %c1 = select i1 %c.big, i64 %avail, i64 %c0
  %need = add i64 %c1, 1
  %dst = call ptr @baf.strlib.alloc(i64 %need)
  %src = getelementptr inbounds i8, ptr %p, i64 %s1
  call void @llvm.memcpy.p0.p0.i64(ptr %dst, ptr %src, i64 %c1, i1 false)
  %term = getelementptr inbounds i8, ptr %dst, i64 %c1
  store i8 0, ptr %term, align 1
  %r0 = insertvalue %baf.str undef, ptr %dst, 0
  %r1 = insertvalue %baf.str %r0, i64 %c1, 1
  ret %baf.str %r1
}

define %baf.str @baf.str.from_bool(i1 %value) {
entry:
  %ptr = select i1 %value, ptr @baf.strlib.true, ptr @baf.strlib.false
  %len = select i1 %value, i64 4, i64 5
  %r0 = insertvalue %baf.str undef, ptr %ptr, 0
  %r1 = insertvalue %baf.str %r0, i64 %len, 1
  ret %baf.str %r1
}

define %baf.str @baf.str.from_int(i64 %value) {
entry:
  %scratch = call ptr @baf.strlib.alloc(i64 24)
  %negative = icmp slt i64 %value, 0
  %flipped = sub i64 0, %value
  %magnitude = select i1 %negative, i64 %flipped, i64 %value
  br label %digits

digits:
  %m = phi i64 [ %magnitude, %entry ], [ %m.next, %digits ]
  %i = phi i64 [ 0, %entry ], [ %i.next, %digits ]
  %digit = urem i64 %m, 10
  %m.next = udiv i64 %m, 10
  %narrow = trunc i64 %digit to i8
  %ascii = add i8 %narrow, 48
  %slot = getelementptr inbounds i8, ptr %scratch, i64 %i
  store i8 %ascii, ptr %slot, align 1
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %m.next, 0
  br i1 %done, label %layout, label %digits

layout:
  %count = phi i64 [ %i.next, %digits ]
  %sign = zext i1 %negative to i64
  %total = add i64 %count, %sign
  %need = add i64 %total, 1
  %dst = call ptr @baf.strlib.alloc(i64 %need)
  br i1 %negative, label %minus, label %reverse.head

minus:
  store i8 45, ptr %dst, align 1
  br label %reverse.head

reverse.head:
  br label %reverse.cond

reverse.cond:
  %k = phi i64 [ 0, %reverse.head ], [ %k.next, %reverse.body ]
  %more = icmp ult i64 %k, %count
  br i1 %more, label %reverse.body, label %finish

reverse.body:
  %from.end = sub i64 %count, %k
  %src.index = sub i64 %from.end, 1
  %src = getelementptr inbounds i8, ptr %scratch, i64 %src.index
  %ch = load i8, ptr %src, align 1
  %dst.index = add i64 %k, %sign
  %dst.slot = getelementptr inbounds i8, ptr %dst, i64 %dst.index
  store i8 %ch, ptr %dst.slot, align 1
  %k.next = add i64 %k, 1
  br label %reverse.cond

finish:
  %term = getelementptr inbounds i8, ptr %dst, i64 %total
  store i8 0, ptr %term, align 1
  %r0 = insertvalue %baf.str undef, ptr %dst, 0
  %r1 = insertvalue %baf.str %r0, i64 %total, 1
  ret %baf.str %r1
}

define i64 @baf.str.to_int(%baf.str %text) {
entry:
  %p = extractvalue %baf.str %text, 0
  %n = extractvalue %baf.str %text, 1
  %empty = icmp sle i64 %n, 0
  br i1 %empty, label %zero, label %check

zero:
  ret i64 0

check:
  %first = load i8, ptr %p, align 1
  %negative = icmp eq i8 %first, 45
  %plus = icmp eq i8 %first, 43
  %signed = or i1 %negative, %plus
  %start = select i1 %signed, i64 1, i64 0
  br label %scan

scan:
  %i = phi i64 [ %start, %check ], [ %i.next, %body ]
  %acc = phi i64 [ 0, %check ], [ %acc.next, %body ]
  %more = icmp ult i64 %i, %n
  br i1 %more, label %body, label %done

body:
  %slot = getelementptr inbounds i8, ptr %p, i64 %i
  %ch = load i8, ptr %slot, align 1
  %wide = zext i8 %ch to i64
  %digit = sub i64 %wide, 48
  %ge = icmp sge i64 %digit, 0
  %le = icmp sle i64 %digit, 9
  %ok = and i1 %ge, %le
  %scaled = mul i64 %acc, 10
  %added = add i64 %scaled, %digit
  %acc.next = select i1 %ok, i64 %added, i64 %acc
  %i.next = add i64 %i, 1
  br label %scan

done:
  %flipped = sub i64 0, %acc
  %result = select i1 %negative, i64 %flipped, i64 %acc
  ret i64 %result
}
