SayonaraUKHD 1.0
Graphics enhancement mod for Sayonara Umihara Kawase (PC, UmiByeWin.exe)
=========================================================================

WHAT IT DOES
  - Higher internal 3D resolution (up to 7168 x 8192).
  - True aspect ratio: the game's aspect-ratio slider widens or heightens the
    view instead of stretching the picture. HUD and menus are unchanged.
  - Culling fix: objects at the newly visible screen edges are drawn instead
    of popping in.
  - FXAA anti-aliasing on the 3D scene. F7 toggles it while playing.
  - Texture dumping and replacement, with PNG or DDS files.

  No game files are modified. Every feature can be turned on or off in
  SayonaraUKHD.ini.


AI DISCLOSURE
  This mod's source code and documentation were written with AI assistance.
  Development was started with GPT-6 Astra, then handed over to Claude Opus
  5.5, which reviewed, optimized, reorganized and extended it. The project's
  goals, direction and all in-game testing are by the author.


INSTALL
  1. Open the game folder (Steam: right-click the game > Manage > Browse local
     files). It is the folder that contains UmiByeWin.exe.
  2. Copy d3d11.dll and SayonaraUKHD.ini into it.
  3. Start the game. The 3D scene should look sharper right away. If you are
     not sure the mod loaded, see TROUBLESHOOTING.

  Linux / Steam Deck
    - Recommended: on SteamOS, play in Game Mode. On a Linux desktop, run the
      game through gamescope, e.g. in Steam > Properties > General > Launch
      Options (use your screen's resolution):
          gamescope -f -W 2560 -H 1440 -- %command%
    - Current Proton and Proton GE load the mod without extra setup. If it
      does not load (e.g. with PROTON_USE_WINED3D=1 or Wine without DXVK),
      add this to the launch options:
          WINEDLLOVERRIDES="d3d11=n,b" %command%
      Combined with gamescope, put it first:
          WINEDLLOVERRIDES="d3d11=n,b" gamescope -f -W 2560 -H 1440 -- %command%

  Upgrading from UmiGraphics (0.15 or older): replace d3d11.dll, use the new
  SayonaraUKHD.ini, and rename the folder "UmiGraphics-textures" to "Textures".

  Uninstall: delete d3d11.dll (and optionally SayonaraUKHD.ini,
  UmiGraphics.log and the Textures folder). On Linux, also remove any launch
  options you added for the mod.


SETTINGS
  Open SayonaraUKHD.ini in a text editor. Each setting is explained there.
  Restart the game after changes.

  The log file UmiGraphics.log is off by default. To create it (useful for
  troubleshooting), set Log=1 in the [Debug] section.


TEXTURE REPLACEMENT
  1. Set Dump=1 in [Textures], play the menus and stages you want, then exit.
     Textures are saved to Textures\dump as
         tex_<id>_<width>x<height>_f<format>.dds
     Set Dump=0 again afterwards (dumping slows loading).
  2. Edit a texture in a DDS-capable editor (e.g. GIMP or Paint.NET). Keep the
     transparency and the full image layout. You may enlarge it by a whole
     number (2x to 8x), the same in both directions.
  3. Save it into Textures\replace with EXACTLY the same file name, as .png or
     .dds. The name always describes the ORIGINAL texture.
  4. Restart the game.

  Rules: up to 8192 pixels per side and 64 MiB of image data. DDS files must
  have no mipmaps (RGBA8, BC1/BC2/BC3 or the original format). If both a .dds
  and a .png exist, the .dds is used. Rejected files are skipped and the
  original texture is used (with Log=1, the reason is in UmiGraphics.log).


TROUBLESHOOTING
  - Checking whether the mod loads: set Log=1 in [Debug] and start the game.
    If no UmiGraphics.log appears, check that d3d11.dll is next to
    UmiByeWin.exe; on Linux, add the WINEDLLOVERRIDES launch option above.
  - Something looks wrong: turn that feature off in SayonaraUKHD.ini to
    confirm. When reporting a problem, set Log=1 and include UmiGraphics.log.
  - Only the Windows, non-VR build of the game is supported.


SOURCE CODE
  Source, documentation and issue tracker: <GitHub repository URL>


LICENSE
  Zero-Clause BSD (0BSD)

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
  SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
  OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

  PNG decoding uses stb_image by Sean Barrett (public domain / MIT).

  This is an unofficial fan project, not affiliated with or endorsed by
  Studio Saizensen or the game's publishers. It contains no game assets.
