# Following the desktop palette without Omarchy

Omanotes reads `~/.local/state/omarchy/current/theme/colors.toml`, the file
Omarchy writes when the theme changes. On a machine without Omarchy that file
does not exist and the app falls back to its built-in palette.

`omarchy-colors.toml` is a [matugen](https://github.com/InioX/matugen) template
that generates it from the same Material palette DankMaterialShell derives from
the wallpaper. Teaching the app a second theme source would be more code than
generating the file it already reads.

```sh
install -Dm644 contrib/matugen/omarchy-colors.toml \
    ~/.config/matugen/templates/omarchy-colors.toml
```

Then add the template to `~/.config/matugen/config.toml`:

```toml
[config]

[templates.omarchy]
input_path = "~/.config/matugen/templates/omarchy-colors.toml"
output_path = "~/.local/state/omarchy/current/theme/colors.toml"
```

DMS renders user matugen templates on every theme change while its
`runUserMatugenTemplates` setting is on, which is the default. To generate the
file once without waiting for a wallpaper change:

```sh
dms ipc call theme dark    # or light, whichever is current
```

Omanotes watches the file and the two directories above it, so a regenerated
palette reaches an open window without a restart.

| Key | Matugen colour | Used for |
| --- | --- | --- |
| `mode` | the render mode | light or dark, which also picks the marker and code-block colours |
| `background` | `background` | the page |
| `foreground` | `on_background` | body text, headings, bold |
| `accent` | `primary` | links, the current note in the sidebar, the Material accent |
| `selection` | `secondary_container` | the selection fill behind selected text |
