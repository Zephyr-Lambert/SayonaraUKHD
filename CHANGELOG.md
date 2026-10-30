# Changelog

The mod was called **UmiGraphics** before 0.16.

## 1.0
- First public release. Same features as 0.16, confirmed in game.
- New `[Debug] Log` setting. `UmiGraphics.log` is now off by default; set `Log=1` to create it (useful for troubleshooting).
- Default internal resolution in the included INI is now 3840×2160.
- Release package now contains only `d3d11.dll`, `SayonaraUKHD.ini` and `readme.txt`. The optional `texture_tool.py` helper lives in the repository's `tools` folder.

## 0.16
- Renamed to SayonaraUKHD. Settings file is now `SayonaraUKHD.ini` (an old `UmiGraphics.ini` is still read if it is the only one present). The log keeps the name `UmiGraphics.log`.
- Texture folder renamed to `Textures` (`dump` and `replace` inside).
- Texture replacements can be PNG files. If no `.dds` exists for a texture, a `.png` with the same name is loaded instead (uses stb_image).
- Verbose log: rope corrections are logged once per aspect ratio instead of once per frame of rope movement.

## 0.15
Cleanup and optimization release.
- Per-draw log formatting is off by default (available with `[Debug] VerboseLog=1`).
- Aspect correction reuses two constant buffers instead of creating a new GPU buffer whenever the camera moves.
- Lock-free hook lookups; texture file I/O no longer blocks rendering; the replace folder is scanned once at startup.
- FXAA settings uploaded once instead of every frame.
- Developer capture tools only active with `[Debug] Diagnostics=1`.
- Fixed a duplicated private-data GUID and a log filter that stopped accepting new lines after 4096 entries.
- Source split into modules, reformatted and documented; build and test scripts added.

## 0.14
- Texture dumping and replacement (DDS).

## 0.13
- FXAA, internal resolution, true aspect ratio, rope correction and culling fix.
