# S1-6.4 color experiment: sRGB to a wide-gamut profile, assign versus convert

M0-E2 profile conversion (spec §8.2, §8.5; ADR-0010). Produced on 2026-10-13 by
`build/dev-debug/tests/ColorExperimentTest` (test `tests/core/color/ColorExperimentTest.cpp`, `math` tier),
with Little CMS 2.19.1 (package `lcms2 2.19.1-1.1`) through `color::iccTransform` (float transforms, alpha passed through).

## Profiles

- **Source:** ArchMage's built-in sRGB profile (`ColorProfile::srgb()`, Little CMS `cmsCreate_sRGBProfile`).
- **Destination:** a matrix/TRC profile built with `ColorProfile::fromPrimaries` from the primaries published
  for the 1998 wide-gamut RGB space: red (0.6400, 0.3300), green (0.2100, 0.7100), blue (0.1500, 0.0600),
  white D65 (0.3127, 0.3290), pure gamma 563/256 = 2.19921875. It is named "ArchMage wide-gamut test RGB",
  not after the trademarked space.

## Conversion: sRGB to wide gamut, relative colorimetric with black point compensation

The independent column is computed in double precision from the xy primaries (RGB to XYZ matrices; both spaces
share D65, so the D50 adaptation of the ICC PCS cancels out).

| Patch | sRGB | wide, relative + BPC (Little CMS) | wide, independent |
|---|---|---|---|
| gray 0.0 | 0.000 0.000 0.000 | 0.000000 0.000000 0.000000 | 0.000000 0.000000 0.000000 |
| gray 0.1 | 0.100 0.100 0.100 | 0.123327 0.123327 0.123327 | 0.123321 0.123321 0.123321 |
| gray 0.2 | 0.200 0.200 0.200 | 0.212323 0.212323 0.212323 | 0.212317 0.212317 0.212317 |
| gray 0.3 | 0.300 0.300 0.300 | 0.304648 0.304648 0.304648 | 0.304642 0.304642 0.304642 |
| gray 0.4 | 0.400 0.400 0.400 | 0.399410 0.399410 0.399410 | 0.399405 0.399405 0.399405 |
| gray 0.5 | 0.500 0.500 0.500 | 0.496108 0.496108 0.496108 | 0.496104 0.496104 0.496104 |
| gray 0.6 | 0.600 0.600 0.600 | 0.594418 0.594418 0.594418 | 0.594415 0.594415 0.594415 |
| gray 0.7 | 0.700 0.700 0.700 | 0.694113 0.694113 0.694113 | 0.694110 0.694110 0.694110 |
| gray 0.8 | 0.800 0.800 0.800 | 0.795025 0.795025 0.795025 | 0.795023 0.795023 0.795023 |
| gray 0.9 | 0.900 0.900 0.900 | 0.897022 0.897022 0.897022 | 0.897021 0.897021 0.897021 |
| gray 1.0 | 1.000 1.000 1.000 | 1.000000 1.000000 1.000000 | 1.000000 1.000000 1.000000 |
| red | 1.000 0.000 0.000 | 0.858585 0.006385 0.000000 | 0.858592 0.000000 0.000000 |
| green | 0.000 1.000 0.000 | 0.564970 1.000002 0.234423 | 0.564972 1.000000 0.234424 |
| blue | 0.000 0.000 1.000 | 0.006394 0.000000 0.981074 | 0.000000 0.000000 0.981069 |
| cyan | 0.000 1.000 1.000 | 0.564984 0.999993 1.000005 | 0.564972 1.000000 1.000000 |
| magenta | 1.000 0.000 1.000 | 0.858593 0.000000 0.981069 | 0.858592 0.000000 0.981069 |
| yellow | 1.000 1.000 0.000 | 0.999993 1.000009 0.234394 | 1.000000 1.000000 0.234424 |
| orange | 1.000 0.500 0.000 | 0.891132 0.496125 0.116232 | 0.891138 0.496104 0.116299 |

## Measurements

```
max |Little CMS - independent|: 0.00639 encoded, 2e-05 linear; |perceptual - relative| = 0; |BPC - no BPC| = 0
sRGB -> wide -> sRGB: max error 0.000258
wide green (0, 1, 0) in sRGB: -5.147250 1.000000 -0.554740
```

- **Agreement:** Little CMS matches the independent computation to 2e-5 in linear light. Channels that should be
  exactly 0 (green of red, red of blue) come out near 1e-5 linear because ICC stores colorant matrices as
  s15Fixed16 numbers (resolution 1.5e-5); the 1/2.2 power lifts that to about 0.006 encoded (1.6 levels in
  8 bits). The test bounds the linear difference at 1e-4.
- **Intents and black point compensation:** perceptual and relative colorimetric give identical results, and
  so do runs with and without black point compensation. Matrix/TRC profiles carry no perceptual tables, and
  both profiles have a zero black point. Distinct intents need LUT-based profiles (outside Stage 1's RGB
  scope).
- **Neutrals stay neutral** (channels within 2e-4), and white maps to white exactly.
- **Round trip** sRGB to wide to sRGB: largest error 2.6e-4 encoded.
- **Out of gamut:** the wide green primary in sRGB is (−5.147250, 1.0, −0.554740). Float transforms are unbounded,
  so float documents keep out-of-range values (D-09). Below zero Little CMS extends the sRGB curve's linear
  segment (12.92 × linear), not a mirrored power curve: a future out-of-gamut warning must be computed in
  linear light.

## Assign versus convert

Appearance is the PCS XYZ (D50) of the samples read through a profile. Assigning the wide profile keeps the
samples and changes their appearance; converting changes the samples and keeps the appearance.

| Patch | look as sRGB (XYZ) | assigned wide: look (XYZ) | converted samples | converted look (XYZ) |
|---|---|---|---|---|
| gray 0.0 | 0.0000 0.0000 0.0000 | 0.0000 0.0000 0.0000 | 0.0000 0.0000 0.0000 | 0.0000 0.0000 0.0000 |
| gray 0.1 | 0.0097 0.0100 0.0083 | 0.0061 0.0063 0.0052 | 0.1233 0.1233 0.1233 | 0.0097 0.0100 0.0083 |
| gray 0.2 | 0.0319 0.0331 0.0273 | 0.0280 0.0290 0.0239 | 0.2123 0.2123 0.2123 | 0.0319 0.0331 0.0273 |
| gray 0.3 | 0.0706 0.0732 0.0604 | 0.0683 0.0708 0.0584 | 0.3046 0.3046 0.3046 | 0.0706 0.0732 0.0604 |
| gray 0.4 | 0.1281 0.1329 0.1096 | 0.1285 0.1333 0.1100 | 0.3994 0.3994 0.3994 | 0.1281 0.1329 0.1096 |
| gray 0.5 | 0.2064 0.2140 0.1766 | 0.2100 0.2178 0.1796 | 0.4961 0.4961 0.4961 | 0.2064 0.2140 0.1766 |
| gray 0.6 | 0.3071 0.3186 0.2628 | 0.3135 0.3252 0.2682 | 0.5944 0.5944 0.5944 | 0.3071 0.3186 0.2628 |
| gray 0.7 | 0.4320 0.4480 0.3695 | 0.4401 0.4564 0.3765 | 0.6941 0.6941 0.6941 | 0.4320 0.4480 0.3695 |
| gray 0.8 | 0.5822 0.6038 0.4981 | 0.5903 0.6122 0.5050 | 0.7950 0.7950 0.7950 | 0.5822 0.6038 0.4981 |
| gray 0.9 | 0.7592 0.7874 0.6495 | 0.7648 0.7932 0.6543 | 0.8970 0.8970 0.8970 | 0.7592 0.7874 0.6495 |
| gray 1.0 | 0.9642 1.0000 0.8249 | 0.9642 1.0000 0.8249 | 1.0000 1.0000 1.0000 | 0.9642 1.0000 0.8249 |
| red | 0.4360 0.2225 0.0139 | 0.6097 0.3111 0.0195 | 0.8586 0.0064 0.0000 | 0.4360 0.2225 0.0139 |
| green | 0.3851 0.7169 0.0971 | 0.2053 0.6257 0.0609 | 0.5650 1.0000 0.2344 | 0.3851 0.7169 0.0971 |
| blue | 0.1431 0.0606 0.7139 | 0.1492 0.0632 0.7446 | 0.0064 0.0000 0.9811 | 0.1431 0.0606 0.7139 |
| cyan | 0.5282 0.7775 0.8110 | 0.3545 0.6889 0.8054 | 0.5650 1.0000 1.0000 | 0.5282 0.7775 0.8110 |
| magenta | 0.5791 0.2831 0.7278 | 0.7589 0.3743 0.7640 | 0.8586 0.0000 0.9811 | 0.5791 0.2831 0.7278 |
| yellow | 0.8212 0.9394 0.1110 | 0.8150 0.9368 0.0803 | 1.0000 1.0000 0.2344 | 0.8212 0.9394 0.1110 |
| orange | 0.5185 0.3759 0.0347 | 0.6544 0.4474 0.0327 | 0.8911 0.4961 0.1162 | 0.5185 0.3759 0.0347 |

- **Convert:** the appearance after conversion matches the original within 2e-4 for every patch.
- **Assign:** saturated colors change appearance by more than 0.01 in XYZ (red's X goes from 0.4360 to 0.6097);
  white and black are unchanged because both spaces share D65 and a zero black. Mid grays change too, through
  the different transfer curves (gray 0.5: Y from 0.2140 to 0.2178).
