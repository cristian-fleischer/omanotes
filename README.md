# OmaNote

A Markdown notes app for Linux: an editor that styles Markdown inline as you
type, and a sidebar listing a folder of `.md` files. No GTK, no Chromium, no
browser engine. Qt 6 and about 600 KB.

The buffer always holds your exact Markdown. Styling is applied over the source,
never a conversion, and saving writes the bytes you see back unchanged.

![OmaNote showing headings, lists, tasks, a table, syntax-highlighted code and a
box-drawing diagram](docs/omanote.png)

That note is `contrib/showcase.md`. Open it to see what the editor draws over
plain Markdown.

Fork of [omawrite](https://github.com/omacom/omawrite) by David Heinemeier
Hansson, MIT. The editing surface, autosave, atomic writes, crash recovery,
portal dialogs and theme following all come from there. What OmaNote adds is
the vault sidebar, the inline Markdown styling, syntax highlighting, tables,
per-note drafts and the typography knobs described below.

- Markdown styled inline over the source: headings, bold, italic, strike,
  links, rules, bullets, checkboxes, code blocks and tables
- Syntax highlighting inside fenced blocks, from a statically linked Lexilla
- A vault sidebar: folder tree, filter, and content search through ripgrep
- Per-note drafts that survive switching notes, closing the window and a crash
- Tables padded into alignment on request, never on open
- Follows the desktop palette, dark mode and text size
- Zoom, column width, fonts and line heights all in a plain INI file

## Install

From the release, on Arch and derivatives:

```sh
curl -LO https://github.com/cristian-fleischer/omanote/releases/download/v0.1.0/omanote-0.1.0-1-x86_64.pkg.tar.zst
sudo pacman -U omanote-0.1.0-1-x86_64.pkg.tar.zst
```

From source:

```sh
sudo pacman -S --needed qt6-base qt6-declarative gcc make
git clone https://github.com/cristian-fleischer/omanote.git
cd omanote
bin/install          # bin/build, then makepkg -fsi
```

`bin/build` alone leaves the binary in `build/omanote` without packaging, and
`bin/test` runs the suite offscreen.

It installs as `omanote` and shares nothing with `omawrite`: separate binary,
desktop file, icon and settings path, so both can be installed side by side.

There is no package for other distributions. `bin/build` needs only `qmake6`,
`make` and a C++17 compiler, so it works anywhere Qt 6 does; it is developed
against Qt 6.11.

## What it renders

The buffer is always your Markdown. These are drawn over it, never in it.

| Written | Shown |
| --- | --- |
| `# ` to `###### ` | Six heading sizes, 1.9x down to 1.0x, the deeper ones fading. Markers hidden. |
| `**b**`, `*i*`, `***bi***`, `~~s~~` | Bold, italic, both, struck through. Markers hidden. |
| `[text](url)`, `![alt](src)` | The text, underlined in the accent colour. Ctrl+click opens it. |
| ` ```lang ` | A slab behind the code, syntax highlighted, the fence rows folded away. |
| `---` | A rule across the page. |
| `* item` | A bullet drawn in the asterisk's own cell. `-` and `+` are left alone. |
| `- [ ]`, `[ ]` | A checkbox, with or without a list marker. Click it to toggle. |
| `\| a \| b \|` | A table: slab, bold header, a rule under it, and column rules once the source lines up. |

Anything folded away comes back when the caret is on its line, or anywhere
inside the fenced block, so it can still be edited.

## Syntax highlighting

Fenced blocks with a language are coloured by [Lexilla](https://github.com/ScintillaOrg/lexilla),
vendored and linked statically in `third_party/lexilla`, so the package still
depends only on Qt and the portal. PHP and Blade, JavaScript and the C family,
Bash, JSON, YAML, Python, SQL, and the properties family that covers `.env`,
`ini`, `conf` and `toml`. Anything else, `text` included, is left plain.

`src/codesyntaxhighlighter.h` is the whole interface between the Markdown
highlighter and the lexer: a list of languages and a `tokenize()` returning
spans. Replacing Lexilla means writing one more adapter.

## Tables

A table is padded so its columns line up when you type in one and move the caret
out, or on `Ctrl+Shift+T`. Never on open and never on a visit, so a file you only
read stays byte for byte what it was, and one undo puts the table back as it was
written. Column rules are drawn only through a table that lines up: half a grid
reads worse than none.

## The vault

The sidebar shows every `.md` and `.markdown` file under one directory as a
folder tree, at full depth, skipping dotfiles and dot-directories. Which folders
are closed is remembered, and opening a note inside a closed one opens the way
down to it. Typing in the filter flattens the tree to the matches. Set the root
from the folder icon in the sidebar footer; the default is `$HOME/Notes`.

The filter matches the path as you type, and a moment later also what is written
inside the notes, using ripgrep if it is installed and grep otherwise.

Notes with unsaved changes are marked with a dot, and a draft that has no file
behind it yet gets a row of its own at the top.

## Shortcuts

- `Ctrl+S` saves. Unsaved documents use the XDG desktop portal file picker.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file through the portal picker.
- `Ctrl+L` shows and hides the sidebar, as does the leftmost footer icon. It starts hidden. `Ctrl+Shift+L` focuses its filter field.
- `Ctrl+Alt+N` creates a new note in the vault root.
- `Ctrl+=` and `Ctrl+-` scale the text, `Ctrl+0` puts it back, `Ctrl+wheel` does the same with the mouse.
- `Ctrl+M` drops the 65-character measure and lets the text use the window, as does the rightmost footer icon.
- `Ctrl+Shift+F` picks the editor font.
- `Ctrl+click` opens a link, a bare URL, or a path that exists next to the note.
- `Ctrl+P` opens the system print dialog.
- `Ctrl+N` opens a new OmaNote window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+H` opens find and replace.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+K` insert bold, italic, and link Markdown.
- `Ctrl+?` shows the keyboard shortcut reference.

Nothing ever asks you about unsaved text. Closing the window keeps it; switching
to another note keeps it under the note it belongs to and gives it back when you
return. Every note holding a draft carries a dot in the sidebar until it is
saved, and the file on disk is untouched throughout. The same snapshot covers a
crash.

OmaNote watches the open file and warns before an external change can replace
local work.

Zoom, the width toggle, the editor font, the window's size and whether it was
left full screen all come back on the next run.

## Line height and the code slab

Prose is set at 140 percent of its own size. Table rows are tighter at 120 so
they read as a grid, and fenced code sits at 100, the font's own spacing, which
is what lets box-drawing characters tile into continuous rules. All three are
settings under `typography/` in the config, since the right answer depends on
the font.

The background behind a fenced block is drawn as one rectangle behind the
editor, not as a background on the characters, so it covers short lines, empty
lines and the leading between rows alike. Its colour is a step down in lightness
from the page, so it follows the palette rather than sitting on top of it.

Code and tables sit inside their slab rather than against its edges:
`typography/blockPadding` insets their text as a block margin and bleeds the
slab out by the same amount, so the space is equal on all four sides. Prose
stays flush with the column.

## Fonts

The bundled font is iA Writer Mono S, under the SIL Open Font License 1.1. It is
compiled into the binary, so it is there whether or not it is installed on the
system.

`Ctrl+Shift+F` picks a different family for the writing surface; the footer and
the dialogs stay on the bundled one unless `view/interfaceFontFamily` names
another, which is a config knob only.

Fenced code is rendered in a font of its own, because iA Writer Mono S has no
box-drawing glyphs and a diagram in a fence needs them to join into continuous
rules. The choice is the editor's font when it can draw them, otherwise the best
installed monospace that can. `typography/codeFontFamily` pins one. Tables use
the editor's font whenever it is monospaced, since a pipe never joins anyway.

Text also follows the desktop text size — `omarchy display text size`, or GNOME's
`text-scaling-factor` — and re-flows without a restart. The default of 12px leaves
the app at the size it is designed around; larger and smaller sizes scale from
there, and the zoom multiplies on top.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative` (which ships QtQuick Controls). Arch has
  no `qt6-quickcontrols2` package; Quick Controls lives inside `qt6-declarative`.
- `xdg-desktop-portal` and a backend for it, for the file and font pickers
- `ripgrep`, optional. Without it the sidebar's content search falls back to
  `grep`.

Nothing else. Lexilla is vendored and linked statically, and the font is
compiled into the binary.

The iA Writer Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright Information Architects Inc. and based on
IBM Plex, copyright IBM Corp.
