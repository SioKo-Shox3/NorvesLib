# Asset Load Baseline

This document records the pre-AssetCook runtime asset loading baseline. The values are for comparison only; this is a Debug build and the numbers are not absolute performance targets.

Post-AssetCook scoped texture-path comparison: [AssetLoadPostCookComparison.md](AssetLoadPostCookComparison.md).

## Run Condition

| item | value |
| --- | --- |
| date | 2026-06-06 17:06:14.909 Asia/Tokyo, from `Game.log` |
| branch | `develop` |
| commit | `a3a2f5c342edcfc408442c85772f005fd2228c11` |
| build config | `Debug` |
| command used | `build\Game\Debug\Game.exe --exit-after-frames=30000` |
| timeout | 600 sec wall-clock timeout with process kill on timeout |
| exit code | `0` |
| required stages | present: `gltf_staging_total`, `gltf_finalize_total`, `megamesh_gpu_upload`, `renderworld_model_flush`, texture stages |
| rejected run | `--exit-after-frames=10000` exited `0`, but missed `gltf_finalize_total`, `megamesh_gpu_upload`, and `renderworld_model_flush` |
| OS | Microsoft Windows 11 Home 10.0.22631 build 22631 |
| GPU | NVIDIA GeForce RTX 4080, driver 32.0.15.9186 |
| repo drive | Lexar SSD NM790 4TB, NVMe, drive `C:` |
| asset set | current runtime startup scene: Silver textures, CobbleStoneFloor textures, Boulder glTF model |
| log policy | raw `Game.log`, stdout, stderr, and generated summaries are not committed |

Validation commands:

```powershell
cmake --build build --config Debug --target Game
ctest --test-dir build -C Debug
build\Game\Debug\Game.exe --exit-after-frames=10000
build\Game\Debug\Game.exe --exit-after-frames=30000
powershell -ExecutionPolicy Bypass -File .\Scripts\SummarizeAssetLoadProfile.ps1 -LogPath .\Game.log -RequireCompleteModelFlush
```

The 10000-frame run is a sanity check only. It is not long enough for this baseline on this machine because the model finalize and render-world model flush stages did not complete before exit.

## Stage Aggregate

| stage | count | success | failed | total_ms | avg_ms | max_ms |
| --- | --- | --- | --- | --- | --- | --- |
| gltf_buffer_metadata_parse | 1 | 1 | 0 | 0.022 | 0.022 | 0.022 |
| gltf_buffer_read | 1 | 1 | 0 | 2.945 | 2.945 | 2.945 |
| gltf_buffer_read_total | 1 | 1 | 0 | 3.137 | 3.137 | 3.137 |
| gltf_clusterize | 1 | 1 | 0 | 2176.298 | 2176.298 | 2176.298 |
| gltf_finalize_megamesh | 1 | 1 | 0 | 2.385 | 2.385 | 2.385 |
| gltf_finalize_register | 1 | 1 | 0 | 0.005 | 0.005 | 0.005 |
| gltf_finalize_textures | 1 | 1 | 0 | 42.868 | 42.868 | 42.868 |
| gltf_finalize_total | 1 | 1 | 0 | 45.946 | 45.946 | 45.946 |
| gltf_image_copy | 3 | 3 | 0 | 63.545 | 21.182 | 22.193 |
| gltf_image_decode | 3 | 3 | 0 | 1146.175 | 382.058 | 477.714 |
| gltf_image_read | 3 | 3 | 0 | 44.866 | 14.955 | 19.182 |
| gltf_json_parse | 1 | 1 | 0 | 0.727 | 0.727 | 0.727 |
| gltf_material_texture_parse | 1 | 1 | 0 | 0.142 | 0.142 | 0.142 |
| gltf_mesh_extract | 1 | 1 | 0 | 8.644 | 8.644 | 8.644 |
| gltf_model_flush | 1 | 1 | 0 | 46.187 | 46.187 | 46.187 |
| gltf_primitive_parse | 1 | 1 | 0 | 0.010 | 0.010 | 0.010 |
| gltf_staging_total | 1 | 1 | 0 | 3659.436 | 3659.436 | 3659.436 |
| gltf_text_read | 1 | 1 | 0 | 0.264 | 0.264 | 0.264 |
| gltf_texture_staging | 1 | 1 | 0 | 1465.647 | 1465.647 | 1465.647 |
| megamesh_gpu_upload | 1 | 1 | 0 |  |  |  |
| renderworld_model_flush | 1 | 1 | 0 | 46.243 | 46.243 | 46.243 |
| renderworld_texture_flush | 8 | 8 | 0 | 297.763 | 37.220 | 75.095 |
| texture_async_flush | 8 | 10 | 0 | 296.401 | 37.050 | 74.876 |
| texture_async_worker | 10 | 10 | 0 | 8721.994 | 872.199 | 2247.434 |
| texture_create_upload | 16 | 16 | 0 | 273.339 | 17.084 | 58.035 |

## Texture Source

| path | stages | read_ms | decode_ms | copy_ms | file_bytes | pixel_bytes | width | height | channels |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_ao_4k.png | texture_async_worker | 137.839 | 892.001 | 26.271 | 28633374 | 67108864 | 4096 | 4096 | 1 |
| Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_diff_4k.png | texture_async_worker | 314.109 | 1631.296 | 30.046 | 90202625 | 67108864 | 4096 | 4096 | 3 |
| Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_disp_4k.png | texture_async_worker | 55.242 | 958.050 | 40.609 | 24876605 | 67108864 | 4096 | 4096 | 1 |
| Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_nor_gl_4k.png | texture_async_worker | 244.079 | 1981.574 | 21.781 | 89014215 | 67108864 | 4096 | 4096 | 3 |
| Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_rough_4k.png | texture_async_worker | 134.641 | 705.631 | 31.168 | 28332562 | 67108864 | 4096 | 4096 | 1 |
| Assets/Textures/Silver/silver_albedo.png | texture_async_worker | 13.900 | 363.000 | 27.888 | 231022 | 67108864 | 4096 | 4096 | 3 |
| Assets/Textures/Silver/silver_ao.png | texture_async_worker | 2.449 | 106.037 | 7.750 | 60382 | 16777216 | 2048 | 2048 | 3 |
| Assets/Textures/Silver/silver_metallic.png | texture_async_worker | 2.700 | 356.655 | 22.694 | 231019 | 67108864 | 4096 | 4096 | 3 |
| Assets/Textures/Silver/silver_normal-ogl.png | texture_async_worker | 0.322 | 98.579 | 5.786 | 60383 | 16777216 | 2048 | 2048 | 3 |
| Assets/Textures/Silver/silver_roughness.png | texture_async_worker | 114.097 | 370.463 | 25.337 | 1350351 | 67108864 | 4096 | 4096 | 3 |
| `C:\Users\KINGkawamura\Documents\NorvesLib\Assets\Models\boulder_01_4k.gltf\textures\boulder_01_arm_4k.jpg` | gltf_image_copy,gltf_image_decode,gltf_image_read | 7.478 | 287.889 | 20.279 | 7802675 | 67108864 | 4096 | 4096 | 3 |
| `C:\Users\KINGkawamura\Documents\NorvesLib\Assets\Models\boulder_01_4k.gltf\textures\boulder_01_diff_4k.jpg` | gltf_image_copy,gltf_image_decode,gltf_image_read | 19.182 | 380.572 | 21.073 | 13282817 | 67108864 | 4096 | 4096 | 3 |
| `C:\Users\KINGkawamura\Documents\NorvesLib\Assets\Models\boulder_01_4k.gltf\textures\boulder_01_nor_gl_4k.jpg` | gltf_image_copy,gltf_image_decode,gltf_image_read | 18.206 | 477.714 | 22.193 | 19220463 | 67108864 | 4096 | 4096 | 3 |

## glTF / Model

| request_id | path | json_parse_ms | buffer_read_ms | mesh_extract_ms | clusterize_ms | texture_staging_ms | staging_total_ms | finalize_ms | vertices | indices | clusters | cpu_staging_bytes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | C:/Users/KINGkawamura/Documents/NorvesLib/Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf | 0.727 | 3.137 | 8.644 | 2176.298 | 1465.647 | 3659.436 | 45.946 | 67042 | 198366 | 25562 | 189226400 |

## Main-Render Flush

| stage | count | processed | success | failed | total_ms | max_ms |
| --- | --- | --- | --- | --- | --- | --- |
| gltf_finalize_megamesh | 1 | 0 | 1 | 0 | 2.385 | 2.385 |
| gltf_finalize_register | 1 | 0 | 1 | 0 | 0.005 | 0.005 |
| gltf_finalize_textures | 1 | 0 | 1 | 0 | 42.868 | 42.868 |
| gltf_finalize_total | 1 | 0 | 1 | 0 | 45.946 | 45.946 |
| gltf_model_flush | 1 | 1 | 1 | 0 | 46.187 | 46.187 |
| megamesh_gpu_upload | 1 | 0 | 1 | 0 |  |  |
| renderworld_model_flush | 1 | 1 | 1 | 0 | 46.243 | 46.243 |
| renderworld_texture_flush | 8 | 10 | 8 | 0 | 297.763 | 75.095 |
| texture_async_flush | 8 | 10 | 10 | 0 | 296.401 | 74.876 |
| texture_create_upload | 15 | 0 | 15 | 0 | 272.344 | 58.035 |

## Future Comparison Checklist

- The same command and frame count should be used when comparing cooked asset changes: `build\Game\Debug\Game.exe --exit-after-frames=30000`.
- Run with a wall-clock timeout and treat timeout as a failed measurement.
- Use `Scripts/SummarizeAssetLoadProfile.ps1 -RequireCompleteModelFlush` before accepting a baseline.
- Cooked texture work should remove runtime `texture_async_worker` decode cost for cooked targets.
- Cooked model work should remove runtime `gltf_json_parse`, `gltf_buffer_read`, `gltf_mesh_extract`, and `gltf_clusterize` cost for cooked targets.
- `gltf_finalize_total`, `megamesh_gpu_upload`, and `renderworld_model_flush` must still appear so GPU-side completion is represented.
- Do not commit raw logs, redirected stdout/stderr, or generated summaries.
- Keep in mind that shader compile, IBL generation, GPU driver cache, storage cache, and Debug build overhead can affect the values.
