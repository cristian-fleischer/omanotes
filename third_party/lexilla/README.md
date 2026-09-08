# Lexilla, vendored

Lexer sources from [Lexilla](https://github.com/ScintillaOrg/lexilla) 553, plus
the three headers Lexilla needs from Scintilla (`ILexer.h`, `Sci_Position.h`,
`Scintilla.h`).

Statically linked, so OmaNote's package still depends only on `qt6-base`,
`qt6-declarative` and `xdg-desktop-portal`. Lexilla itself has no dependencies
beyond the C++ standard library.

Only the lexers the app offers are compiled. Adding a language means copying one
more `lexers/Lex*.cxx` here, listing it in `lexilla.pri`, and mapping it in
`src/lexillacodehighlighter.cpp`.

| File | Languages |
| --- | --- |
| `LexHTML.cxx` | PHP, Blade, HTML, XML |
| `LexCPP.cxx` | JavaScript, JSX, TypeScript, C, C++, Java, C#, Go, Rust |
| `LexBash.cxx` | Bash, sh, zsh |
| `LexJSON.cxx` | JSON |
| `LexPython.cxx` | Python |
| `LexSQL.cxx` | SQL |
| `LexYAML.cxx` | YAML |
| `LexProps.cxx` | .env, ini, conf, properties, toml |

Licence: see `LICENSE`. Copyright 1998-2021 Neil Hodgson.

To update: replace `lexlib/` and the listed `lexers/` from a newer Lexilla
release and re-run `bin/test`. The adapter only uses `LexerModule::Create()`
and `ILexer5::Lex()`, both stable across Lexilla 5.

## Local changes

Kept to a minimum so an update is a straight copy.

| File | Change | Why |
| --- | --- | --- |
| `include/Sci_Position.h` | added `#include <cstdint>` | Upstream expects it to arrive through another header. GCC 16 does not provide it, and `intptr_t` comes out undeclared. |
