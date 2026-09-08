# OmaNote

A Markdown notes app for Linux: an editor that styles Markdown inline as you
type, and a sidebar listing a folder of `.md` files. No GTK, no Chromium, no
browser engine. Qt 6 and about 600 KB.

The buffer always holds your exact Markdown. Styling is applied over the source,
never a conversion, and saving writes the bytes you see back unchanged.

Fork of [omawrite](https://github.com/omacom/omawrite) by David Heinemeier
Hansson, MIT. The editing surface, autosave, atomic writes, crash recovery,
portal dialogs and theme following all come from there.

## Install

```sh
bin/install     # builds, then makepkg -fsi
```

Installs as `omanote`, alongside `omawrite` if you have it.

## The vault

The sidebar lists every `.md` and `.markdown` file under one directory, at full
depth, skipping dotfiles and dot-directories. Set it from the folder button in
the sidebar footer; the default is `$HOME/Notes`.

## Shortcuts

- `Ctrl+S` saves. Unsaved documents use the XDG desktop portal file picker.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file through the portal picker.
- `Ctrl+L` shows and hides the sidebar, as does the leftmost footer icon. It starts hidden. `Ctrl+Shift+L` focuses its filter field.
- `Ctrl+Alt+N` creates a new note in the vault root.
- `Ctrl+P` opens the system print dialog.
- `Ctrl+N` opens a new OmaNote window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+H` opens find and replace.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+K` insert bold, italic, and link Markdown.
- `Ctrl+?` shows the keyboard shortcut reference.

Closing the window never loses an unsaved draft and never asks about it. What
you typed is written to a snapshot and comes back the next time the app opens,
with the file on disk untouched, the way Sublime Text's hot exit works. The same
snapshot covers a crash. Switching to another note still asks, since that buffer
is about to be replaced.

OmaNote watches the open file and warns before an external change can replace
local work.

Text follows the desktop text size — `omarchy display text size`, or GNOME's
`text-scaling-factor` — and re-flows without a restart. The default of 12px leaves
the app at the size it is designed around; larger and smaller sizes scale from there.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative` (which ships QtQuick Controls)
- `xdg-desktop-portal` and a portal backend

The iA Writer Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright Information Architects Inc. and based on
IBM Plex, copyright IBM Corp.
