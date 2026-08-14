; Freestanding i386 runtime for BAF.
; Writes strings to VGA text memory and mirrors bytes to QEMU debugcon.

target triple = "i386-unknown-none"

%baf.str = type { ptr, i32 }

@baf.cursor = internal global i32 0, align 4

declare void @baf.debug_putc(i8)

define void @baf.putsc(%baf.str %text) {
entry:
  %data = extractvalue %baf.str %text, 0
  %length = extractvalue %baf.str %text, 1
  br label %loop

loop:
  %index = phi i32 [ 0, %entry ], [ %next.index, %continue ]
  %done = icmp uge i32 %index, %length
  br i1 %done, label %exit, label %body

body:
  %source = getelementptr inbounds i8, ptr %data, i32 %index
  %character = load i8, ptr %source, align 1
  call void @baf.debug_putc(i8 %character)
  %is.newline = icmp eq i8 %character, 10
  br i1 %is.newline, label %newline, label %print

newline:
  %old.cursor.nl = load i32, ptr @baf.cursor, align 4
  %row = udiv i32 %old.cursor.nl, 80
  %next.row = add i32 %row, 1
  %new.cursor.nl = mul i32 %next.row, 80
  store i32 %new.cursor.nl, ptr @baf.cursor, align 4
  br label %continue

print:
  %old.cursor = load i32, ptr @baf.cursor, align 4
  %screen = inttoptr i32 753664 to ptr
  %cell = getelementptr inbounds i16, ptr %screen, i32 %old.cursor
  %wide.character = zext i8 %character to i16
  %colored.character = or i16 %wide.character, 3840
  store volatile i16 %colored.character, ptr %cell, align 2
  %new.cursor = add i32 %old.cursor, 1
  store i32 %new.cursor, ptr @baf.cursor, align 4
  br label %continue

continue:
  %next.index = add i32 %index, 1
  br label %loop

exit:
  ret void
}
