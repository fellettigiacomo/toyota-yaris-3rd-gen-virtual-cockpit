# fonts_src/

- `SpaceGrotesk-Medium.ttf`, `SpaceGrotesk-SemiBold.ttf`, `SpaceGrotesk-Bold.ttf`
  -- the cluster's typeface: static instances of Space Grotesk, under the SIL
  Open Font License 1.1 (`SpaceGrotesk-OFL.txt`). `../scripts/gen_fonts.sh`
  turns them into the bitmap subsets in `../src/ui/fonts/`.
- `DIN_Next_LT_Pro_Regular.otf` -- the previous typeface, no longer used by
  the build. User-provided, under a Monotype/Linotype commercial license,
  **not** OFL/freely redistributable: don't publish this OTF file itself if
  this repo is ever made public.
- `DIN_Next_LT_Pro_Bold_Synthetic.otf` -- the Bold that build used, generated
  from the Regular file above by `../scripts/synth_bold.py` (FontForge outline
  emboldening). Same license caveat.
