# AutoSEQ

An SKSE plugin for Skyrim Special Edition / Anniversary Edition that finds and repairs outdated or missing **SEQ files** every time the game starts.

## The problem

Every plugin with *Start Game Enabled* quests needs a matching `Data\Seq\<plugin>.seq` file. It lists those quests by FormID so the game can set up their dialogue. When the SEQ file doesn't match the plugin, the quests still start, but their dialogue, scenes and some script fragments don't work until you save and reload.

SEQ files go stale when:

- a plugin is **ESL-flagged and its FormIDs are compacted**. The SEQ still lists the old IDs.
- a plugin's **master list changes**, which shifts the first byte of every FormID.
- quests are added after the SEQ was generated, or the author never shipped one.

Many mods pack their SEQ inside their BSA. A loose, regenerated SEQ file **does not override** a copy inside a BSA (tested in game), so regenerating it in xEdit isn't always enough.

## What AutoSEQ does

At the main menu, AutoSEQ:

1. Reads every loaded plugin from disk and works out what its SEQ file must contain, using the same rule as xEdit's *Create SEQ File*: every Start Game Enabled quest the plugin adds, or that it newly flags as start-enabled.
2. Reads the SEQ file the game actually sees (loose or inside a BSA) and compares the two.
3. Writes corrected copies of any stale or missing SEQ files into `AutoSEQ.bsa`. The empty, ESL-flagged `AutoSEQ.esp` loads that archive, and because it loads last, it overrides the outdated copies. No other mod's files are touched.
4. Prints a summary to the console, shows a message box when a restart is needed, and logs every plugin and quest it fixed.

A missing SEQ is only created when one of the plugin's start-enabled quests has dialogue. The base game and Creation Club files are skipped.

As a safety net, after a save loads AutoSEQ also starts any start-enabled quest that has never run. Quests that ran and were stopped on purpose are left alone.

## Installing

1. Install with Mod Organizer 2 or Vortex.
2. Enable `AutoSEQ.esp` and put it at the **very end** of your load order. It's ESL-flagged, so it doesn't use a full plugin slot.
3. Start the game. If AutoSEQ reports that it updated `AutoSEQ.bsa`, restart Skyrim before starting or loading a game.

**Requirements:** [SKSE64](https://skse.silverlock.org/) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444). Developed and tested on 1.6.1170.

## Checking the results

Open the console (`~`) at the main menu:

| Message | Meaning |
|---|---|
| `AutoSEQ: all N SEQ files are correct (M supplied by AutoSEQ.bsa)` | Everything is fine. |
| `AutoSEQ: X of N SEQ files were out of date - corrected in AutoSEQ.bsa, restart Skyrim to apply` | Fixes were written; restart once. |
| `AutoSEQ: X of N SEQ files are still out of date - load AutoSEQ.esp last` | The archive isn't winning; check the load order. |

Details are in `Documents\My Games\Skyrim Special Edition\SKSE\AutoSEQ.log`.

`tools/seqcheck.py` runs the same check without starting the game, including SEQ files inside BSAs:

```
python tools/seqcheck.py --mo2 "<MO2 folder>" --profile "<profile>" --game-data "<Skyrim>/Data" -v
python tools/seqcheck.py --data "<Skyrim>/Data" --plugins "<path to plugins.txt>" -v
```

## Building

Requirements: Visual Studio 2022 with the C++ workload (MSVC v143), CMake and vcpkg (`VCPKG_ROOT` set). Visual Studio's bundled vcpkg works.

- Open the folder in Visual Studio and pick the **Debug** or **Release** preset, or run `cmake --preset release` and then `cmake --build build/release`.
- `cmake/x64-windows-static-md.cmake` pins vcpkg to the v143 toolset. Without it, vcpkg picks the newest Visual Studio installed, and MSVC 14.50+ (VS 18) can't build the fmt version CommonLibSSE-NG depends on.
- Set `SKYRIM_MODS_FOLDER` to your mod manager's mods folder to have builds deployed to `<mods>/AutoSEQ`. The DLL and ESP are always copied; the placeholder `AutoSEQ.bsa` is only copied when missing, so existing fixes survive.
- **Release** builds also produce `dist/AutoSEQ/` and `dist/AutoSEQ-<version>.zip`, ready to upload. The version comes from `project(... VERSION ...)` in `CMakeLists.txt`.

## Credits

- The xEdit team: AutoSEQ follows xEdit's *Create SEQ File* rule.
- The CommonLibSSE and CommonLibSSE-NG authors and contributors.
- The SKSE team, and meh321 for Address Library.
- The modding community members who documented the SEQ dialogue bug.

## License

[MIT](LICENSE)
