# The bundled font

JetBrains Mono NL 2.304, SIL Open Font License 1.1, in `OFL.txt`. The upstream
release is at https://github.com/JetBrains/JetBrainsMono.

NL is the variant without ligatures. A writing surface is the wrong place for
`->` to turn into an arrow behind your back, and dropping the ligature tables
saves 112 KB.

The four faces here are subsets. The full ones are 405 KB compressed, which is
most of a megabyte-sized binary spent on scripts a Markdown note never uses.
These keep Latin, Latin Extended, Greek, Cyrillic, punctuation, currency,
arrows, box drawing, block elements, geometric shapes and mathematical
operators: everything the editor draws with, and more than the font this
replaced covered. Anything outside that falls back to a system font, as emoji
and CJK already did.

Regenerate with fonttools, from the upstream TTFs:

```sh
RANGES='U+0000-00FF,U+0100-017F,U+0180-024F,U+0250-02AF,U+0300-036F,U+0370-03FF,U+0400-04FF,U+2000-206F,U+2070-209F,U+20A0-20BF,U+2100-214F,U+2190-21FF,U+2200-22FF,U+2300-23FF,U+2500-257F,U+2580-259F,U+25A0-25FF,U+2600-26FF,U+FB00-FB06,U+FEFF'
for face in Regular Italic Bold BoldItalic; do
  pyftsubset "JetBrainsMonoNL-$face.ttf" --output-file="fonts/JetBrainsMonoNL-$face.ttf" \
    --unicodes="$RANGES" --layout-features='kern,mark,mkmk,ccmp,locl' \
    --drop-tables+=DSIG --name-IDs='*' --recalc-bounds
done
```

The OFL declares no Reserved Font Name, so a subset keeps the family name.
