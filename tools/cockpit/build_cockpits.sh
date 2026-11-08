#!/bin/sh
# Rebuild the bundled cockpits from the Daniel Zhabotinsky CC-BY-4.0 models
# (download the GLB/glTF files from Sketchfab first, see data/cockpits/CREDITS.md).
# Usage: tools/cockpit/build_cockpits.sh <folder with the downloads>
set -e
D="${1:-$HOME/Downloads}"; T="$(dirname "$0")"; O="$(dirname "$0")/../../data/cockpits"
C() { python3 "$T/convert_cockpit.py" "$1" "$O/$2.rdc" --trim "$3" --interior Interior,doorcard --shell "$4" --credit "$5 by Daniel Zhabotinsky, CC-BY-4.0, https://sketchfab.com/3d-models/$6"; }
C "$D/duke_69_-_low_poly_model/scene.gltf" duke69 tan Body,Bottom "Duke '69 - Low Poly Model" 46fed79f600c4fc69e060991f9e3e4f9
C "$D/canyon_75_custom_-_low_poly_model.glb" canyon75 blue Body,Bottom "Canyon '75 Custom - Low poly model" bd4c9883b9194ba1ac1c7f22300976b9
C "$D/bokaroo_67_-_low_poly_model.glb" bokaroo67 tan Bottom "Bokaroo '67 - Low poly model" 22015d1863d6455aa31cfd738b972c50
C "$D/el_alamo_78_-_low_poly_model.glb" elalamo78 red Body,Bottom "El Alamo '78 - Low Poly model" 41b3a5c0bc534817b3311c3736c403e6
