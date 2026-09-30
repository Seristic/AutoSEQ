# AutoSEQ

An SKSE plugin for Skyrim Special Edition / Anniversary Edition that finds and repairs outdated or missing **SEQ files** every time the game starts.

## The problem

Every plugin with *Start Game Enabled* quests needs a matching `Data\Seq\<plugin>.seq` file. It lists those quests by FormID so the game can set up their dialogue. When the SEQ file doesn't match the plugin, the quests still start, but their dialogue, scenes and some script fragments don't work until you save and reload.

SEQ files go stale when:

- a plugin is **ESL-flagged and its FormIDs are compacted**. The SEQ still lists the old IDs.
- a plugin's **master list changes**, which shifts the first byte of every FormID.
- quests are added after the SEQ was generated, or the author never shipped one.

Many mods pack their SEQ inside their BSA. In testing, a loose SEQ file **did not override** a copy inside a mod's own BSA, so those can't be fixed with a loose file (one regenerated in xEdit included). AutoSEQ reports them instead.

## What AutoSEQ does

At the main menu, AutoSEQ:

1. Reads every loaded plugin from disk and works out what its SEQ file must contain, using the same rule as xEdit's *Create SEQ File*: every Start Game Enabled quest the plugin adds, or that it newly flags as start-enabled.
2. Reads the SEQ file the game actually sees (loose or inside a BSA) and compares the two.
3. Writes a new, corrected SEQ file for each stale or missing one into its own mod folder (`AutoSEQ\Seq`). Your mod manager puts those on top of the old ones, the same way any mod overrides another. The SEQ files other mods ship are never edited, and nothing is written into `Data`. AutoSEQ keeps a list of the files it made in `Seq\AutoSEQ.txt` and won't overwrite anything that isn't on it.
4. Prints a summary to the console, shows a message box when a restart is needed, and logs every plugin and quest it fixed.

A missing SEQ is only created when one of the plugin's start-enabled quests has dialogue. The base game and Creation Club files are skipped.

As a safety net, after a save loads AutoSEQ also starts any start-enabled quest that has never run. Quests that ran and were stopped on purpose are left alone.

## Installing

1. Install with Mod Organizer 2 or Vortex. AutoSEQ needs to be its own mod. If it's copied straight into `Data`, it won't write anything.
2. Give it the highest priority: the bottom of MO2's left pane, or in Vortex, set it to load after any mod it conflicts with.
3. Start the game. If AutoSEQ says it wrote new SEQ files, press F5 in MO2 (or Deploy Mods in Vortex) and restart Skyrim before starting or loading a game.

There's no plugin and no BSA.

**Upgrading from 1.0.0 or 1.1.0:** delete `AutoSEQ.esp` and `AutoSEQ.bsa` if you still have them (a clean reinstall does that). Those versions could write into `Data\Seq` and replace other mods' SEQ files: 1.0.0 when `AutoSEQ.esp` wasn't active, 1.1.0 outside MO2. If that happened to you, reinstall the affected mods to get their original files back.

**Requirements:** [SKSE64](https://skse.silverlock.org/) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444). Developed and tested on 1.6.1170.

## Checking the results

Open the console (`~`) at the main menu:

```
AutoSEQ: 120 plugins need an SEQ file - 110 OK, 0 written, 0 not picked up yet, 0 overridden by another mod, 10 overridden by a BSA, 0 failed
```

| Count | Meaning |
|---|---|
| OK | The SEQ the game uses already lists every quest it should. |
| written | New SEQ files were written this launch. Refresh your mod manager and restart once. |
| not picked up yet | AutoSEQ's file is there, but the game doesn't see it. Check AutoSEQ is enabled and refresh (F5 in MO2, Deploy Mods in Vortex). |
| overridden by another mod | Another mod's loose SEQ wins. Give AutoSEQ a higher priority. |
| overridden by a BSA | The stale SEQ is packed in that mod's own BSA, and a loose file can't beat it. |
| failed | Couldn't write the file. The log says why, for example a file with that name already in AutoSEQ's folder that AutoSEQ didn't make. |

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
- Set `SKYRIM_MODS_FOLDER` to your mod manager's mods folder to have builds deployed to `<mods>/AutoSEQ`. Only the DLL is copied, so the SEQ files AutoSEQ already wrote there are left alone.
- **Release** builds also produce `dist/AutoSEQ/` and `dist/AutoSEQ-<version>.zip`, ready to upload. The version comes from `project(... VERSION ...)` in `CMakeLists.txt`.

## Credits

- The xEdit team: AutoSEQ follows xEdit's *Create SEQ File* rule.
- The CommonLibSSE and CommonLibSSE-NG authors and contributors.
- The SKSE team, and meh321 for Address Library.
- The modding community members who documented the SEQ dialogue bug.

## License

[MIT](LICENSE)
