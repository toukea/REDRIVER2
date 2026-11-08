# Cockpit interiors — credits

The `.rdc` files in this folder are reduced, recoloured PlayStation-style
conversions (`tools/cockpit/convert_cockpit.py`) of the interiors of the
following models. All are licensed under
[CC-BY-4.0](http://creativecommons.org/licenses/by/4.0/) and the author also
grants unconditional permission to use and modify them.

| File | Based on | Author |
| --- | --- | --- |
| `duke69.rdc` | ["Duke '69 - Low Poly Model"](https://sketchfab.com/3d-models/duke-69-low-poly-model-46fed79f600c4fc69e060991f9e3e4f9) | [Daniel Zhabotinsky](https://sketchfab.com/DanielZhabotinsky) |
| `canyon75.rdc` | ["Canyon '75 Custom - Low poly model"](https://sketchfab.com/3d-models/bd4c9883b9194ba1ac1c7f22300976b9) | [Daniel Zhabotinsky](https://sketchfab.com/DanielZhabotinsky) |
| `bokaroo67.rdc` | ["Bokaroo '67 - Low poly model"](https://sketchfab.com/3d-models/22015d1863d6455aa31cfd738b972c50) | [Daniel Zhabotinsky](https://sketchfab.com/DanielZhabotinsky) |
| `elalamo78.rdc` | ["El Alamo '78 - Low Poly model"](https://sketchfab.com/3d-models/41b3a5c0bc534817b3311c3736c403e6) | [Daniel Zhabotinsky](https://sketchfab.com/DanielZhabotinsky) |

Changes: only the cabin is kept (interior, steering wheel, inside of the body
shell); geometry reduced; textures cropped, downscaled to a 256×256 atlas,
tinted with period trim colours, brightened and reduced to 15-bit colour with
ordered dithering. Each file also stores its credit line.

To rebuild them: download the GLB/glTF files and run
`tools/cockpit/build_cockpits.sh <download folder>`.
