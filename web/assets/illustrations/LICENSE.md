# Bird illustrations — attribution and license

The PNG cutout illustrations in this directory come from
[AvianVisitors](https://github.com/Twarner491/AvianVisitors) by Teddy Warner
(built on [BirdNET-Pi](https://github.com/mcguirepr89/BirdNET-Pi)), and are
used here under the repository's
**Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International
(CC BY-NC-SA 4.0)** license: https://creativecommons.org/licenses/by-nc-sa/4.0/

Unlike the rest of this repository (MIT), the images in this directory remain
under CC BY-NC-SA 4.0:

- **Attribution** — keep this notice with the images.
- **NonCommercial** — fine for CIJE's school/classroom use; do not sell.
- **ShareAlike** — if you adapt or redistribute the images, they must stay
  under this same license.

## Changes made

Resized from the originals to a 640 px maximum dimension (matching this
project's thumbnail spec). Filenames are unchanged: kebab-case scientific
name, perched pose (upstream also has `-2` in-flight variants).

## Coverage

13 of the ~22 species in our northern-NJ feeder bundle exist in the upstream
set (it is US-West-focused). Missing eastern species — Northern Cardinal,
Blue Jay, Carolina/Black-capped Chickadee, Tufted Titmouse, Red-bellied
Woodpecker, Carolina Wren, White-throated Sparrow, Common Grackle — can be
generated with upstream's `frame/generate_illustrations.py` (Gemini API)
in the same visual style, or replaced with public-domain art. Until then the
local UI falls back to its built-in SVG silhouettes for those species.
