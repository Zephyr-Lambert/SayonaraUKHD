# SayonaraUKHD

**SayonaraUKHD** is a graphics enhancement mod for the PC version of *Sayonara Umihara Kawase*. It raises the internal rendering resolution, turns the game's aspect-ratio slider into a real widescreen/tallscreen option, adds FXAA, and lets you dump and replace the game's textures.

No game files are modified. Remove one DLL and the game is back to normal.

> [!NOTE]
> ## AI disclosure
> This mod's source code and documentation were written with AI assistance. Development was started with GPT-6 Astra, then handed over to Claude Opus 5.5, which reviewed, optimized, reorganized and extended it. The project's goals, direction and all in-game testing are by me.

<!-- Screenshots: before/after comparison, wide aspect ratio, replaced texture -->

## Features

- **Higher internal resolution**: render the 3D scene at your screen resolution or higher (up to 7168 × 8192) instead of the game's fixed internal size.
- **True aspect ratio**: the game's built-in aspect-ratio slider normally just stretches a 16:9 image. With the mod it widens or heightens the actual view instead. The HUD and menus keep their original layout.
- **Culling fix**: objects that come into view at the newly visible screen edges are drawn instead of popping in.
- **FXAA**: anti-aliasing on the 3D scene only (the HUD stays sharp). Press **F7** in game to compare it on and off.
- **Texture dumping and replacement**: save the game's textures, edit them at up to 8× their original size, and load your versions as PNG or DDS.

Every feature can be turned on or off individually in `SayonaraUKHD.ini`.

## Requirements

- The Windows (non-VR) PC version of the game (`UmiByeWin.exe`).
- Windows, or Linux / Steam Deck through Proton.

## Installation

1. Download the latest release from the [Releases](../../releases) page.
2. Open the game folder: in Steam, right-click the game → **Manage** → **Browse local files**. It is the folder that contains `UmiByeWin.exe`.
3. Copy `d3d11.dll` and `SayonaraUKHD.ini` into that folder.
4. Start the game. The 3D scene should look sharper right away; if you are not sure the mod loaded, see [Troubleshooting](#troubleshooting).

### Linux / Steam Deck notes

- **Recommended:** on SteamOS, play in **Game Mode**. On a Linux desktop, run the game through **gamescope**, for example with this in the game's **Properties** → **General** → **Launch Options** (use your screen's resolution):
  ```
  gamescope -f -W 2560 -H 1440 -- %command%
  ```
- With current Proton and Proton GE the mod loads without any extra setup. If it does not load (for example when using `PROTON_USE_WINED3D=1`, or Wine without DXVK), add this to the launch options:
  ```
  WINEDLLOVERRIDES="d3d11=n,b" %command%
  ```
  When combining both, put the override first: `WINEDLLOVERRIDES="d3d11=n,b" gamescope -f -W 2560 -H 1440 -- %command%`

To uninstall, delete `d3d11.dll` (and optionally `SayonaraUKHD.ini`, `UmiGraphics.log` and the `Textures` folder). On Linux, also remove any launch options you added for the mod.

## Configuration

All settings are in `SayonaraUKHD.ini`, with an explanation next to each one. Restart the game after making changes.

| Section | Setting | What it does |
|---|---|---|
| `[Rendering]` | `Enabled`, `Width`, `Height` | Internal 3D resolution. Usually your screen resolution, or higher for supersampling. |
| `[AspectRatio]` | `Enabled` | Real aspect-ratio change, controlled by the game's own slider. |
| | `FixCulling` | Draws objects at the newly visible edges. |
| `[Antialiasing]` | `Enabled` | FXAA on the 3D scene. **F7** toggles it while playing. |
| `[Textures]` | `Dump` | Saves the game's textures to `Textures\dump` as you play. |
| | `Replacement` | Loads your edited textures from `Textures\replace`. |
| | `MaxDumpFiles` | Limit on new dump files per launch. |
| `[Debug]` | `Log` | Writes `UmiGraphics.log` (off by default). Turn on when troubleshooting. |
| | `VerboseLog`, `Diagnostics` | Troubleshooting and developer tools. Leave at `0`. |

## Texture replacement

1. **Collect.** Set `Dump=1`, play through the menus and stages you want, then exit. Textures are saved as
   `Textures\dump\tex_<id>_<width>x<height>_f<format>.dds`.
   Set `Dump=0` again afterwards, since dumping slows down loading.
2. **Edit.** Open the DDS file in an editor that supports DDS (for example GIMP or Paint.NET). Keep the transparency and the full layout of the image, because many textures are sheets of several sprites. You can enlarge it, but only by a whole number (2× to 8×) and by the same amount in both directions, e.g. 256×128 → 1024×512.
3. **Install.** Save your edit into `Textures\replace` with **exactly the same file name** as the original, as either `.png` or `.dds`. The name always describes the *original* texture, even if yours is larger.
4. **Restart** the game. Replacements are only read at startup.

Notes:
- PNG files can be colour, greyscale or palette images, with or without transparency.
- DDS files must be a single image (no mipmaps). Uncompressed RGBA8, BC1/BC2/BC3, or the original's format are accepted. DDS loads faster than PNG, and BC formats use less video memory, which matters for large texture packs.
- If both a `.dds` and a `.png` exist for the same texture, the `.dds` is used.
- Limits: 8192 pixels per side and 64 MiB of image data per texture.
- Files that break these rules are skipped (the original texture is used). With `Log=1`, the reason is written to `UmiGraphics.log`.
- Optional helper: [`tools/texture_tool.py`](tools/texture_tool.py) converts a whole dump folder to PNG previews (Python 3 + Pillow).

Textures the game uploads once from its data files are covered, which includes menu and stage art. Textures the game draws on the GPU or updates piece by piece always use the original.

## Troubleshooting

- **Checking whether the mod loads:** set `Log=1` in the `[Debug]` section and start the game. If no `UmiGraphics.log` appears next to the game, check that `d3d11.dll` is next to `UmiByeWin.exe`. On Linux, add the `WINEDLLOVERRIDES` launch option from the Linux notes.
- **Something looks wrong:** turn the related feature off in the INI to confirm the cause. When reporting a problem, please turn on `Log=1` and include `UmiGraphics.log`.
- **A texture is not replaced:** check that the file name matches the dump exactly, then turn on `Log=1` and look for `TEXTURE replacement rejected` in the log.

## Known limitations

- Only the Windows, non-VR build of the game is supported. The culling fix checks for the exact game version and turns itself off on any other build.
- Textures with mipmaps, dynamic or partially updated textures, and instanced/indirect draws are not covered by texture replacement.

## Building from source

You need a 32-bit MinGW-w64 cross compiler (GCC or Clang):

```sh
# Fedora / Bazzite toolbox: sudo dnf install mingw32-gcc
# Debian / Ubuntu:          sudo apt install gcc-mingw-w64-i686
./build.sh          # -> build/d3d11.dll
./run_tests.sh      # unit tests (native C compiler)
./package.sh        # -> dist/SayonaraUKHD-<version>.zip
```

To use llvm-mingw instead: `CC=/path/to/i686-w64-mingw32-clang ./build.sh`.

See [docs/DEVELOPING.md](docs/DEVELOPING.md) for how the mod works and how the source is organized.

## License

The mod is released under the [Zero-Clause BSD license (0BSD)](LICENSE): use, modify and share it however you like, with or without credit.

PNG decoding uses [stb_image](https://github.com/nothings/stb) by Sean Barrett (public domain / MIT).

This is an unofficial fan project, not affiliated with or endorsed by Studio Saizensen or the game's publishers. It contains no game assets and grants no rights to the game or its assets.
