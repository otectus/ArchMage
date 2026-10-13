# S1-6.5 PSD decode probe (M0-E4)

Spec §22.2, §26.1; plan S1-6.5. Produced on 2026-10-13.

## Fixture

`layered-gimp-3.2.6.psd` in the corpus directory (`~/.local/share/archmage-dev/corpus/`), registered in
`docs/testing/corpus-plan.md`: 512 × 384, RGB, 8 bits, 117,738 bytes, SHA-256
`acc09c066c9d12619a236ea487787bf9e99628576b431df5fda30aca7b46d8c0`. Authored with GIMP 3.2.6 by
`benchmarks/probes/fixtures/make_psd_fixture.py` (command in the register). Layers, bottom to top: an opaque
background; a red disc in Multiply; a green bar in Screen at 70 %; an orange layer with a layer mask; a group
holding a normal blue square.

## Command

```
cmake --build --preset dev-debug --target psd_probe
build/dev-debug/benchmarks/psd_probe ~/.local/share/archmage-dev/corpus/layered-gimp-3.2.6.psd <output>
```

Output: [`psd/psd-probe.txt`](psd/psd-probe.txt), [`psd/merged.png`](psd/merged.png) (the file's own merged
composite, as GIMP wrote it), [`psd/archmage-flat.png`](psd/archmage-flat.png) (what ArchMage's CPU
compositor renders from the layers Stage 1 can represent).

## Result

- The bounded parser reads the header, five image resources, all 7 layer records (5 raster layers and the
  two section markers GIMP writes for a group), 23 additional-information blocks, the RLE channel data and
  the merged composite, without a bounds failure.
- **Rendered:** 2 of 7 records, the background and the blue square (normal mode, no mask; the group is
  flattened, which is exact for normal layers).
- **Unsupported, listed explicitly (13 records):** the embedded ICC profile (rendered as sRGB); seven `lclr`
  (layer color label) blocks; Multiply and Screen blend modes (Stage 1 composites Normal only); the layer
  mask (masks are M2); the group start and end markers.
- **Agreement:** outside the shapes of the unsupported layers, with a 2-pixel margin for their antialiased
  edges, all 116,718 pixels of `archmage-flat.png` equal `merged.png` exactly (`(200, 210, 230)` background,
  `(40, 90, 220)` square).

## Limitations

- Only 8-bit RGB channels are decoded; 16- and 32-bit, CMYK, Lab, indexed and ZIP-compressed channels are
  reported and skipped.
- Blend modes other than Normal, layer masks, vector masks, clipping, groups, adjustment and text layers,
  effects and smart objects are listed, not rendered.
- One fixture from one writer (GIMP 3.2.6). Photoshop-written files, PSB files and the Krita writer are not
  covered yet; the broad PSD import arrives with the format work (M4).
- The probe is not a codec: it has no fuzz coverage and is not linked into the product.
