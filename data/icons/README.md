# KGlanceQT icon

Original AGPL-3.0-only artwork: a folded file with an eye for quick previewing.
The blue gradient and restrained shapes sit alongside Breeze application icons;
the mint iris distinguishes it from a generic document icon. White and dark eye
shapes keep their contrast on both light and dark panels.

`org.kglanceqt.KGlanceQT.svg` is the scalable source. The `-small.svg` source
simplifies the page, fold and eye for 16/22/24-pixel use. The PNG variants are
committed so installation and development fallback icons do not require Qt SVG.
Render with the distribution's librsvg tools after changing either source:

```sh
for size in 16 22 24 32 48 64 128 256; do
    source=org.kglanceqt.KGlanceQT.svg
    if [ "$size" -le 24 ]; then source=org.kglanceqt.KGlanceQT-small.svg; fi
    rsvg-convert -w "$size" -h "$size" "$source" -o "org.kglanceqt.KGlanceQT-$size.png"
done
```

Visual review uses the native 16–128-pixel assets on `#eff0f1` and `#232629`
backgrounds, plus the actual window icon on the current 2× Wayland display.
The desktop files, installed icon names and embedded fallback use the same
artwork. Full-size sources should not replace the small variant at small sizes.
