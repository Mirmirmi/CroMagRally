# Cro-Mag Rally (Custom Fork)

> **Note & Attribution:** This repository is a personal fork of **[Iliyas Jorio’s modern port of Cro-Mag Rally (`jorio/CroMagRally`)](https://github.com/jorio/CroMagRally)**. Nearly all of the port to modern operating systems is Jorio’s work (upon Pangea Software’s original 2000 game); this fork layers a custom vehicle, physics/gameplay bug fixes, and Raspberry Pi (`aarch64`) build support on top of his port.
>
> The section immediately below lists the changes made in this fork, followed by **[Jorio’s original README](#original-upstream-readme-by-iliyas-jorio)** preserved in full.

## Changes in This Fork (`v4.1`)

### Multiplayer CPU Racers & Pre-Race Options (`v4.1`)
- **CPU Grid Fill & Finish Condition:** Enables CPU racers to fill remaining slots up to 6 total racers in local split-screen Multiplayer Races (`GAME_MODE_MULTIPLAYERRACE`), ending the race when the first human player crosses the finish line and displaying per-player split-screen final standings via [`ShowFinalPlace()`](Source/Screens/Infobar.c).
- **Pre-Race CPU & Difficulty Menu:** Adds [`DoMultiplayerRaceOptionsScreen()`](Source/Screens/SelectVehicle.c) after multiplayer human character/vehicle selections with a **CPU Racers** On/Off toggle (default On) and the **Difficulty** bone indicator below it, with Back navigation to the previous screen.
- **Full CPU Vehicle Pool:** Allows multiplayer CPU racers to select randomly from all 11 land vehicles (`NUM_LAND_CAR_TYPES`), preventing duplicates below Hard difficulty.

### New Vehicle: **Scuttle-saur** (`CAR_TYPE_LIZARD`)
- **Custom 3D Vehicle & Texture:** Adds a brand-new 11th land vehicle—the **Scuttle-saur**, a crested prehistoric reptile racer with a custom 3D mesh and 256×256 procedural scale texture packed into [`Data/Models/carparts.bg3d`](Data/Models/carparts.bg3d) and [`Data/Models/carselect.bg3d`](Data/Models/carselect.bg3d).
- **Rotary Lizard Gait & Body Slither:** Replaces standard spinning wheels with 360° rotary crank legs (diagonal pairs `FL+BR` and `FR+BL` 180° out of phase) and a speed-synced sinusoidal body yaw/roll waddle in [`Source/Player/Player_Car.c`](Source/Player/Player_Car.c).
- **Balanced Mid-Pack Stats & Stone Age Unlock:** Positioned at slot 6 in the vehicle lineup (unlocked from the start in Stone Age tournaments and available to CPU racers), matching the Bone Buggy's speed and acceleration (`5, 4`) with enhanced stability (`5` Traction, `4` Suspension).
- **Full Localization & Editor Support:** Localized across English, French, German, Spanish, Italian, and Dutch in [`Data/System/strings.csv`](Data/System/strings.csv) and exposed in the in-game Physics Editor.

### Collision & Physics Fixes
- **Relative Collision Velocity Scaling:** Fixes a unit mismatch in [`CollisionDetect()`](Source/System/Collision.c) where per-second target velocity (`thisNode->Delta`) was subtracted directly from per-subpass displacement (`realDX/Y/Z`), which previously inverted relative velocity signs for objects moving in the same direction (causing no-clip collisions on wandering Vikings/trolls and same-direction vehicles).
- **Minimum Penetration Depth Resolution:** Resolves already-overlapping solid bounding boxes along the shallowest penetration axis (`minPen`) rather than velocity direction, preventing side-by-side vehicle scrapes or rear-end contacts from slingshotting cars across opposite bounding box faces.
- **Multi-Pass Collision & Momentum Sync:** Fixes [`HandleCollisions()`](Source/System/Collision.c) so secondary collision passes do not undo primary pass separation or queue duplicate collision entries, deduplicates [`VehicleHitVehicle()`](Source/Player/Player_Car.c) per sub-pass, and keeps `car1->Delta` synchronized with `gDelta`.
- **Spline Execution Order:** Runs `MoveSplineObjects()` before `MoveObjects()` in [`MoveEverything()`](Source/System/Main.c) so spline-driven hazards and NPCs have up-to-date positions before collision detection.

### Gameplay, AI & Stability Fixes
- **Druid Powerup Trigger:** Fixes the Stonehenge Druid trigger in [`Source/Items/Triggers.c`](Source/Items/Triggers.c) so it properly activates `SetStickyTires()` and `SetSuspensionPOW()` physics routines instead of only setting timers.
- **Goddess Lightning & Cactus Terrain Coordinates:** Fixes coordinate typos in [`Source/Items/Traps.c`](Source/Items/Traps.c) (using `goddess->Coord.z` instead of `Coord.x` for lightning endpoints) and [`Source/Items/Triggers.c`](Source/Items/Triggers.c) (sampling terrain height at `(x, z)` instead of `(x, y)` for exploding cactus parts).
- **Submarine AI State Reset:** Resets per-player submarine stuck/evasion timers on player initialization in [`Source/Player/Player_Submarine.c`](Source/Player/Player_Submarine.c) so state does not leak across Atlantis races.
- **Memory & Bounds Hardening:** Fixes a fence vertex index over-read in [`Source/Terrain/Fences.c`](Source/Terrain/Fences.c), clamps item type parameters in [`Source/Items/Items.c`](Source/Items/Items.c) and [`Source/Items/Triggers.c`](Source/Items/Triggers.c), guards `MakeNewDisplayGroupObject()` against `NULL` dereferences, and validates skeleton animation indices in [`Source/Skeleton/SkeletonAnim.c`](Source/Skeleton/SkeletonAnim.c) and [`Source/System/File.c`](Source/System/File.c).

### Build & CI Changes
- **Linux `aarch64` (Raspberry Pi) AppImage CI:** Updates [`.github/workflows/ReleaseBuilds.yml`](.github/workflows/ReleaseBuilds.yml) to automatically build native `aarch64` and `x86_64` Linux AppImages on push.

---

## Original Upstream README (by [Iliyas Jorio](https://github.com/jorio/CroMagRally))

### *The wildest racing game since man invented the wheel!*

This is a port of Pangea Software’s racing game **Cro-Mag Rally** to modern operating systems.

**Download the game for macOS, Windows and Linux here:** https://github.com/jorio/CroMagRally/releases

![Cro-Mag Rally Screenshot](docs/screenshot.webp)

## About Cro-Mag Rally

> In Cro-Mag Rally you are a speed-hungry caveman named Brog who races through the Stone, Bronze, and Iron Ages in primitive vehicles such as the Geode Cruiser, Bone Buggy, Logmobile, Trojan Horse, and many others. Brog has at his disposal an arsenal of primitive weaponry ranging from Bone Bombs to Chinese Bottle Rockets and Heat Seeking Homing Pigeons.
> 
> In addition to single-player racing where one player races against the computer, there are also several different multi-player modes including Tag, Capture the Flag, and Survival. Up to four players can play on a single computer in split-screen mode.

CMR was released in 2000 by Pangea Software as a Mac exclusive, and it was a pack-in game on Macs that came out around that time.

## About this port

This is a port of the original OS 9 version of the game. It aims to provide the best way to experience CMR on today’s computers. It is an “enhanced” version insofar as it fixes bugs that may hinder the experience, and it brings in a few new features in keeping with the spirit of the original game.

Some of the new features include:
- Up to 4 players in split-screen multiplayer (up from 2 in the original).
- The UI is subtly animated and has been tweaked to be pleasant to look at on modern widescreens.
- Enable a timer in race modes to hone your racing skills, and keep track of your records in the all-new scoreboard!

I haven’t had time to restore NetSprockets multiplayer from the OS 9 version yet, but that may come in a later release.

### More documentation

- [BUILD](BUILD.md) – How to build the game from source
- [CHANGELOG](CHANGELOG.md) – Cro-Mag Rally version history
- [LICENSE](LICENSE.md) – Licensing info (see also below)
- [SECRETS](SECRETS.md) – Cheat codes!

### Legal info

Cro-Mag Rally © 2000 Pangea Software, Inc. Cro-Mag Rally is a trademark of Pangea Software, Inc. This version was made and re-released here (https://github.com/jorio/CroMagRally) under permission from Pangea Software, Inc.

This version is licensed under [CC-BY-NC SA 4.0](LICENSE.md).

## More Pangea stuff!

Check out my ports of [Bugdom](https://github.com/jorio/Bugdom), [Nanosaur](https://github.com/jorio/Nanosaur), [Mighty Mike (Power Pete)](https://github.com/jorio/MightyMike) and [Otto Matic](https://github.com/jorio/OttoMatic).

All ports are free of charge! If you’d like to support the development of Pangea game ports, feel free to visit https://jorio.itch.io and name your own price for any of the games there. Much appreciated! 😊
