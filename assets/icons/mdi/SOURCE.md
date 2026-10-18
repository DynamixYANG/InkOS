# Material Design Icons 7.4.47

Upstream: https://github.com/Templarian/MaterialDesign-SVG/tree/v7.4.47

Apache-2.0 license in LICENSE. Upstream SVG files are unmodified. InkOS modifies derived masks: inset black boundaries by 0.5 SVG units at 4x resolution, threshold to native 1-bit masks, retain isolated detail centerlines, and replace sunny triangular rays with six fine radial strokes. Output: 40/64 px. No runtime resizing.

0.7.10 adds `aurora.svg` (mdiAurora, F1BB9, added in v7.1.96 by Jeff Anders; fetched unmodified from the same v7.4.47 tag) for the Home Kp slot. `tools/generate_mdi_icons.py` applies the same light-mask pipeline (4x fill, 0.5 SVG-unit inset, box downsample, 50 % threshold) at the existing native 40/64 px sizes and writes `firmware/InkOS/src/MdiIconsData.h`.
