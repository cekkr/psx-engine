# Carrot Man demo

Run `./build.sh debug` from the repository root and open
`build/debug/samples/carrot/psxe_carrot.cue` in a PlayStation emulator.
Use the port 1 D-pad or left analog stick to move and turn in eight directions.
Carrot Man walks while moving and idles when you release the controller.
Enable analog mode on the emulated pad to use its left stick.

## Rebuild the character assets

The sample builds from the cooked files in [`assets/`](assets/). To recook them,
run this command with Python 3 and FFmpeg on your host.

```sh
python3 -B samples/carrot/bake.py ~/Downloads/carrot_man.glb
```

The script uses the [GLB cooker](../../tools/cook_glb.py) to apply the scene's
transforms, weld position duplicates while keeping per-corner UVs, and write
mesh topology, packed poses, and an 8-bpp TIM. It generates four idle poses
and twelve walk poses. The walk cycle is authored in the script for this mesh.
The two loops use the same faces, UVs, and texture.

For another static model, export one mesh primitive with a base color image
embedded in the GLB, triangles, one material, and UVs in the 0–1 range.
The cooker uses the default scene's transforms and normalizes model height.
It writes unlit faces into `mesh.inc`, frame-major little-endian `SVECTOR`s
into `poses.bin`, and a relocatable TIM into `color.tim`.

```sh
python3 tools/cook_glb.py model.glb build/model --height 180 --bpp 16
```

Use `--bpp 8` for textures with up to 255 opaque colors after RGB555
conversion. `--black-key` makes exact black transparent, and source alpha
below 128 becomes transparent.

## Asset credits

[Carrot Man](https://sketchfab.com/3d-models/carrot-man-d51f2c4a84554867aab1d9191ed9077e)
by [keatura](https://sketchfab.com/keatura) is licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). For this demo the
model was reoriented, quantized, and animated. The texture was converted to
PlayStation TIM with black keyed to transparency. The cutout cards were
adjusted for the PlayStation renderer.
