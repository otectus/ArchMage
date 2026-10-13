# S1-6.6 RAW decode probe (M0-E5, RAW part)

Spec §18.1, §26.1; plan S1-6.6. Produced on 2026-10-13 with LibRaw 0.22.2 (`libraw_r`).

## Fixture

`canon-powershot-a410-chdk-10bit.dng` in the corpus directory: raw.pixls.us sample 1327, a Canon PowerShot
A410 DNG written by CHDK 1.5.0 (10-bit Bayer, 4:3), 4,219,200 bytes, SHA-256
`f0d2fe47507fadf50008bddbc6bd2c5e39fddbe90cca6bca72dd360fa0e0eb38` (equal to the site's published checksum).
License CC0 1.0, as the raw.pixls.us repository table states for this sample; recorded in
`docs/dependencies/manifest.yaml` (`fixtures`) and the corpus register.

## Commands

```
cmake --build --preset dev-debug --target raw_probe
build/dev-debug/benchmarks/raw_probe ~/.local/share/archmage-dev/corpus/canon-powershot-a410-chdk-10bit.dng <output>
build/dev-debug/benchmarks/raw_probe ~/.local/share/archmage-dev/corpus/layered-gimp-3.2.6.psd <output>   # not RAW
```

Output: [`raw/raw-probe.txt`](raw/raw-probe.txt), [`raw/raw-probe-preview.png`](raw/raw-probe-preview.png)
(696 × 517, a 3 × 3 box-filtered copy of the 2088 × 1552 export, which is 5.5 MB and not kept), and
[`raw/raw-probe-unsupported.txt`](raw/raw-probe-unsupported.txt).

## Result

- **Camera:** Canon PowerShot A410; ISO 50, 1/424 s, f/5.5, 9.8 mm.
- **Geometry and orientation:** raw 2144 × 1560; visible area 2088 × 1552 at (2, 6). LibRaw flip 0, so no
  rotation applies.
- **Sensor:** RGBG Bayer filter (pattern 0xb4b4b4b4), black level 31, white level 1023.
- **Processing:** LibRaw's default demosaic, with camera white balance, sRGB primaries and transfer, 16 bits
  and no automatic brightening. The result goes into a float32 ArchMage document: one layer of 221 tiles,
  samples in [0, 1] with mean 0.1762. It is exported as 8-bit sRGB through the CPU compositor.
- **Unsupported input:** given the PSD fixture, the probe reports `unsupported_input/raw.unsupported` ("Unsupported
  file format or not RAW file") and exits 1.

## Limitations

- One 10-bit compact-camera DNG. Dark saturated areas (the violet petals) show contours in the 8-bit export.
  The source has about 1,000 levels between black and white, and white balance plus the sRGB curve stretch
  the dark end. No vendor RAW format (CR2, NEF, ARW, RAF) and no high-bit-depth or non-Bayer sensor was tried.
- The demosaic, white balance and color matrix are LibRaw's. ArchMage's RAW development pipeline, with its
  nondestructive parameters, is M4 work (spec §18.1).
- LibRaw runs in-process with a 2 GiB allocation cap. Isolated decoder workers come with the broad format set
  (D-18, M4).
