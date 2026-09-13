# BfrAss

An Assimp importer and command line converter for NintendoWare BFRES model
archives, modelled on Random Talking Bush's 3ds Max importer script.

Supported inputs:

| Platform | FRES versions | Textures |
| --- | --- | --- |
| Wii U (big endian) | 3.x, 4.x | FTEX (GX2 surfaces), `.Tex1`/`.Tex2` companion archives |
| Switch (little endian) | 0.0.0, 3.x, 5.x to 10.x | embedded BNTX, `.Tex` companion archives, standalone BNTX |

Yaz0 compressed files (`.sbfres`, `.szs`) are decompressed transparently.

## Building

BfrAss builds against the Assimp source tree in `../assimp` (override with
`-DBFRASS_ASSIMP_SOURCE_DIR=...`). Assimp and its bundled zlib are linked
statically, so the tool has no other dependencies.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces `build/bfrass` and the static library `bfrass_core`.

## Command line

```sh
bfrass info file.bfres                     # list models, shapes, materials, textures
bfrass convert file.bfres -o out/file.gltf
bfrass convert file.bfres -o file.glb -m Model_fmdl_name
bfrass textures file.bfres -o textures --tex-format dds
bfrass formats                                  # Assimp export format ids
```

`convert` picks the exporter from the output extension (`-f` overrides it).
For `.glb` and `.assbin` textures are embedded as PNG; for every other format
they are written next to the output (or `--tex-dir`) and referenced by path.
Run `bfrass` without arguments for the full option list.

## Mapping of the 3ds Max importer options

| 3ds Max script | CLI | Assimp property |
| --- | --- | --- |
| Model selection list | `-m <name or index>` | `BFRES_MODELS` (`;` separated) |
| Texture format PNG / DDS | `--tex-format png\|dds` | `BFRES_TEXTURE_FORMAT` |
| UV layers: merge / split / none | `--uv-layers merge\|split\|none` | `BFRES_UV_LAYERS` |
| Rigging + Reset XForms: Yes/Yes, Yes/No, No | `--rig reset\|skin\|none` | `BFRES_RIGGING` |
| Import LODs | `--lods` | `BFRES_IMPORT_LODS` |
| Vertex colors | `--no-vertex-colors` | `BFRES_VERTEX_COLORS` |
| Texture path: absolute / relative / subfolder | `--tex-path absolute\|relative\|sub` | `BFRES_TEXTURE_PATH` |
| Print material info | `--mat-info` | `BFRES_PRINT_MATERIAL_INFO` |
| Print debug info | `--debug` | `BFRES_PRINT_DEBUG_INFO` |

Options without a script equivalent:

| CLI | Assimp property | Effect |
| --- | --- | --- |
| `--embed-textures` | `BFRES_EMBED_TEXTURES` | Store textures as `aiTexture` (`*N` references) |
| (default for file outputs) | `BFRES_EXPORT_TEXTURES` | Write texture files |
| `--no-texture-files` | both of the above off | Only reference texture paths |
| `--tex-dir <dir>` | `BFRES_TEXTURE_DIRECTORY` | Where texture files are written |
| | `BFRES_REFERENCE_DIRECTORY` | Base directory for relative texture paths |
| `--all-textures` | `BFRES_EXPORT_UNREFERENCED_TEXTURES` | Also write textures no material uses |
| `--textures <file>` | `BFRES_TEXTURE_SOURCES` | Extra BFRES/BNTX archives searched for textures |
| `--no-auto-textures` | `BFRES_AUTO_TEXTURE_SIBLINGS` | Disable loading `<name>.Tex*.bfres` siblings |
| `--raw-channels` | `BFRES_APPLY_CHANNEL_MAP` | Keep stored channels instead of the texture swizzle |
| `--no-normal-z` | `BFRES_RECONSTRUCT_NORMAL_Z` | Keep two-channel normal maps as stored |
| `--morphs-as-meshes` | `BFRES_MORPHS_AS_MESHES` | Emit key shapes as separate meshes |
| `--pp <step>` | | Run an Assimp post-process step before export |

`BFRES_LOG` accepts a pointer to a `bfrass::Log` to receive the material and
debug output; without it the messages go to Assimp's `DefaultLogger`.

## Using the importer from code

```cpp
#include "importer/BfresImporter.hpp"
#include "importer/ImportConfig.hpp"

Assimp::Importer importer;
bfrass::importer::registerImporter(importer);
importer.SetPropertyString(bfrass::importer::keys::Rigging, "skin");
const aiScene* scene = importer.ReadFile("file.bfres", 0);
```

## Scene layout

* Root node `FRES_<file>`, one child `FMDL_<model>` per model, then the bone
  hierarchy. Bone names get a `_<model index>` suffix when several models are
  imported so skeletons stay distinct.
* Meshes are named after the shape; LOD meshes append ` (LOD: n)`, split UV
  layers append ` Layer N`.
* Vertex attributes follow the script: `_p0` positions, `_n0` normals, `_t0`
  tangents, `_b0` bitangents, `_c0`..`_c7` colors, `_u0`..`_u4` and
  `_g3d_02_*` UVs (V flipped), `_i0`/`_i1` + `_w0`/`_w1` skinning.
* Key shapes (`_p1`.., `_n1`..) become `aiAnimMesh` morph targets unless
  `--morphs-as-meshes` is given.
* Materials map textures by sampler name the same way the script does:
  `_a0` diffuse/base color (plus opacity when the shader uses alpha), `_sd0`
  multiplied diffuse layer, `_ao0` ambient occlusion, `_e0`/`_em0` emissive,
  `_n0` normals, `_s0` specular, `_r0` shininess, `_rn0` roughness, `_x0`
  reflection. Unmapped samplers are kept as `aiTextureType_UNKNOWN`. Shader
  params, render info, shader assignments, sampler settings and user data are
  stored as `$bfres.*` material properties. `display_face = both` marks the
  material two-sided.

## Differences from the 3ds Max script

* Bone world matrices are computed with the NintendoSDK g3d rules (Euler or
  quaternion rotation, Maya/Softimage segment scale compensation) instead of
  Max's XForm reset, and are checked against the file's inverse model
  matrices.
* Textures are decoded natively (BC1 to BC7, BC6H, ASTC LDR, ETC1/ETC2/EAC and
  uncompressed formats) with Tegra block-linear and GX2 deswizzling, so no
  external converter is needed. DDS output keeps the original compressed
  blocks and all mips and array layers when DXGI can represent the format,
  and falls back to RGBA8 otherwise.
* The texture channel mapping stored in BNTX/FTEX is applied by default.
  Two-channel normal maps get their Z component reconstructed.

## Limitations

* HDR ASTC endpoint modes decode to magenta, since no Switch texture format
  uses them.
* Wii U companion archives (`.Tex1` images, `.Tex2` mips) are merged by texture
  name.
