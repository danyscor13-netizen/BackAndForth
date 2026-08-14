# BackAndForth Studio

A single-file editor for `.baf` sources, built on
[Monaco](https://microsoft.github.io/monaco-editor/) — the editor from VS Code.
It lives at `tools/editor/index.html`, and the Windows installer copies it to
`<install dir>\editor\index.html`.

Open it by double-clicking the file. It is one HTML document with no build
step and no server; the only thing it fetches is Monaco itself, from a CDN.

## What it does

**Language support.** Syntax highlighting for every construct in 0.7.0 —
keywords, types, literals, qualified built-in names, strings with escapes,
line and block comments, the `[include "..."]` directive. Bracket matching,
auto-closing pairs, and automatic indentation after `{`.

**Completion.** Ctrl+Space offers keywords, types, all the built-ins with
parameter placeholders, and identifiers already present in the buffer.
Snippets cover the shapes you type most: `begin`, `func`, `funcvoid`, `if`,
`ifelse`, `elsif`, `for`, `while`, `switch`, `include`.

**Hover.** Hovering a built-in shows its signature and what it returns;
hovering a keyword explains it.

**Problems panel.** A checker runs as you type and flags unbalanced brackets,
unterminated strings and block comments, a missing or duplicated `begin`
block, control statements without a `{ ... }` block, and `=` where `==` was
probably meant. Click an entry to jump to it. This is *not* the compiler — it
is the cheap subset that can run in a browser, to shorten the round trip. Run
`bafc file.baf --check` for the real answer.

**Themes.** Fourteen, switchable from the toolbar and remembered between
sessions:

| Dark | Light | High contrast |
| --- | --- | --- |
| Midnight, Ocean, Forest, Ember, Grape, Slate, Nightfall | Daylight, Solar, Paper, Sandstone | Terminal green, High contrast dark, High contrast light |

Every theme colours built-in calls, user function names, types and operators
distinctly, not just the generic token classes.

**Files.** Open and Save use the File System Access API where the browser
supports it (Chrome, Edge), so Save writes straight back to the file you
opened. Elsewhere they fall back to a file picker and a download. The buffer is
also kept in `localStorage`, so closing the tab does not lose your work.

**Build commands.** The side panel shows the four commands you actually run,
with your filename already substituted. Click one to copy it.

**Reindent.** The Format button (Shift+Alt+F) reindents by brace depth. It is
deliberately conservative: it never reflows or rewrites a line, so it cannot
break a program it does not fully understand.

## Keyboard shortcuts

| Key | Action |
| --- | --- |
| Ctrl+S | Save |
| Ctrl+O | Open |
| Ctrl+Space | Completions |
| Shift+Alt+F | Reindent |
| F1 | Monaco command palette |
| Ctrl+F / Ctrl+H | Find / replace |
| Alt+Click | Extra cursor |
| Ctrl+/ | Toggle line comment |

## Offline use

The page loads Monaco from `cdn.jsdelivr.net`. To use it without a network,
download the `monaco-editor` package and point the two `<link>`/`<script>`
tags and the `require.config` path at your local copy:

```bash
npm pack monaco-editor@0.52.2
# unpack it next to index.html as ./vs, then replace every
# https://cdn.jsdelivr.net/npm/monaco-editor@0.52.2/min/vs
# with ./vs
```

## Limits

It edits and checks; it does not compile or run. There is no compiler in the
browser — BackAndForth compiles to LLVM IR and links with clang, neither of
which exists in a web page. Save the file and use `baf` from a terminal.
