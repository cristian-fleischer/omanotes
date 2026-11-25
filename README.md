# Omanotes

A Markdown notes app for Linux: an editor that styles Markdown inline as you
type, and a sidebar listing a folder of `.md` files. No GTK, no Chromium, no
browser engine.

It builds on [omawrite](https://github.com/omacom/omawrite) by David Heinemeier
Hansson and adds a notes sidebar, tables, syntax highlighting, collapsible YAML
headers and drafts per note, while being a third smaller, using less memory and
starting faster:

| | Omanotes | omawrite 0.7.8 |
| --- | --- | --- |
| Binary | 769 KB | 1,224 KB |
| Short note on screen | 0.12 s | 0.16 s |
| 62 KB note on screen | 0.15 s | 0.20 s |
| Memory, 62 KB note | 108 MB | 120 MB |

[How it is measured](#footprint) is further down, and what omawrite has that this
does not: it speaks 49 languages, Omanotes only English. Nothing at runtime
outside Qt 6. That makes it a quick viewer for any `.md` file too: `omanotes file.md`, or a
double-click once it is the default.

The buffer always holds your exact Markdown. Styling is applied over the source,
never a conversion, and saving writes the bytes you see back unchanged.

![Omanotes showing a collapsed YAML header, headings, lists, tasks, a table,
syntax-highlighted code and a box-drawing diagram](docs/omanotes.png)

That note is `contrib/showcase.md`. Open it to see what the editor draws over
plain Markdown.

From omawrite, MIT: the editing surface, atomic writes, crash recovery, portal
dialogs and following the desktop's palette, dark mode and text size. What
Omanotes adds:

- Markdown styled inline over the source: headings, bold, italic, strike,
  links, rules, bullets, checkboxes, code blocks and tables
- Syntax highlighting inside fenced blocks, from a statically linked Lexilla
- A vault sidebar: folder tree, filter, and content search through ripgrep
- Per-note drafts that survive switching notes, closing the window and a crash
- YAML headers that collapse to one row
- A source view of the note as written
- Tables drawn aligned however they were typed, with cells that carry their
  own bold, italic, links and code spans; the bytes are padded only on request
- Zoom, column width, fonts and line heights all in a plain INI file

## Install

### Arch and derivatives

From the release:

```sh
curl -LO https://github.com/cristian-fleischer/omanotes/releases/download/v0.6.0/omanotes-0.6.0-1-x86_64.pkg.tar.zst
sudo pacman -U omanotes-0.6.0-1-x86_64.pkg.tar.zst
```

From source:

```sh
sudo pacman -S --needed qt6-base qt6-declarative gcc make
git clone https://github.com/cristian-fleischer/omanotes.git
cd omanotes
bin/install          # bin/build, then makepkg -fsi
```

### Debian and Ubuntu

No package yet, so build it and install the three files by hand:

```sh
sudo apt install build-essential git \
    qt6-base-dev qt6-declarative-dev qt6-quickcontrols2-dev \
    qml6-module-qtquick-controls qml6-module-qtquick-controls-material \
    qml6-module-qtquick-dialogs qml6-module-qtquick-layouts \
    xdg-desktop-portal-gtk ripgrep

git clone https://github.com/cristian-fleischer/omanotes.git
cd omanotes
bin/build

sudo install -Dm755 build/omanotes /usr/local/bin/omanotes
sudo install -Dm644 pkgbuild/omanotes.desktop /usr/local/share/applications/omanotes.desktop
sudo install -Dm644 pkgbuild/omanotes.svg /usr/local/share/icons/hicolor/scalable/apps/omanotes.svg
sudo update-desktop-database /usr/local/share/applications
```

`qml6-module-qtquick-controls-material` is the one people forget. Debian ships
the Material style separately from Quick Controls, and the app asks for it by
name. `ripgrep` is optional and only speeds up the sidebar's content search.

Package names verified against Debian trixie and Ubuntu 24.04. The build itself
has not been run on either, so treat this as the shape of it rather than a
tested recipe, and say so if it does not work.

### Anywhere else

`bin/build` needs `qmake6`, `make` and a C++17 compiler, so it works anywhere
Qt 6 does. It is developed against Qt 6.12; nothing in it is known to need a
version that new, but no older Qt has been tested against.

`bin/build` alone leaves the binary in `build/omanotes` without packaging, and
`bin/test` runs the suite offscreen.

It installs as `omanotes` and shares nothing with `omawrite`: separate binary,
desktop file, icon and settings path, so both can be installed side by side.

## What it renders

The buffer is always your Markdown. These are drawn over it, never in it.

| Written | Shown |
| --- | --- |
| `# ` to `###### ` | Six heading sizes, 1.9x down to 1.0x, the top three at regular weight and the deeper ones fading. Markers hidden. |
| `**b**`, `*i*`, `***bi***`, `~~s~~` | Bold, italic, both, struck through. Markers hidden. |
| `[text](url)`, `![alt](src)` | The text, underlined in the accent colour. Ctrl+click opens it. |
| `` `code` `` | A rounded chip drawn behind the code, in the shade the sidebar uses. Backticks hidden. |
| ` ```lang ` | A slab behind the code, syntax highlighted, the fence rows folded away. |
| `---`, `***`, `___` | A rule across the page, which goes while the caret is on the line: `***` opens bold italic too. |
| `---` YAML `---` at the top | One row naming its keys. Click it to open the header, highlighted as YAML, and again to close it. Closed whenever a note is opened; a search match inside opens it. |
| `* item` | A bullet drawn in the asterisk's own cell. `-` and `+` are left alone. |
| `- [ ]`, `[ ]` | A checkbox, with or without a list marker. Click it to toggle. |
| `\| a \| b \|` | A table: slab, bold header, a rule under it, column rules, and every column lined up whether or not the source is. |

Anything folded away comes back when the caret is on its line, or anywhere
inside the fenced block, so it can still be edited.

Emphasis may wrap a code span, so ``**`x`**`` is bold code. It may not start or
end inside one: a `*` between backticks is a character.

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

Every table is drawn with its columns lined up, whatever its source looks like.
`|a|b|`, `| a | b |` and a row indented past the margin all come out the same,
under a grid, and the bytes stay exactly as typed. The rendering does what the
aligner would have done to the file: each cell is drawn one space in, padded to
its column, one space out, with the difference made up in the advance of a
glyph rather than in characters. Nothing on disk changes for it, and nothing is
marked modified.

`Ctrl+Shift+T` pads the source of the table under the caret, for when you want
the bytes aligned too, in another editor or in a diff. Because the layout and
the aligner read the table the same way, nothing on screen moves when it runs.
One undo puts the table back as it was written, and a table that already lines
up is left alone rather than rewritten to the same bytes. That is the only time
the app touches a table's bytes: not on open, not on typing, not when the caret
leaves.

Putting the caret in a table, or on the line under it, shows it as written,
pipes and all, without a grid, and the columns stop lining up if the source does
not. Moving further out draws it aligned again.

A row is any line that starts with a pipe. The closing pipe is optional, as in
GFM, so a row being typed stays a row while each new cell is started. A row
without one ends in a cell, drawn and lined up like the others; `Ctrl+Shift+T`
closes it. The separator needs a dash, so a row of empty cells is a row.

`Enter` in a table adds an empty row with every pipe written and puts the caret in
its first cell. From the header the row goes under the separator. `Enter` on an
empty row leaves the table with a blank line, the way it leaves a list.
`Shift+Enter` still inserts a bare newline.

Cells carry bold, italic, strikethrough, links and code spans like the rest of a
note. A column lines up because every glyph on the row has the same advance, and
a monospace family keeps one advance across all four of its faces, so the styling
itself moves nothing; the markers around it do go away, and the width they give
up is padding like any other.

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

Drag a note onto a folder to file it there. Hold over a closed folder and it
opens so you can drop inside, the list scrolls when you reach an edge, and a
label follows the cursor saying where the note would land. Dropping past the
last row files it in the vault root. Folders, section labels and drafts do not
travel: a draft has no name of its own yet, and saving it is what files it.

Right-click a row for the rest: a new note in that folder, move to another
folder, or delete. Deleting goes to the desktop trash, after a confirmation, so
it can be put back.

Notes with unsaved changes are marked with a dot. `Ctrl+Shift+R` throws those
changes away and reads the note back off the disk, after asking; the same is on
the right-click menu of any note carrying a dot, open or not.

## Reading a single file

`omanotes some-note.md` opens that file with the sidebar out of the way, so the
document gets the window. It is the same app either way, and `Ctrl+L` brings the
vault out whenever you want it. Reading a file does not decide anything for the
next launch: unless you touch the sidebar, the vault window opens the way you
left it. The desktop entry claims `text/markdown`, so this is also what a
double-click gets once `xdg-mime default omanotes.desktop text/markdown` is set.

## Shortcuts

- `Ctrl+S` saves. Unsaved documents use the XDG desktop portal file picker.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file through the portal picker.
- `Ctrl+L` shows and hides the sidebar, as does the leftmost footer icon. `Ctrl+Shift+L` focuses its filter field.
- `Ctrl+Alt+N` creates a new note where you are pointing: the folder of the row picked in the sidebar, else the folder the open note lives in, else the vault root.
- `Del` deletes the note picked in the sidebar, after asking. With no row picked it stays an ordinary editing key.
- `Ctrl+=` and `Ctrl+-` scale the text, `Ctrl+0` puts it back, `Ctrl+wheel` does the same with the mouse.
- A wheel notch scrolls by the desktop's wheel-scroll-lines, 3 unless the platform theme says otherwise, of prose at the current font and line height, on a browser's easing curve. A touchpad scrolls pixel for pixel as the compositor sends it.
- `Ctrl+M` drops the 65-character measure and lets the text use the window, as does the rightmost footer icon.
- `Ctrl+U` toggles source view, as does the `</>` footer icon: the note as written, in one font, with no markers folded away and no slabs, grids, chips or bullets drawn over it. Code and tables keep their inset and line height, because those are block formats and changing them would go on the undo stack. Not remembered across launches.
- `Ctrl+Shift+F` picks the editor font.
- `Ctrl+Shift+T` lines up the source of the table under the caret.
- `Ctrl+click` opens a link, a bare URL, or a path that exists next to the note.
- `Ctrl+Shift+R` throws away the unsaved changes and reads the note back off the disk.
- `Ctrl+P` opens the system print dialog.
- `Ctrl+N` opens a new Omanotes window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` and `F11` toggle fullscreen. Qt maps the first as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match, from wherever the caret is. The bar can stay open while you edit: the matches follow the text.
- `Ctrl+H` opens find and replace. Replacing goes on to the next match.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+K` insert bold, italic, and link Markdown.
- `Ctrl+?` shows the keyboard shortcut reference.

Unsaved text is kept without asking. Closing the window keeps it; switching
to another note keeps it under the note it belongs to and gives it back when you
return. Every note holding a draft carries a dot in the sidebar until it is
saved, and the file on disk is untouched throughout. The same snapshot covers a
crash. A draft remembers which version of its file it was written against: if
the file has been written by something else since, opening the note asks whether
to keep your text or take the file. A note you only looked at is never a draft.

Omanotes watches the open file and warns before an external change can replace
local work.

Zoom, the width toggle, the editor font, the window's size and whether it was
left full screen all come back on the next run.

## Line height and the code slab

Prose and table rows are set at 140 percent of their own size, and fenced code
at 100, the font's own spacing, which is what lets box-drawing characters tile
into continuous rules. All three are settings under `typography/` in the config,
since the right answer depends on the font.

The background behind a fenced block is drawn as one rectangle behind the
editor, not as a background on the characters, so it covers short lines, empty
lines and the leading between rows alike. Its colour is a step down in lightness
from the page, so it follows the palette rather than sitting on top of it.

Body text is the palette's foreground taken a tenth of the way towards the
page. Headings, bold and table headers keep the foreground itself, which is
what makes them read as strong; at full contrast a page of prose glared and
they had nothing to stand out from.

Code and tables sit inside their slab rather than against its edges:
`typography/blockPadding` insets their text as a block margin and bleeds the
slab out by the same amount, so the space is equal on all four sides. Prose
stays flush with the column. Where prose runs straight into a fence or a table
with no blank line between, the run reserves twice that above or below itself:
half for the slab to bleed into, half as page between the slab's edge and the
text, the same distance the text inside keeps from that edge. A blank line is
room enough on its own and gets nothing extra; it comes to about the same gap.

## Fonts

Three surfaces need a font, and each is chosen the same way: what you set for
Omanotes, then what your desktop is set to, then the font in the binary.

| Surface | Config knob | Then | Then |
| --- | --- | --- | --- |
| Writing | `view/fontFamily`, `Ctrl+Shift+F` | the desktop's monospace | bundled |
| Chrome: sidebar, footer, dialogs | `view/interfaceFontFamily` | the desktop's interface font | bundled |
| Code and tables | `typography/codeFontFamily` | the writing font, then the desktop's monospace | bundled |

"What your desktop is set to" is asked of the desktop rather than guessed:
`kdeglobals` first, which is where Plasma keeps it and where Qt's own KDE
platform theme looks; then GNOME's `org.gnome.desktop.interface`; then
whatever Qt's active platform theme says, which on a machine with no desktop
settings at all is fontconfig's `monospace` and `sans-serif`. Each answer has to
name a family that is actually installed, or the next one is tried.

Code has one requirement the others do not: box-drawing characters must join
into continuous rules. Having the glyphs is not enough — the ink has to be at
least as tall as the line spacing, or a diagram in a fence comes out dashed.
Noto Sans Mono is the one common monospace that fails this, at 0.918. So the
code font walks a longer chain: the writing font if it tiles, the desktop's
monospace if it tiles, then DejaVu Sans Mono, Liberation Mono, Adwaita Mono or
Consolas, then any installed fixed-pitch family that tiles, then the bundled
one, which does.

The bundled font is JetBrains Mono NL 2.304, SIL Open Font License 1.1,
compiled into the binary so it is there whether or not it is installed. NL is
the variant without ligatures: a writing surface is the wrong place for `->` to
turn into an arrow behind your back. The four faces are subset to the scripts a
Markdown note uses and packed as one collection, which is what keeps them at 138
KB rather than 405 KB; see `fonts/README.md` for the range and how to regenerate
them. They are unpacked when the font is used, and not before.

Text also follows the desktop text size — `omarchy display text size`, or GNOME's
`text-scaling-factor` — and re-flows without a restart. The default of 12px leaves
the app at the size it is designed around; larger and smaller sizes scale from
there, and the zoom multiplies on top.

## Footprint

Measured on Arch with Qt 6.12, Hyprland, an AMD iGPU and the performance power
profile, against upstream omawrite 0.7.8 on the same machine and the same files.
Startup is wall-clock from exec to the window on screen, the median of fifteen
shuffled runs. Memory is four seconds after that.

| | Omanotes | omawrite |
| --- | --- | --- |
| Binary | 769 KB | 1,224 KB |
| Installed package | 777 KB | 1,237 KB |
| Download | 457 KB | 452 KB |
| Window on screen, short note | 0.12 s | 0.16 s |
| Window on screen, 62 KB note | 0.15 s | 0.20 s |
| PSS, short note idle | 99 MB | 114 MB |
| PSS, 62 KB note idle | 108 MB | 120 MB |

Up to 183 KB of omawrite's binary are its translations, in 49 languages. Omanotes
is in English only.

An empty Qt Quick window with the same style takes about 0.1 s here, so most of
what is left is Qt, the platform theme and the graphics driver. What keeps the rest
small:

- The driver is opened on a thread of its own while the interface is read.
- A note goes in with its formats, as one edit, and is laid out once. Only the
  lines in view are handed to the scene graph.
- Dialogs, menus, the search bar and the sidebar are made when they are first
  shown. The bundled font is unpacked when it is first used.
- The desktop's colour scheme, text size and fonts are asked for in one D-Bus
  call.
- Memory the allocator no longer needs goes back once a note is open.
- `-Os`, link-time optimisation, section garbage collection, packed relocations,
  and the four font faces compressed as one. All of it is set in `omanotes.pro`
  and `lexilla.pri` with the reasoning.

Power-saver roughly doubles the times. Most of the memory is neither app: some
40 MB is the Mesa GL stack the scene graph pulls in, which
`QT_QUICK_BACKEND=software` removes at the cost of frame rate. Nothing polls:
idle CPU is a few hundredths of a second per twelve at most.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative` (which ships QtQuick Controls). Arch has
  no `qt6-quickcontrols2` package; Quick Controls lives inside `qt6-declarative`.
- `xdg-desktop-portal` and a backend for it, for the file and font pickers
- `ripgrep`, optional. Without it the sidebar's content search falls back to
  `grep`.

Nothing else. Lexilla is vendored and linked statically, and the font is
compiled into the binary.

JetBrains Mono NL is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt` for the licence and `fonts/README.md` for what was subset out of
it. The font is copyright 2020 The JetBrains Mono Project Authors.
