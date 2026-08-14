; BackAndForth hosted POSIX runtime.

%baf.str = type { ptr, i64 }

@baf.host.newline = private constant [1 x i8] c"\0A"
@baf.host.clear = private constant [7 x i8] c"\1B[2J\1B[H"
@baf.host.disk.unavailable = private constant [30 x i8] c"Disk drivers require --osDev.\0A"
@baf.host.int.format = private constant [5 x i8] c"%lld\00"
@baf.host.int.buffer = internal global [32 x i8] zeroinitializer, align 1
@baf.host.true = private constant [4 x i8] c"true"
@baf.host.false = private constant [5 x i8] c"false"

; VGA colour number -> ANSI base colour index. VGA orders the low 8 colours as
; black, blue, green, cyan, red, magenta, brown, light grey; ANSI orders them
; black, red, green, yellow, blue, magenta, cyan, white. Entries 8-15 repeat
; the table because those are the bright variants of the same eight hues.
@baf.ansi.map = private constant [16 x i8] c"\00\04\02\06\01\05\03\07\00\04\02\06\01\05\03\07"
@baf.ansi.format = private constant [6 x i8] c"\1B[%dm\00"
@baf.ansi.buffer = internal global [16 x i8] zeroinitializer, align 1

declare i64 @write(i32, ptr, i64)
declare i64 @read(i32, ptr, i64)
declare void @exit(i32) noreturn
declare i32 @snprintf(ptr, i64, ptr, ...)

define void @baf.putl(%baf.str %value) {
entry:
  %ptr = extractvalue %baf.str %value, 0
  %len = extractvalue %baf.str %value, 1
  %ignored = call i64 @write(i32 1, ptr %ptr, i64 %len)
  ret void
}

define void @baf.putsc(%baf.str %value) {
entry:
  call void @baf.putl(%baf.str %value)
  %ignored = call i64 @write(i32 1, ptr @baf.host.newline, i64 1)
  ret void
}

define void @baf.put.int(i64 %value) {
entry:
  %length32 = call i32 (ptr, i64, ptr, ...) @snprintf(
      ptr @baf.host.int.buffer, i64 32, ptr @baf.host.int.format, i64 %value)
  %length = sext i32 %length32 to i64
  %ignored = call i64 @write(i32 1, ptr @baf.host.int.buffer, i64 %length)
  ret void
}

define void @baf.put.bool(i1 %value) {
entry:
  br i1 %value, label %yes, label %no
yes:
  %ignored.true = call i64 @write(i32 1, ptr @baf.host.true, i64 4)
  ret void
no:
  %ignored.false = call i64 @write(i32 1, ptr @baf.host.false, i64 5)
  ret void
}

define void @baf.put.newline() {
entry:
  %ignored = call i64 @write(i32 1, ptr @baf.host.newline, i64 1)
  ret void
}

define %baf.str @baf.input.read(ptr %buffer, i64 %capacity) {
entry:
  %has.space = icmp ugt i64 %capacity, 0
  br i1 %has.space, label %loop, label %empty

loop:
  %index = phi i64 [ 0, %entry ], [ %next, %keep ]
  %limit = sub i64 %capacity, 1
  %full = icmp uge i64 %index, %limit
  br i1 %full, label %done.loop, label %read.one

read.one:
  %slot = getelementptr inbounds i8, ptr %buffer, i64 %index
  %count = call i64 @read(i32 0, ptr %slot, i64 1)
  %eof = icmp sle i64 %count, 0
  br i1 %eof, label %done.read, label %check

check:
  %character = load i8, ptr %slot, align 1
  %newline = icmp eq i8 %character, 10
  br i1 %newline, label %done.check, label %keep

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

define void @baf.console.clear() {
entry:
  %ignored = call i64 @write(i32 1, ptr @baf.host.clear, i64 7)
  ret void
}

; Emits one SGR escape for the already-computed ANSI code.
define internal void @baf.ansi.emit(i64 %code) {
entry:
  %length32 = call i32 (ptr, i64, ptr, ...) @snprintf(
      ptr @baf.ansi.buffer, i64 16, ptr @baf.ansi.format, i64 %code)
  %length = sext i32 %length32 to i64
  %ignored = call i64 @write(i32 1, ptr @baf.ansi.buffer, i64 %length)
  ret void
}

; Splits a VGA colour into its ANSI base index and a bright flag.
define internal { i64, i1 } @baf.ansi.split(i64 %color) {
entry:
  %masked = and i64 %color, 15
  %slot = getelementptr inbounds [16 x i8], ptr @baf.ansi.map, i64 0, i64 %masked
  %base8 = load i8, ptr %slot, align 1
  %base = zext i8 %base8 to i64
  %bright = icmp uge i64 %masked, 8
  %result.0 = insertvalue { i64, i1 } zeroinitializer, i64 %base, 0
  %result.1 = insertvalue { i64, i1 } %result.0, i1 %bright, 1
  ret { i64, i1 } %result.1
}

define void @baf.console.set_text_color(i64 %color) {
entry:
  %split = call { i64, i1 } @baf.ansi.split(i64 %color)
  %base = extractvalue { i64, i1 } %split, 0
  %bright = extractvalue { i64, i1 } %split, 1
  ; 30-37 for the normal hues, 90-97 for the bright ones.
  %offset = select i1 %bright, i64 90, i64 30
  %code = add i64 %offset, %base
  call void @baf.ansi.emit(i64 %code)
  ret void
}

define void @baf.console.set_background_color(i64 %color) {
entry:
  %split = call { i64, i1 } @baf.ansi.split(i64 %color)
  %base = extractvalue { i64, i1 } %split, 0
  %bright = extractvalue { i64, i1 } %split, 1
  ; 40-47 for the normal hues, 100-107 for the bright ones.
  %offset = select i1 %bright, i64 100, i64 40
  %code = add i64 %offset, %base
  call void @baf.ansi.emit(i64 %code)
  ret void
}

; Hosted builds have no real power control, so both calls end the process.
; Colours are reset first so the user's shell is left as it was found.
define void @baf.power.shutdown() {
entry:
  call void @baf.ansi.emit(i64 0)
  call void @exit(i32 0)
  unreachable
}

define void @baf.power.reboot() {
entry:
  call void @baf.ansi.emit(i64 0)
  call void @exit(i32 0)
  unreachable
}

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
  %ignored = call i64 @write(i32 1, ptr @baf.host.disk.unavailable, i64 30)
  ret void
}

define void @baf.disk.hex(i64 %disk, i64 %lba) {
entry:
  %ignored = call i64 @write(i32 1, ptr @baf.host.disk.unavailable, i64 30)
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
  %ignored = call i64 @write(i32 1, ptr @baf.host.disk.unavailable, i64 30)
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
  %ignored = call i64 @write(i32 1, ptr @baf.host.disk.unavailable, i64 30)
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
