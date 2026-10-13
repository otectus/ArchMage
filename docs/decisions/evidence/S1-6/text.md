# S1-6.7 Text shaping probe (M0-E5, text part)

Spec §17.1; plan S1-6.7. Produced on 2026-10-13 with HarfBuzz 14.6.0 and FreeType 2.14.3, at 48 px, using
Noto Sans Arabic Regular and Noto Sans Devanagari Regular from `noto-fonts 1:2026.10.01-1` (system fonts, not
bundled; recorded in `docs/dependencies/manifest.yaml`).

## Command

```
cmake --build --preset dev-debug --target text_probe
build/dev-debug/benchmarks/text_probe <output>
```

Output: [`text/text-probe.txt`](text/text-probe.txt) (glyphs, positions and cluster map) and
[`text/text-probe.png`](text/text-probe.png). The image's rows are Arabic shaped, Arabic naive, Devanagari
shaped and Devanagari naive. Naive means each code point's nominal glyph, left to right, with no shaping. Glyph
coverage is rasterized by FreeType into an ArchMage uint8 layer and composited black over white by the CPU
compositor.

## Result

**Arabic, السلام عليكم ("as-salamu alaykum").** 12 code points shape into 13 glyphs in 12 clusters.
- HarfBuzz detects script `Arab` and direction `rtl`, and returns glyphs in visual order (clusters 21 down to 0).
- Every letter takes its joining form (`init`, `medi`, `fina`).
- Lam-alef is realized by this font as two contextual glyphs (`uni0644.medi.rlig`, `uni0627.fina.rlig.2`), not
  one ligature glyph.
- Yeh (U+064A) is drawn as a dotless base plus a separate two-dots-below mark (a zero-advance glyph with an
  offset), which is why there is one more glyph than code points.
- The naive row shows isolated letters in reversed order: shaping is required.

**Devanagari, नमस्ते दुनिया ("namaste duniya").** 13 code points shape into 11 glyphs in 8 clusters.
- Sa + virama becomes the half form `s-deva` before `ta-deva`.
- Da + u matra becomes the ligature `da_uMatra-deva`.
- The short i matra (U+093F), stored after na, is placed before it (`iMatra-deva.04` then `na-deva`, both in
  cluster 25).
- The naive row shows a visible virama and the matra after its consonant.

## Limitations

- Fixed size, one font per script, no font fallback, no line breaking and no bidirectional run splitting beyond
  HarfBuzz's single-run guess. Lines are left-aligned even when right to left.
- Rasterization is FreeType's 8-bit antialiasing, placed at whole pixels; no subpixel positioning, hinting
  choices or color fonts.
- The text engine proper (layout, editing, fallback, vertical text) is M5 work (spec §17).
