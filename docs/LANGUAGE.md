# The BackAndForth language

Version 0.7.0. This is the complete reference for the language as implemented.
If something is not described here, the compiler does not support it yet.

- [A first program](#a-first-program)
- [Lexical structure](#lexical-structure)
- [Types](#types)
- [Variables](#variables)
- [Expressions and operators](#expressions-and-operators)
- [Statements](#statements)
- [Functions](#functions)
- [Includes](#includes)
- [Built-in library](#built-in-library)
- [Targets](#targets)
- [Error messages](#error-messages)

## A first program

```baf
func -> greet(name: str) {
    putsc("Hello, " + name + "!")
}

begin {
    greet("world")
}
```

```sh
baf hello.baf --exe --run
```

Every program has exactly one `begin` block. It is the entry point, and it is
what becomes `main` on a hosted target. Functions may be declared before or
after it, in any order.

## Lexical structure

### Comments

```baf
// a line comment, to the end of the line

/* a block comment,
   which can span lines */
```

Block comments do not nest.

### Statement separators

Statements end at a newline or a `;`. Both are accepted, and blank lines are
ignored, so these are the same program:

```baf
begin {
    int a = 1
    int b = 2
}

begin { int a = 1; int b = 2 }
```

A line may be continued after an operator, a comma, or an open bracket:

```baf
int total = 1 +
            2 +
            3
```

### Identifiers

Letters, digits and `_`, not starting with a digit. Identifiers are case
sensitive. Qualified names such as `Console.Clear` name built-ins; user code
cannot declare a name containing a dot.

### Keywords

```text
begin   func    static  return  void
int     str     string  bool    true    false
if      elsif   elif    else
while   for     break   continue
switch  case    default
```

`elif` is accepted as a synonym for `elsif`, and `else if` is accepted as a
spelling of `elsif`. `string` is a synonym for `str`.

### Literals

```baf
42          // int
-7          // int (unary minus applied to a literal)
"text"      // str
true false  // bool
```

String escapes: `\n`, `\r`, `\t`, `\\`, `\"`. Embedded NUL is not supported.

## Types

| Type | Meaning | Default value |
| --- | --- | --- |
| `int` | 64-bit signed integer hosted, 32-bit on the i386 target | `0` |
| `str` | immutable text, held as a pointer and a length | empty |
| `bool` | `true` or `false` | `false` |
| `void` | the absence of a value; only a function return type | – |

Types are checked statically. There are no implicit numeric conversions; the
only automatic conversion is when `+` concatenates a `str` with an `int` or a
`bool`, described below.

## Variables

```baf
int count = 0
str name = "ada"
bool ready            // no initialiser: takes the default value
```

The type is always written. A variable is visible from its declaration to the
end of the enclosing block, and a nested block may not redeclare a name that is
already in scope.

Assignment reuses the name:

```baf
count = count + 1
count += 1        // the same thing
```

Compound assignments `+=`, `-=`, `*=`, `/=`, `%=` are rewritten as
`x = x <op> (expression)`.

## Expressions and operators

From loosest to tightest binding:

| Precedence | Operators | Notes |
| --- | --- | --- |
| 1 | `\|\|` | short circuits |
| 2 | `&&` | short circuits |
| 3 | `==` `!=` | operands must have the same type |
| 4 | `<` `<=` `>` `>=` | `int` only |
| 5 | `+` `-` | `+` also concatenates strings |
| 6 | `*` `/` `%` | `int` only |
| 7 | `!` `-` (unary) | |
| 8 | calls, `(...)` | |

All binary operators associate to the left.

### Arithmetic

`+ - * / %` operate on `int` and produce `int`. Division and remainder
truncate toward zero. **Division by zero yields `0`** rather than crashing,
which keeps freestanding kernels alive; check the divisor yourself if zero is
a real error in your program.

### Comparison

`==` and `!=` work on `int`, `bool` and `str`. On `str` they compare the
contents, not the address, so `name == "ada"` does what it looks like.
Comparing two different types is a compile error.

`<`, `<=`, `>`, `>=` are `int` only.

### Logic

`&&` and `||` take `bool` operands, produce `bool`, and evaluate the right
side only when the result still depends on it. `!` negates a `bool`.

### String concatenation

When either side of `+` is a `str`, the operator concatenates, converting an
`int` or a `bool` operand to text automatically:

```baf
putsc("v" + 7 + " ready=" + true)   // v7 ready=true
```

Concatenation results live in a fixed-size arena inside the runtime (64 KiB).
The arena wraps around when it fills, so a long-running loop that keeps
building strings will eventually overwrite the oldest ones. Keep concatenation
close to where the result is used; do not stash thousands of built strings and
expect them all to survive.

## Statements

### if / elsif / else

```baf
if (temperature > 30) {
    putsc("hot")
} elsif (temperature > 15) {
    putsc("mild")
} elsif (temperature > 0) {
    putsc("cold")
} else {
    putsc("freezing")
}
```

The condition must be a `bool`; there is no truthiness. Braces are required,
which is why there is no dangling-else ambiguity. `elsif`, `elif` and
`else if` are the same thing. Any number of `elsif` branches may appear, and
the trailing `else` is optional.

### while

```baf
while (running) {
    tick()
}
```

### for

```baf
for (int i = 0; i < 10; i += 1) {
    putl(i + " ")
}
```

The three parts are all optional: `for (;;) { }` is an infinite loop. The
initialiser may declare a variable — scoped to the loop — or assign to an
existing one. The step may be an assignment or a call. `continue` jumps to the
step, so a `for` loop always advances.

### break and continue

`break` leaves the innermost enclosing `while` or `for`; `continue` starts its
next iteration. Both are errors outside a loop.

Note one deliberate difference from C: a `switch` is **not** a break target.
`break` inside a `switch` that sits inside a loop leaves the *loop*. Cases
never fall through, so there is nothing for a `switch`-local break to do.

### switch

```baf
switch (command) {
    case "open" { openFile() }
    case "quit" { running = false }
    default     { putsc("unknown command") }
}
```

The subject may be `int`, `str` or `bool`. Case values must be literals of the
subject's type. Cases do not fall through, `default` is optional, and at most
one `default` is allowed.

### return

```baf
func -> abs(n: int) : int {
    if (n < 0) { return -n }
    return n
}
```

`return` with no value leaves a `void` function early. In a function with a
return type, the compiler checks that every path through the body ends in a
`return`; a loop never counts as returning, because its body may run zero
times.

## Functions

```baf
func -> name(parameter: type, other: type) : returnType {
    ...
}
```

- The return type is optional. Written with `:` (or `->`), it may be `int`,
  `str`, `bool` or `void`.
- With no annotation, a function that returns a value takes the type of what
  it returns, and a function that never returns a value is `void`.
- Parameter types may be omitted when they can be inferred from a call, but
  annotating them gives much better error messages.
- `static func -> helper()` gives the function internal linkage, so it is not
  visible outside the compiled module.
- Recursion is supported, including mutual recursion, since the whole program
  is analysed before any code is generated.
- `main` is reserved and cannot be used as a function name.

### Named arguments

Arguments may be passed by name, in any order, after all positional ones:

```baf
func -> box(width: int, height: int) { ... }

begin {
    box(3, 4)
    box(height: 4, width: 3)
    box(3, height: 4)
}
```

Every parameter must be supplied exactly once. The variadic console built-ins
do not accept named arguments.

## Includes

```baf
[include "lib/strings.baf"]
```

Include directives appear at the top level and are resolved relative to the
including file before parsing. Each file is included once even if it is named
several times, and a cycle is reported as an error. See `docs/INCLUDES.md`.

## Built-in library

### Console

```text
putsc(values...)        prints every value, then a newline
putl(values...)         prints every value with no newline
inpt(prompt...) -> str  prints the prompt, then reads one line
clearc()                clears the screen
Console.Clear()
Console.SetTextColor(color: int)
Console.SetTextBackgroundColor(color: int)
```

`putsc`, `putl` and `inpt` are variadic and accept `str`, `int` and `bool`
arguments. Colours use the 16 VGA colour numbers; hosted builds translate them
to ANSI, so the same program looks the same on a terminal and on bafOS.

### Strings

```text
Str.Length(text: str) -> int
Str.Concat(left: str, right: str) -> str
Str.Sub(text: str, start: int, count: int) -> str
Str.FromInt(value: int) -> str
Str.FromBool(value: bool) -> str
Str.ToInt(text: str) -> int
Str.Equals(left: str, right: str) -> bool
```

`Str.Sub` clamps `start` and `count` to the string, so it never reads out of
bounds; asking for more than there is returns what is there. `Str.ToInt`
accepts an optional leading `+` or `-` and ignores non-digits, returning `0`
for text with no digits at all. The lowercase spellings `str.length`,
`str.concat`, `str.sub`, `str.fromInt`, `str.fromBool`, `str.toInt` and
`str.equals` are accepted as aliases.

### Maths

```text
Math.Abs(value: int) -> int
Math.Min(left: int, right: int) -> int
Math.Max(left: int, right: int) -> int
```

Also available as `math.abs`, `math.min`, `math.max`.

### Power and disks

```text
Power.Shutdown()
Power.Reboot()
Disk.Scan()   Disk.Count() -> int   Disk.List()   Disk.Hex(disk, lba)
Disk.Select(disk: int) -> bool      Disk.Format() -> bool
Disk.Files()  Disk.Info()
Disk.Write(name: str, content: str) -> bool
Disk.Read(name: str) -> str         Disk.Rem(name: str) -> bool
Disk.Exists(name: str) -> bool      Disk.Size(name: str) -> int
Disk.CreateDir(name: str) -> bool   Disk.GotoDir(name: str) -> bool
Disk.GetDir() -> str
```

The disk and power calls do real work only on the freestanding i386 target.
Hosted builds print a notice instead. Lowercase `disk.*` aliases exist for all
of the filesystem calls. See `docs/I386_DISK.md`.

## Targets

```sh
baf program.baf                 # hosted LLVM IR
baf program.baf --exe --run     # native executable, then run it
baf program.baf --osDev         # bootable 32-bit Multiboot image
baf program.baf --osDev --run   # build it and boot it in QEMU
bafc program.baf --check        # analyse only, write nothing
```

On the hosted target `int` is 64-bit; on `i386-freestanding` it is 32-bit.
Everything else in this document behaves identically on both.

## Error messages

The compiler reports as many problems as it can find in one run, each with a
line and column:

```text
program.baf:12:9: error: 'break' can only appear inside a while or for loop
program.baf:20:5: error: function 'area' returns int, but some paths reach the
                         end of its body without a return
program.baf:31:18: error: cannot compare int with str
```

Analysis happens in three passes — name resolution, type inference, then type
checking — so an error in one function does not hide errors in another.
