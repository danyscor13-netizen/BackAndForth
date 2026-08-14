# BackAndForth include system

## Syntax

```baf
[include "relative/or/absolute/path.baf"]
```

A directory is also accepted:

```baf
[include "commands"]
```

A directory include loads its direct `.baf` children in lexicographic filename order. It does not recurse automatically; nested directories must be included explicitly from a source file.

## Path resolution

Paths are resolved relative to the file containing the directive:

```text
project/main.baf
project/lib/messages.baf
project/lib/format/helpers.baf
```

```baf
// project/main.baf
[include "lib/messages.baf"]
```

```baf
// project/lib/messages.baf
[include "format/helpers.baf"]
```

## Semantics

- The included source is expanded before lexing and parsing.
- Functions in included files are callable by the root program.
- Nested includes are supported.
- The same canonical file is expanded only once.
- Circular include chains are rejected.
- Included source may define functions, but the complete expanded program may contain only one `begin` block.
- `static func` keeps LLVM internal linkage.

## Restrictions in 0.6.0

- An include directive must occupy its own line.
- The path must be quoted with `"`.
- Optional whitespace and a trailing `// comment` are accepted.
- Errors after expansion currently use the root source path for lexer/parser diagnostics; preprocessing errors show the actual included path.
- There is no namespace or module isolation yet, so function names must remain unique across the expanded program.
