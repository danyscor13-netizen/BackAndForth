; BackAndForth freestanding i386 ABI wrappers for console and keyboard I/O.

target triple = "i386-unknown-none"

%baf.str = type { ptr, i32 }

declare void @baf_core_console_write(ptr, i32, i32)
declare void @baf_core_console_clear()
declare void @baf_core_console_newline()
declare void @baf_core_console_write_i32(i32)
declare void @baf_core_console_write_bool(i32)
declare void @baf_core_console_set_text_color(i32)
declare void @baf_core_console_set_background_color(i32)
declare i32 @baf_core_input_read(ptr, i32)
declare i32 @baf_core_string_equal(ptr, i32, ptr, i32)
declare void @baf_core_power_shutdown()
declare void @baf_core_power_reboot()
declare void @baf_core_disk_scan()
declare i32 @baf_core_disk_count()
declare void @baf_core_disk_list()
declare void @baf_core_disk_hex(i32, i32)
declare i32 @baf_core_disk_select(i32)
declare i32 @baf_core_fs_format()
declare void @baf_core_fs_list()
declare i32 @baf_core_fs_write(ptr, i32, ptr, i32)
declare i32 @baf_core_fs_read(ptr, i32, ptr, ptr)
declare i32 @baf_core_fs_remove(ptr, i32)
declare i32 @baf_core_fs_exists(ptr, i32)
declare i32 @baf_core_fs_size(ptr, i32)
declare void @baf_core_fs_info()
declare i32 @baf_core_fs_create_dir(ptr, i32)
declare i32 @baf_core_fs_goto_dir(ptr, i32)
declare i32 @baf_core_fs_get_dir(ptr, ptr)

define void @baf.putl(%baf.str %value) {
entry:
  %ptr = extractvalue %baf.str %value, 0
  %len = extractvalue %baf.str %value, 1
  call void @baf_core_console_write(ptr %ptr, i32 %len, i32 0)
  ret void
}

define void @baf.putsc(%baf.str %value) {
entry:
  %ptr = extractvalue %baf.str %value, 0
  %len = extractvalue %baf.str %value, 1
  call void @baf_core_console_write(ptr %ptr, i32 %len, i32 1)
  ret void
}

define void @baf.put.int(i32 %value) {
entry:
  call void @baf_core_console_write_i32(i32 %value)
  ret void
}

define void @baf.put.bool(i1 %value) {
entry:
  %wide = zext i1 %value to i32
  call void @baf_core_console_write_bool(i32 %wide)
  ret void
}

define void @baf.put.newline() {
entry:
  call void @baf_core_console_newline()
  ret void
}

define %baf.str @baf.input.read(ptr %buffer, i32 %capacity) {
entry:
  %length = call i32 @baf_core_input_read(ptr %buffer, i32 %capacity)
  %result.0 = insertvalue %baf.str zeroinitializer, ptr %buffer, 0
  %result.1 = insertvalue %baf.str %result.0, i32 %length, 1
  ret %baf.str %result.1
}

define i1 @baf.str.eq(%baf.str %left, %baf.str %right) {
entry:
  %left.ptr = extractvalue %baf.str %left, 0
  %left.len = extractvalue %baf.str %left, 1
  %right.ptr = extractvalue %baf.str %right, 0
  %right.len = extractvalue %baf.str %right, 1
  %equal32 = call i32 @baf_core_string_equal(ptr %left.ptr, i32 %left.len,
                                             ptr %right.ptr, i32 %right.len)
  %equal = icmp ne i32 %equal32, 0
  ret i1 %equal
}

define void @baf.console.clear() {
entry:
  call void @baf_core_console_clear()
  ret void
}

define void @baf.console.set_text_color(i32 %color) {
entry:
  call void @baf_core_console_set_text_color(i32 %color)
  ret void
}

define void @baf.console.set_background_color(i32 %color) {
entry:
  call void @baf_core_console_set_background_color(i32 %color)
  ret void
}

define void @baf.power.shutdown() {
entry:
  call void @baf_core_power_shutdown()
  unreachable
}

define void @baf.power.reboot() {
entry:
  call void @baf_core_power_reboot()
  unreachable
}

define void @baf.disk.scan() {
entry:
  call void @baf_core_disk_scan()
  ret void
}

define i32 @baf.disk.count() {
entry:
  %count = call i32 @baf_core_disk_count()
  ret i32 %count
}

define void @baf.disk.list() {
entry:
  call void @baf_core_disk_list()
  ret void
}

define void @baf.disk.hex(i32 %disk, i32 %lba) {
entry:
  call void @baf_core_disk_hex(i32 %disk, i32 %lba)
  ret void
}


define i1 @baf.disk.select(i32 %disk) {
entry:
  %ok32 = call i32 @baf_core_disk_select(i32 %disk)
  %ok = icmp ne i32 %ok32, 0
  ret i1 %ok
}

define i1 @baf.disk.format() {
entry:
  %ok32 = call i32 @baf_core_fs_format()
  %ok = icmp ne i32 %ok32, 0
  ret i1 %ok
}

define void @baf.disk.files() {
entry:
  call void @baf_core_fs_list()
  ret void
}

define i1 @baf.disk.write(%baf.str %name, %baf.str %content) {
entry:
  %name.ptr = extractvalue %baf.str %name, 0
  %name.len = extractvalue %baf.str %name, 1
  %content.ptr = extractvalue %baf.str %content, 0
  %content.len = extractvalue %baf.str %content, 1
  %ok32 = call i32 @baf_core_fs_write(ptr %name.ptr, i32 %name.len,
                                      ptr %content.ptr, i32 %content.len)
  %ok = icmp ne i32 %ok32, 0
  ret i1 %ok
}

define %baf.str @baf.disk.read(%baf.str %name) {
entry:
  %name.ptr = extractvalue %baf.str %name, 0
  %name.len = extractvalue %baf.str %name, 1
  %out.ptr = alloca ptr, align 4
  %out.len = alloca i32, align 4
  store ptr null, ptr %out.ptr, align 4
  store i32 0, ptr %out.len, align 4
  %ok = call i32 @baf_core_fs_read(ptr %name.ptr, i32 %name.len,
                                    ptr %out.ptr, ptr %out.len)
  %data = load ptr, ptr %out.ptr, align 4
  %length = load i32, ptr %out.len, align 4
  %result.0 = insertvalue %baf.str zeroinitializer, ptr %data, 0
  %result.1 = insertvalue %baf.str %result.0, i32 %length, 1
  ret %baf.str %result.1
}

define i1 @baf.disk.rem(%baf.str %name) {
entry:
  %name.ptr = extractvalue %baf.str %name, 0
  %name.len = extractvalue %baf.str %name, 1
  %ok32 = call i32 @baf_core_fs_remove(ptr %name.ptr, i32 %name.len)
  %ok = icmp ne i32 %ok32, 0
  ret i1 %ok
}

define i1 @baf.disk.exists(%baf.str %name) {
entry:
  %name.ptr = extractvalue %baf.str %name, 0
  %name.len = extractvalue %baf.str %name, 1
  %ok32 = call i32 @baf_core_fs_exists(ptr %name.ptr, i32 %name.len)
  %ok = icmp ne i32 %ok32, 0
  ret i1 %ok
}

define i32 @baf.disk.size(%baf.str %name) {
entry:
  %name.ptr = extractvalue %baf.str %name, 0
  %name.len = extractvalue %baf.str %name, 1
  %size = call i32 @baf_core_fs_size(ptr %name.ptr, i32 %name.len)
  ret i32 %size
}

define void @baf.disk.info() {
entry:
  call void @baf_core_fs_info()
  ret void
}


define i1 @baf.disk.create_dir(%baf.str %name) {
entry:
  %name.ptr = extractvalue %baf.str %name, 0
  %name.len = extractvalue %baf.str %name, 1
  %ok32 = call i32 @baf_core_fs_create_dir(ptr %name.ptr, i32 %name.len)
  %ok = icmp ne i32 %ok32, 0
  ret i1 %ok
}

define i1 @baf.disk.goto_dir(%baf.str %name) {
entry:
  %name.ptr = extractvalue %baf.str %name, 0
  %name.len = extractvalue %baf.str %name, 1
  %ok32 = call i32 @baf_core_fs_goto_dir(ptr %name.ptr, i32 %name.len)
  %ok = icmp ne i32 %ok32, 0
  ret i1 %ok
}

define %baf.str @baf.disk.get_dir() {
entry:
  %out.ptr = alloca ptr, align 4
  %out.len = alloca i32, align 4
  store ptr null, ptr %out.ptr, align 4
  store i32 0, ptr %out.len, align 4
  %ok = call i32 @baf_core_fs_get_dir(ptr %out.ptr, ptr %out.len)
  %data = load ptr, ptr %out.ptr, align 4
  %length = load i32, ptr %out.len, align 4
  %result.0 = insertvalue %baf.str zeroinitializer, ptr %data, 0
  %result.1 = insertvalue %baf.str %result.0, i32 %length, 1
  ret %baf.str %result.1
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

@baf.strlib.arena = internal global [65536 x i8] zeroinitializer, align 4
@baf.strlib.arena.next = internal global i32 0, align 4
@baf.strlib.true = private constant [4 x i8] c"true"
@baf.strlib.false = private constant [5 x i8] c"false"

; A byte-at-a-time copy rather than llvm.memcpy. The intrinsic lowers to a call
; to memcpy, and a freestanding kernel has no libc to satisfy it, so this keeps
; the string helpers self-contained.
define internal void @baf.strlib.copy(ptr %dst, ptr %src, i32 %count) {
entry:
  br label %cond

cond:
  %i = phi i32 [ 0, %entry ], [ %i.next, %body ]
  %more = icmp ult i32 %i, %count
  br i1 %more, label %body, label %done

body:
  %from = getelementptr inbounds i8, ptr %src, i32 %i
  %byte = load i8, ptr %from, align 1
  %to = getelementptr inbounds i8, ptr %dst, i32 %i
  store i8 %byte, ptr %to, align 1
  %i.next = add i32 %i, 1
  br label %cond

done:
  ret void
}

define internal ptr @baf.strlib.alloc(i32 %size) {
entry:
  %too.big = icmp ugt i32 %size, 65536
  %clamped = select i1 %too.big, i32 65536, i32 %size
  %next = load i32, ptr @baf.strlib.arena.next, align 4
  %end = add i32 %next, %clamped
  %overflow = icmp ugt i32 %end, 65536
  %base = select i1 %overflow, i32 0, i32 %next
  %new.next = add i32 %base, %clamped
  store i32 %new.next, ptr @baf.strlib.arena.next, align 4
  %ptr = getelementptr inbounds [65536 x i8], ptr @baf.strlib.arena, i32 0, i32 %base
  ret ptr %ptr
}

define %baf.str @baf.str.concat(%baf.str %left, %baf.str %right) {
entry:
  %lp = extractvalue %baf.str %left, 0
  %ll = extractvalue %baf.str %left, 1
  %rp = extractvalue %baf.str %right, 0
  %rl = extractvalue %baf.str %right, 1
  %total = add i32 %ll, %rl
  %need = add i32 %total, 1
  %dst = call ptr @baf.strlib.alloc(i32 %need)
  call void @baf.strlib.copy(ptr %dst, ptr %lp, i32 %ll)
  %tail = getelementptr inbounds i8, ptr %dst, i32 %ll
  call void @baf.strlib.copy(ptr %tail, ptr %rp, i32 %rl)
  %term = getelementptr inbounds i8, ptr %dst, i32 %total
  store i8 0, ptr %term, align 1
  %r0 = insertvalue %baf.str undef, ptr %dst, 0
  %r1 = insertvalue %baf.str %r0, i32 %total, 1
  ret %baf.str %r1
}

define %baf.str @baf.str.sub(%baf.str %text, i32 %start, i32 %count) {
entry:
  %p = extractvalue %baf.str %text, 0
  %n = extractvalue %baf.str %text, 1
  %s.neg = icmp slt i32 %start, 0
  %s0 = select i1 %s.neg, i32 0, i32 %start
  %s.big = icmp sgt i32 %s0, %n
  %s1 = select i1 %s.big, i32 %n, i32 %s0
  %avail = sub i32 %n, %s1
  %c.neg = icmp slt i32 %count, 0
  %c0 = select i1 %c.neg, i32 0, i32 %count
  %c.big = icmp sgt i32 %c0, %avail
  %c1 = select i1 %c.big, i32 %avail, i32 %c0
  %need = add i32 %c1, 1
  %dst = call ptr @baf.strlib.alloc(i32 %need)
  %src = getelementptr inbounds i8, ptr %p, i32 %s1
  call void @baf.strlib.copy(ptr %dst, ptr %src, i32 %c1)
  %term = getelementptr inbounds i8, ptr %dst, i32 %c1
  store i8 0, ptr %term, align 1
  %r0 = insertvalue %baf.str undef, ptr %dst, 0
  %r1 = insertvalue %baf.str %r0, i32 %c1, 1
  ret %baf.str %r1
}

define %baf.str @baf.str.from_bool(i1 %value) {
entry:
  %ptr = select i1 %value, ptr @baf.strlib.true, ptr @baf.strlib.false
  %len = select i1 %value, i32 4, i32 5
  %r0 = insertvalue %baf.str undef, ptr %ptr, 0
  %r1 = insertvalue %baf.str %r0, i32 %len, 1
  ret %baf.str %r1
}

define %baf.str @baf.str.from_int(i32 %value) {
entry:
  %scratch = call ptr @baf.strlib.alloc(i32 24)
  %negative = icmp slt i32 %value, 0
  %flipped = sub i32 0, %value
  %magnitude = select i1 %negative, i32 %flipped, i32 %value
  br label %digits

digits:
  %m = phi i32 [ %magnitude, %entry ], [ %m.next, %digits ]
  %i = phi i32 [ 0, %entry ], [ %i.next, %digits ]
  %digit = urem i32 %m, 10
  %m.next = udiv i32 %m, 10
  %narrow = trunc i32 %digit to i8
  %ascii = add i8 %narrow, 48
  %slot = getelementptr inbounds i8, ptr %scratch, i32 %i
  store i8 %ascii, ptr %slot, align 1
  %i.next = add i32 %i, 1
  %done = icmp eq i32 %m.next, 0
  br i1 %done, label %layout, label %digits

layout:
  %count = phi i32 [ %i.next, %digits ]
  %sign = zext i1 %negative to i32
  %total = add i32 %count, %sign
  %need = add i32 %total, 1
  %dst = call ptr @baf.strlib.alloc(i32 %need)
  br i1 %negative, label %minus, label %reverse.head

minus:
  store i8 45, ptr %dst, align 1
  br label %reverse.head

reverse.head:
  br label %reverse.cond

reverse.cond:
  %k = phi i32 [ 0, %reverse.head ], [ %k.next, %reverse.body ]
  %more = icmp ult i32 %k, %count
  br i1 %more, label %reverse.body, label %finish

reverse.body:
  %from.end = sub i32 %count, %k
  %src.index = sub i32 %from.end, 1
  %src = getelementptr inbounds i8, ptr %scratch, i32 %src.index
  %ch = load i8, ptr %src, align 1
  %dst.index = add i32 %k, %sign
  %dst.slot = getelementptr inbounds i8, ptr %dst, i32 %dst.index
  store i8 %ch, ptr %dst.slot, align 1
  %k.next = add i32 %k, 1
  br label %reverse.cond

finish:
  %term = getelementptr inbounds i8, ptr %dst, i32 %total
  store i8 0, ptr %term, align 1
  %r0 = insertvalue %baf.str undef, ptr %dst, 0
  %r1 = insertvalue %baf.str %r0, i32 %total, 1
  ret %baf.str %r1
}

define i32 @baf.str.to_int(%baf.str %text) {
entry:
  %p = extractvalue %baf.str %text, 0
  %n = extractvalue %baf.str %text, 1
  %empty = icmp sle i32 %n, 0
  br i1 %empty, label %zero, label %check

zero:
  ret i32 0

check:
  %first = load i8, ptr %p, align 1
  %negative = icmp eq i8 %first, 45
  %plus = icmp eq i8 %first, 43
  %signed = or i1 %negative, %plus
  %start = select i1 %signed, i32 1, i32 0
  br label %scan

scan:
  %i = phi i32 [ %start, %check ], [ %i.next, %body ]
  %acc = phi i32 [ 0, %check ], [ %acc.next, %body ]
  %more = icmp ult i32 %i, %n
  br i1 %more, label %body, label %done

body:
  %slot = getelementptr inbounds i8, ptr %p, i32 %i
  %ch = load i8, ptr %slot, align 1
  %wide = zext i8 %ch to i32
  %digit = sub i32 %wide, 48
  %ge = icmp sge i32 %digit, 0
  %le = icmp sle i32 %digit, 9
  %ok = and i1 %ge, %le
  %scaled = mul i32 %acc, 10
  %added = add i32 %scaled, %digit
  %acc.next = select i1 %ok, i32 %added, i32 %acc
  %i.next = add i32 %i, 1
  br label %scan

done:
  %flipped = sub i32 0, %acc
  %result = select i1 %negative, i32 %flipped, i32 %acc
  ret i32 %result
}
