These files are isolated performance fixtures, not application assets. The
Android scene-probe artifact includes them with their original license files.

Source: KhronosGroup/glTF-Sample-Models, `2.0/<model>/glTF-Binary/<model>.glb`.
The SHA256 digest fixes each test input even if the upstream branch changes.

| Model | Purpose | SHA256 |
| --- | --- | --- |
| DamagedHelmet | Multiple PBR textures and static geometry | a1e3b04de97b11de564ce6e53b95f02954a297f0008183ac63a4f5974f6b32d8 |
| Fox | Animated hierarchy and skinning | d97044e701822bac5a62696459b27d7b375aada5de8574ed4362edbba94771f7 |
| AnimatedMorphCube | Morph animation, including repeated shared copies | 214ee56160a50dbf22543a1d66dbf860986e87f0efac3d89feac1359d0e6aeab |

The adjacent `LICENSE_<model>.md` files accompany the original model bytes.
They include the authors' applicable licenses and metadata licensing.

Probe examples (same arguments for baseline and candidate binaries):

```
probe --model /data/local/tmp/DamagedHelmet.glb 0 0 0 1280 720 60 0 0 /data/local/tmp/helmet.half 32 1 shared -1
probe --model /data/local/tmp/Fox.glb 0 0 0 1280 720 60 0 0 /data/local/tmp/fox.half 32 1 shared 0
probe --model /data/local/tmp/AnimatedMorphCube.glb 0 0 0 1280 720 60 0 0 /data/local/tmp/morph100.half 32 100 shared 0
```

Each result reports import, first frame and warmed frames separately. Explicit
GPU waits measure the offscreen renderer; these numbers are not UI playback FPS.
