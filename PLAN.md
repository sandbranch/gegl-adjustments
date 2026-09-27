# Plan

## Done

`adj:selective-color`, `adj:black-and-white`, `adj:blend-if` and
`adj:luminosity-mask`, and the Create Luminosity Masks plug-in (README).
Tested with tests/check.sh (130 checks, also under ASan and UBSan),
tests/crosscheck.sh (FFmpeg's selectivecolor: its arithmetic read
exactly, the operation within its rounding), tests/gimp-check.sh (GIMP
3.2.6 Flatpak: every precision, grayscale, XCF, the command line, the
plug-in), tests/gui/shots.sh (the dialogs on Broadway, looked at, and a
live filter on a channel through an XCF), tests/bench.sh.

## Next

1. Offer the four to GEGL as one merge request (operations/common,
   LGPL-3+): rename to `gegl:selective-color`, `gegl:black-and-white`,
   `gegl:blend-if` and `gegl:luminosity-mask`, drop the `adj:` namespace
   and `gimp:menu-path` (GIMP lists GEGL's own operations itself), add
   reference hashes for GEGL's tests, and ask whether upstream prefers
   `gimp:` placement for Blend If.
2. A GIMP MR for PSD import (issue #15505, next to draft MR !2598):
   `selc` to Selective Color (the method and the nine CMYK rows map one to
   one; the PSD stores percent), `blwh` to Black & White (the six weights,
   `useTint` and `tintColor` map one to one; `bwPresetKind` needs no
   mapping since the weights are stored), and the layer blending ranges
   (`psd-load.c` skips them with a FIXME) to Blend If for the "This Layer"
   half: psd-tools reads, for the composite gray and each channel, a
   source (This Layer) and a destination (Underlying Layer) range of two
   black values and then two white values, the order of the properties
   here; which byte is the lower half of a split slider is to be checked
   with a real PSD.
3. Selective Color: read Photoshop's `.asv` preset files (FFmpeg's
   `psfile` shows the layout: version, method, ten CMYK rows of int16 of
   which the first is unused).
4. Check the open questions against Photoshop when one is at hand:
   Black & White's formula on unsaturated colors (Ransom's and Kuckir's
   formulas differ there), the default tint (#e1d3b3 is Graphite's), the
   gray of Blend If and its rounding at 8 bits, whether Blend If's
   channels multiply, and the values of Photoshop's own Black & White
   presets (this filter has its own, named as such).
5. Luminosity masks: once GIMP's PDB allows filters on channels, make the
   plug-in attach the operation live instead of writing values (the GUI
   and XCF already support it); zone presets (TK's Zones 1 to 9) as
   named tonal ranges if people ask.
6. Black & White: an "Auto" mix like Photoshop's (not reverse engineered;
   would be this filter's own).

## Found along the way (to report upstream)

- GIMP 3.2.6 `app/pdb/drawable-cmds.c`: `gimp-drawable-append-filter`
  refuses channels ("only drawables of type GimpLayer can have
  non-destructive effects"), although the filter tool applies
  non-destructive filters to channels from the menu and XCF saves and
  loads them (checked here). Letting the PDB do what the GUI does would
  let plug-ins make live masks.
- GIMP 3.2.6: filters made through the PDB (`gimp-drawable-filter-new`)
  do not read an operation's `needs-alpha` key, which the filter tool
  does, so merging such a filter on a layer without alpha loses the
  transparency (tests/gimp-check.py notes it).
- GIMP adds the operation's name to every GEGL filter's title
  ("Selective Color (adj:selective-color)", `app/actions/filters-actions.c`),
  also for operations that have a title; with the `gegl:` prefix only
  that is stripped.
- `gimp-3.2` without `--new-instance`, with a session bus, hands its
  `-b` batch commands to a GIMP that is already running and exits
  (`app/main.c`, `gimp_unique_batch_run`): a GUI test must pass
  `--new-instance`, or its script runs in whatever GIMP is open, the
  user's included. `gimp-console` implies it.
- `flatpak run --sandbox` keeps `~/.var/app/<app>` out of the sandbox
  entirely (no `.ld.so` rewrite, no caches) while `--filesystem` still
  works; for headless GIMP and SDK builds an alternative to setting HOME
  for flatpak run. GTK programs then lack the session bus that glycin's
  image loaders need (GIMP aborts loading its icons), so not for GUI runs
  without `--socket=session-bus`.
