# Material icon theme

Alternative icon set selectable in *Preferences → General → Icon theme*.

The icons are [Material Symbols](https://fonts.google.com/icons) (Rounded,
weight 400, 24 px design grid) by Google, licensed under the Apache License 2.0
(see `LICENSE` in this directory).

Each classic icon `<name>` in `../button/` has two variants here:

- `mdl_<name>_<size>.png` — dark ink (`#2B303A`), used with light system themes
- `mdd_<name>_<size>.png` — light ink (`#E6E8EC`), used with dark system themes

`tools/respack.py` pairs them with the classic icons, and
`libresrc_getbitmapbundle()` swaps them in when the Material theme is active.
Icons without a Material variant keep their classic look.
- `icon-map.json` lists which Material Symbol is used for each icon.
