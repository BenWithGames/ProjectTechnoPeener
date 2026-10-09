# Loader setup status

The native DLL now compiles and links against the UE4SS v3.0.1 release and
matching headers. Both exported entry points were verified. The game log
confirms `[SC6Rollback] loaded; research scaffold only`.

Startup is now verified through Steam: the user reached the title screen,
the game process remained responsive, and the fresh log contains both messages:

```
[SC6Rollback] loaded; research scaffold only
[SC6Rollback] Unreal initialized; SC6 hooks not installed
```

Working configuration: UE4SS v3.0.1 Game__Shipping__Win64, engine detected as
4.17, `bUseUObjectArrayCache = false`, GUI/console windows disabled, only
SC6Rollback enabled in `Mods/mods.txt`. See `../startup-verified.log`.

Earlier direct-executable tests failed, including one without a loader. The
initial loader crash was symbolized as Unreal 4.17 object-array iteration.
Because both the launch method and cache setting changed, this does not prove
which change resolved that crash. Use Steam for subsequent tests.

The v3.0.1 `dwmapi.dll` and `UE4SS.dll` are active in the game's Win64 directory.
An experimental proxy remains renamed `dwmapi-experimental.dll.disabled`;
the experimental `ue4ss` subfolder is not the active runtime. No original game
files were overwritten. Gameplay, frame control, and ranked compatibility
remain untested.

## Local prerequisites found

- Visual Studio 2022 Community, MSVC 14.44.35207.
- CMake bundled with Visual Studio 2022 Build Tools (not on PATH).
- Game executable: `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe`.
- No loader files were present before this task's installation.

## Dependency research

Downloaded official UE4SS v3.0.1 development runtime and checked out source at
`d935b5b23bac03b65c14ae38382b02007204cc2e` in the workspace's `work` directory.
This is a candidate matching source/runtime pair, not a verified SC6 selection.
The development ZIP contains runtime/debug files, not a complete C++ SDK.

The source requires `Re-UE4SS/UEPseudo` at
`d09b7218bfe7392adeffb500fdeee0b42ca1cd27`. Anonymous access failed and Git
requested authentication. GitHub authentication and EpicGames membership are
now working, and the pinned dependency was downloaded. The referenced HorseMod repository could not be retrieved
anonymously, so cached web examples cannot establish current compatibility.

Follow the official UE4SS build access prerequisites using your own GitHub
account; do not put access tokens or passwords into this project or chat.
After source access works, initialize the pinned dependencies and build a
matched loader/mod pair. A successful build must still be tested in SC6 offline.

## Repeatable commands

From the project directory in PowerShell:

```powershell
.\scripts\Build.ps1
.\scripts\Build-Native.ps1
.\scripts\Build.ps1 -Target Mod -UE4SSSource 'C:\path\to\RE-UE4SS'
```

Build-Native.ps1 is the verified native build path for the downloaded v3.0.1
dependencies. It derives an import library from that exact runtime's exports,
uses the corresponding headers and release CRT, and verifies the DLL exports.
The source-build alternative in Build.ps1 uses bundled CMake for the DLL,
checks command failures, runs both core tests,
and stages a successful mod build locally. It never installs files into SC6.

The CMake Visual Studio generator also encountered an SDK registration access
error in this session. Core builds use the working native compiler route;
the DLL's CMake path remains unverified until dependencies and SDK access work.

## Remaining acceptance checks

1. Observe a training session without mutating game state.
2. Identify the actual battle-frame boundary and input representation.
3. Implement and verify frame stepping, then recording/replay.
4. Integrate the private/ranked policy only after reliable match-mode detection.

References:
- https://github.com/UE4SS-RE/RE-UE4SS/tree/v3.0.1
- https://docs.ue4ss.com/dev/guides/creating-a-c%2B%2B-mod.html
