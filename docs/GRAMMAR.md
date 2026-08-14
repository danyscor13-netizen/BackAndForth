# BackAndForth 0.7.0 grammar

This is the implemented subset, written informally in EBNF. Prose descriptions
of everything below are in `docs/LANGUAGE.md`.

```text
source-file   = { include | function | begin-block } EOF ;
include       = "[" "include" string "]" newline ;

program       = { function | begin-block } EOF ;
function      = [ "static" ] "func" "->" identifier
                "(" [ parameters ] ")" [ return-type ] block ;
return-type   = ( ":" | "->" ) ( type | "void" ) ;
parameters    = parameter { "," parameter } ;
parameter     = identifier [ ":" type ] ;
begin-block   = "begin" block ;

type          = "int" | "str" | "string" | "bool" ;
block         = "{" { separator | statement } "}" ;
separator     = newline | ";" ;

statement     = variable
              | assignment
              | call
              | if
              | while
              | for
              | switch
              | return
              | "break" separator
              | "continue" separator ;

variable      = type identifier [ "=" expression ] separator ;
assignment    = identifier assign-op expression separator ;
assign-op     = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
call          = qualified-name "(" [ arguments ] ")" separator ;

if            = "if" "(" expression ")" block
                { elsif-clause } [ else-clause ] ;
elsif-clause  = ( "elsif" | "elif" | "else" "if" ) "(" expression ")" block ;
else-clause   = "else" block ;

while         = "while" "(" expression ")" block ;
for           = "for" "(" [ simple-statement ] ";" [ expression ] ";"
                [ simple-statement ] ")" block ;
simple-statement = variable | assignment | call ;   (* without the separator *)

switch        = "switch" "(" expression ")" "{"
                { case | default-case } "}" ;
case          = "case" literal block ;
default-case  = "default" block ;

return        = "return" [ expression ] separator ;

arguments     = argument { "," argument } ;
argument      = [ identifier ":" ] expression ;

(* Loosest to tightest. Every binary operator is left associative. *)
expression     = or-expression ;
or-expression  = and-expression { "||" and-expression } ;
and-expression = equality { "&&" equality } ;
equality       = relational { ( "==" | "!=" ) relational } ;
relational     = additive { ( "<" | "<=" | ">" | ">=" ) additive } ;
additive       = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative = unary { ( "*" | "/" | "%" ) unary } ;
unary          = ( "!" | "-" ) unary | primary ;

primary       = integer
              | string
              | "true"
              | "false"
              | identifier
              | call-expression
              | "(" expression ")" ;
call-expression = qualified-name "(" [ arguments ] ")" ;
qualified-name  = identifier { "." identifier } ;
literal       = integer | string | "true" | "false" ;
```

## Typing rules

```text
int  <op> int   -> int      for + - * / %
int  <op> int   -> bool     for < <= > >=
T    <op> T     -> bool     for == != where T is int, str or bool
bool <op> bool  -> bool     for && || (both short circuit)
!bool           -> bool
-int            -> int
str + str       -> str      concatenation
str + int       -> str      the int side is converted
str + bool      -> str      the bool side is converted
```

`x / 0` and `x % 0` are defined to produce `0`.

## Variadic built-ins

```text
putsc(values: str|int|bool...) -> void
putl(values: str|int|bool...) -> void
inpt(prompt_values: str|int|bool...) -> str
```

Arguments are evaluated and printed left to right. Named arguments are not
accepted by variadic built-ins.

## Fixed built-ins

```text
clearc() -> void
Console.Clear() -> void
Console.SetTextColor(color: int) -> void
Console.SetTextBackgroundColor(color: int) -> void

Str.Length(text: str) -> int
Str.Concat(left: str, right: str) -> str
Str.Sub(text: str, start: int, count: int) -> str
Str.FromInt(value: int) -> str
Str.FromBool(value: bool) -> str
Str.ToInt(text: str) -> int
Str.Equals(left: str, right: str) -> bool

Math.Abs(value: int) -> int
Math.Min(left: int, right: int) -> int
Math.Max(left: int, right: int) -> int

Power.Shutdown() -> void
Power.Reboot() -> void
Disk.Scan() -> void
Disk.Count() -> int
Disk.List() -> void
Disk.Hex(disk: int, lba: int) -> void
Disk.Select(disk: int) -> bool
Disk.Format() -> bool
Disk.Files() -> void
Disk.Write(name: str, content: str) -> bool
Disk.Read(name: str) -> str
Disk.Rem(name: str) -> bool
Disk.Exists(name: str) -> bool
Disk.Size(name: str) -> int
Disk.Info() -> void
Disk.CreateDir(name: str) -> bool
Disk.GotoDir(name: str) -> bool
Disk.GetDir() -> str
```

The string and maths calls also accept lowercase `str.*` and `math.*`
spellings, and the filesystem calls accept the documented `disk.*` aliases.
User-defined functions may return `int`, `str`, `bool` or `void`.
