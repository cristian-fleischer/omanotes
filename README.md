# Omanotes

A Markdown notes app for Linux: an editor that styles Markdown inline as you
type, and a sidebar listing a folder of `.md` files. No GTK, no Chromium, no
browser engine.

Lean and quick on purpose. One 871 KB binary, nothing at runtime outside Qt 6,
and a note on screen in well under half a second: 0.26 s for a short note and
0.40 s for a 62 KB one, wall clock from exec to the window. The numbers are
measured, and there is a [table of them](#footprint) further down.

The buffer always holds your exact Markdown. Styling is applied over the source,
never a conversion, and saving writes the bytes you see back unchanged.

![Omanotes showing headings, lists, tasks, a table, syntax-highlighted code and a
box-drawing diagram](docs/omanotes.png)

That note is `contrib/showcase.md`. Open it to see what the editor draws over
plain Markdown.

Based on [omawrite](https://github.com/omacom/omawrite) by David Heinemeier
Hansson, MIT. The editing surface, autosave, atomic writes, crash recovery,
portal dialogs and theme following all come from there. What Omanotes adds is
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
curl -LO https://github.com/cristian-fleischer/omanotes/releases/download/v0.1.0/omanotes-0.1.0-1-x86_64.pkg.tar.zst
sudo pacman -U omanotes-0.1.0-1-x86_64.pkg.tar.zst
```

From source:

```sh
sudo pacman -S --needed qt6-base qt6-declarative gcc make
git clone https://github.com/cristian-fleischer/omanotes.git
cd omanotes
bin/install          # bin/build, then makepkg -fsi
```

`bin/build` alone leaves the binary in `build/omanotes` without packaging, and
`bin/test` runs the suite offscreen.

It installs as `omanotes` and shares nothing with `omawrite`: separate binary,
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
| `` `code` `` | A chip a step brighter than the page, the backticks holding a space of it either side rather than showing. |
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

A table is padded so its columns line up when you move the caret out of one, or
on `Ctrl+Shift+T`. Putting the caret in a table is what counts as working on it;
opening a note, reading it and scrolling past change nothing, so a file you only
read stays byte for byte what it was. One undo puts the table back as it was
written, and a table that already lines up is left alone entirely rather than
rewritten to the same bytes.

Column rules are drawn only through a table that lines up: half a grid reads
worse than none. A table with no grid keeps everything it was written with, the
separator row included, because there is no rule drawn to stand in for it.

## The vault

The sidebar shows every `.md` and `.markdown` file under one directory as a
folder tree, at full depth, skipping dotfiles and dot-directories. Which folders
are closed is remembered, and opening a note inside a closed one opens the way
down to it. Typing in the filter flattens the tree to the matches. Set the root
from the folder icon in the sidebar footer; the default is `$HOME/Notes`.

The filter matches the path as you type, and a moment later also what is written
inside the notes, using ripgrep if it is installed and grep otherwise. Those two
kinds of match are listed as two groups.

A new note has no name yet, so it starts as a draft in `.omanotes/drafts` inside
the folder you made it in, never in the vault proper. Each folder holding drafts
grows a `Drafts` row above its own folders and notes, which opens and closes
like any other and stays closed if you leave it closed. Drafts are labelled by
their first line, and are written as you type so the label survives switching
notes and closing the app.
Saving one names it and moves it out: `# Meeting notes` becomes
`Meeting notes.md` in the folder that held the drafts directory. Save As offers
the same name rather than "draft".

The filter matches note names first and lists what ripgrep found inside the
notes below, under a heading of its own, because a name is the stronger answer.

Right-click a row for the rest: a new note in that folder, move to another
folder, or delete. Deleting goes to the desktop trash, after a confirmation, so
it can be put back.

Notes with unsaved changes are marked with a dot. `Ctrl+Shift+R` throws those
changes away and reads the note back off the disk, after asking; the same is on
the right-click menu of any note carrying a dot, open or not.

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
- `Ctrl+Shift+R` throws away the unsaved changes and reads the note back off the disk.
- `Ctrl+P` opens the system print dialog.
- `Ctrl+N` opens a new Omanotes window.
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

Omanotes watches the open file and warns before an external change can replace
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

## Footprint

Measured on Arch with Qt 6.11, a Wayland session and an AMD iGPU, against
upstream omawrite 0.5.0 on the same machine and the same files. Startup is
wall-clock from exec to the window on screen, best of five.

| | Omanotes | omawrite |
| --- | --- | --- |
| Binary | 871 KB | 572 KB |
| Installed package | 877 KB | 565 KB |
| Window on screen, short note | 0.26 s | 0.21 s |
| Window on screen, 62 KB note | 0.40 s | 0.26 s |
| PSS, short note idle | 86 MB | 71 MB |
| PSS, 62 KB note idle | 118 MB | 99 MB |
| CPU idle, per 12 s | 0.01 s | 0.00 s |

200 KB of the binary is the bundled font, compressed, and it comes from
upstream. Of the code, measured before link-time optimisation folds it
together: 379 KB is Lexilla's nine lexers, 333 KB this fork's own, and 170 KB
upstream's editor. Compressing the resources, link-time optimisation with section garbage
collection, and building Lexilla for size rather than speed are worth about
450 KB between them. All three are set in `omanotes.pro` and `lexilla.pri`
with the reasoning.

Most of the memory is neither app: 38 MB of the 118 MB is the Mesa GL stack the
scene graph pulls in, which `QT_QUICK_BACKEND=software` removes at the cost of
frame rate. Qt's own libraries account for 16 MB of it, and the whole of
Omanotes for about 19 MB more than upstream.

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
