# Developer notes

## How it works

`d3d11.dll` is a proxy. When the game calls `D3D11CreateDevice` or `D3D11CreateDeviceAndSwapChain`, the mod loads the real system `d3d11.dll` (DXVK under Proton), forwards the call, and then hooks selected device, context and swap-chain methods by patching their shared COM vtables. In any executable other than `UmiByeWin.exe` it only forwards calls and changes nothing.

| Feature | How |
|---|---|
| Resolution | The game renders its 3D scene into a 1024×512 buffer with an 896×512 viewport. Exactly those buffers, viewports and scissor rectangles are enlarged. |
| Aspect ratio | Camera matrices are captured as the CPU uploads them and validated. Before each 3D draw, a copy corrected for the displayed aspect ratio is bound in place of the game's buffer, then the original is restored. |
| Culling | The game's shared sphere-visibility routine is patched (only for the exact known build, checked by PE timestamp, image size and a hash of the routine). Objects rejected by the original test are re-tested against widened side planes. |
| FXAA | The scene target is copied and an FXAA pass is drawn back over it just before the HUD starts. All touched pipeline state is restored. |
| Textures | CPU-uploaded textures are hashed and optionally dumped. Matching replacement files become mod-owned textures, which are swapped into pixel-shader slots for each draw and swapped back afterwards. |

## Source layout

```
build.sh                build/d3d11.dll
run_tests.sh            host unit tests (+ DLL import/export check if built)
package.sh              dist/SayonaraUKHD-<version>.zip for players
release/                files shipped with the DLL (INI, readme.txt)
.clang-format           code style: clang-format -i --style=file

src/main.c              entry points, config loading, hook installation.
                        The only .c file; it includes the modules below.
src/exports.def         maps the two D3D11 exports to the mod's functions
src/core/
  common.h              includes, vtable slot asserts/names, shared state
  log.h                 UmiGraphics.log writer (unique-line filter, VLOG)
  hooks.h               vtable patching and original-function lookup
src/policy/             pure functions with no D3D calls (unit tested)
  resolution.h          internal size and scissor-edge math
  scale_policy.h        which buffers are the gameplay buffers
  aspect_policy.h       camera / rope matrix validation and correction
  culling_policy.h      widened frustum side planes
  texture_format.h      supported formats, DDS read/write, content hash
  png_decode.h          stb_image configuration for PNG replacements
  camera_shaders.h      hashes of the game's 3D camera vertex shaders
src/features/
  resolution_hooks.h    CreateTexture2D / viewport / scissor scaling
  shader_ids.h          vertex shader hashing and identification
  aspect_hooks.h        camera upload tracking, per-draw correction
  culling_hook.h        patch of the game's visibility routine
  fxaa_pass.h           FXAA pass and F7 toggle
  texture_system.h      texture dump / replacement
  frame_hooks.h         Draw, DrawIndexed, OMSetRenderTargets, Present, Resize
src/debug/              only active with [Debug] Diagnostics=1
  debug_files.h         SayonaraUKHD-debug output folder
  frame_capture.h       shader/layout tagging, F8 frame capture
src/third_party/
  stb_image.h           stb_image v2.30 (public domain / MIT), PNG only

shaders/
  fxaa.hlsl             FXAA source (original implementation, 0BSD)
  fxaa_bytecode.h       compiled vs_4_0 / ps_4_0 bytecode embedded in the DLL
  generate_shaders.py   regenerate bytecode with vkd3d-compiler (Linux)
  compile_shaders.c     regenerate bytecode with D3DCompile (Windows/Wine)

tests/                  C unit tests for src/policy, PNG decode test with
                        fixtures/, FXAA shader smoke test
tools/
  verify_dll.py         checks x86 PE32, exact exports, only system imports
  texture_tool.py       optional: DDS dumps -> PNG previews, PNG -> DDS
```

## Building

You need a MinGW-w64 i686 (32-bit) cross compiler, either GCC or Clang:

- Fedora / Bazzite toolbox: `sudo dnf install mingw32-gcc`
- Debian / Ubuntu: `sudo apt install gcc-mingw-w64-i686`
- or [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (msvcrt variant): `CC=/path/to/i686-w64-mingw32-clang ./build.sh`

```sh
./build.sh       # build/d3d11.dll
./run_tests.sh   # native cc with AddressSanitizer / UndefinedBehaviorSanitizer
./package.sh     # dist/SayonaraUKHD-<version>.zip
```

The 1.0 release DLL was built with llvm-mingw 20250613 (Clang).

## Constraints worth knowing

- The texture hash (`umi_tex_hash`) and dump file names must not change, or existing dumps and replacement packs stop matching.
- The camera shader hashes (`camera_shaders.h`, `ROPE_SHADER`) and the culling patch address/signature are specific to the current game build.
- `cullingEntry` and `invokeCulling` are hand-written assembly for the game's internal calling convention, and `cullingDispatch` must stay `cdecl`.
- Mod-owned resources are created and updated through `original()` functions, so the mod's own hooks never track, dump or replace them.

## Testing status (1.0)

Automated, passing:
- Unit tests: resolution, buffer selection, aspect/rope correction, culling planes, DDS formats/hash/scaling/fuzzing, and PNG decoding of colour/grey/palette/16-bit files with rejection of truncated, oversized and junk files.
- Build with `-Wall -Wextra -Werror`. The Clang static analyzer is clean apart from two harmless dead-store notes inside stb_image.
- `verify_dll.py`: x86 PE32 DLL that exports exactly `D3D11CreateDevice` and `D3D11CreateDeviceAndSwapChain` and imports only KERNEL32, USER32 and msvcrt.

Confirmed in game (Linux/Proton): resolution, aspect ratio across the full slider range, rope, culling, FXAA, texture dumping, and DDS and PNG replacements.
