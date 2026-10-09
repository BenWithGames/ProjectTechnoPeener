# SC6Rollback — initial C++ scaffold

This is an experimental SC6 rollback research mod. Bounded offline movement and
single-A correction tests have passed; network rollback is not implemented yet.

A standalone [two-peer network diagnostic](docs/NETWORK_DIAGNOSTIC.md) now tests
authenticated UDP input exchange and late-input replay using the existing bounded
controller and a synthetic simulation. Local two-process tests passed with added
latency and packet loss. It is separate from the game DLL; online SC6 rollback
and real opponent telemetry still require game integration.

The opt-in [input observation mode](docs/INPUT_OBSERVATION.md) now captures
120 actual frame-boundary input envelopes and exchanges them without restoration
or injection. Transport delivery is tested with mock mappings; real-game capture,
player mapping and cross-PC frame alignment still need validation.

The native build includes a click-through status panel at the bottom of the game
window. It displays **SC6Rollback ENABLED**, network status, opponent ping (ms),
input delay (frames), and rollback depth (frames). ENABLED means the mod is loaded,
not that online rollback is active. Unmeasured values show `--`; no simulated ping
or inferred delay is displayed. The panel supports windowed/borderless play;
exclusive fullscreen may hide it. It hides when SC6 loses focus or is minimized.
The black panel is 50% transparent; text uses full opacity with antialiased edges.

Future network integration should publish measured values through
`StatusOverlay::publish(OverlayStats)`. Publish on connection and disconnection;
connected telemetry older than three seconds is marked stale and its values hidden.
Delay means configured input delay; rollback means the most recent correction depth.
The current build does not publish online telemetry, so the panel says Network: inactive.

## Contents

- `src/Mod.cpp`: UE4SS entry points and load logging.
- `include/sc6/Game.hpp`: the capture/restore/advance contract for a future SC6 adapter.
- `tests/ContractTest.cpp`: a toy simulation validating frame semantics and replay.
- `include/sc6/SessionPolicy.hpp`: private opt-in and compatibility decisions.
- `tests/SessionPolicyTest.cpp`: original-networking defaults and private-session gates.
- [Session policy](docs/SESSION_POLICY.md): handshake and ranked fallback requirements.
- `CMakeLists.txt`: standalone test build by default; optional UE4SS DLL build.
- [Training observation](docs/OBSERVATION.md): read-only Python probe, captures,
  candidate address provenance, and verification limits.

Snapshots represent state **before** `next_frame`. Advancing consumes inputs
for that frame and increments it. State bytes must use an explicit encoding,
not memcpy of structs with padding or pointers. A real implementation must
capture all gameplay-affecting state and handle resimulation presentation effects.

## Build the standalone test

On this machine, run `.\scripts\Build.ps1` in PowerShell. It locates the
Visual Studio compiler and runs both tests. See [setup status](docs/SETUP_STATUS.md)
for the current native-loader setup and verification results.

The downloaded v3.0.1 headers/runtime can now build the logging DLL with
`.\scripts\Build-Native.ps1`. This uses the existing workspace dependencies
and generates an import library from the matching runtime's exports. It stages
`package/Mods/SC6Rollback/dlls/main.dll`. Startup through Steam is verified with
UE4SS v3.0.1 and `bUseUObjectArrayCache = false`: both startup messages appear
and the title screen is reachable. Gameplay and networking are not tested yet.

Use an x64 Native Tools Command Prompt for VS 2022. C++20 is required.

```bat
mkdir build
cl /nologo /std:c++20 /EHsc /W4 /I include tests\ContractTest.cpp /Fo:build\ContractTest.obj /Fe:build\sc6_contract_test.exe
build\sc6_contract_test.exe
cl /nologo /std:c++20 /EHsc /W4 /I include tests\SessionPolicyTest.cpp /Fo:build\SessionPolicyTest.obj /Fe:build\sc6_session_policy_test.exe
build\sc6_session_policy_test.exe
```

Or, with CMake installed:

```bat
cmake -S . -B build-core -G "Visual Studio 17 2022" -A x64
cmake --build build-core --config Debug
ctest --test-dir build-core -C Debug --output-on-failure
```

Passing this test only validates the toy simulation and contract. It says
nothing about SC6 determinism or whether SC6 supports complete state restoration.

## Build the DLL (next setup stage)

First identify your SC6 executable version and working UE4SS/shimloader setup.
Use UE4SS sources matching that runtime's ABI; do not assume current development
sources are compatible with an older loader. Follow the selected revision's
build requirements, including its submodules and any Unreal source access.
This scaffold does not download or choose that dependency automatically.

For a compatible CMake-based UE4SS checkout:

```bat
cmake -S . -B build-mod -G "Visual Studio 17 2022" -A x64 -DSC6_BUILD_MOD=ON -DUE4SS_SOURCE_DIR="C:/path/to/RE-UE4SS"
cmake --build build-mod --config Game__Shipping__Win64 --target SC6Rollback
cmake --install build-mod --config Game__Shipping__Win64 --prefix build-mod/package
```

The install step stages `SC6Rollback/dlls/main.dll` locally. Once loader
compatibility is verified, copy that folder into the loader's `Mods` directory
and add `SC6Rollback : 1` to its `mods.txt`. The project never automatically
copies files into the game. Launch offline and check the UE4SS log for
`[SC6Rollback] loaded` and `[SC6Rollback] Unreal initialized`.

## Research milestones

1. Verify DLL loading against the installed runtime.
2. Identify and observe the actual battle simulation boundary and inputs.
3. Verify pause and single-frame stepping; then record/inject inputs.
4. Implement a complete, version-specific snapshot and restore adapter.
5. Compare every replayed frame with the original, including field differences.
6. Exercise delayed inputs offline, then evaluate GGPO integration.

No SC6 adapter is supplied because its addresses, calling conventions, object
lifetimes, and complete state layout have not been verified. Do not substitute
guessed offsets or treat mod update callbacks as combat ticks.

## References

- [UE4SS C++ mod guide](https://docs.ue4ss.com/dev/guides/creating-a-c%2B%2B-mod.html)
- [UE4SS installation layout](https://docs.ue4ss.com/dev/guides/installing-a-c%2B%2B-mod.html)
- [UE4SS source and build requirements](https://github.com/UE4SS-RE/RE-UE4SS)
- [SCUFFLE research source](https://github.com/FottenSC/Scuffle)
- [GGPO](https://github.com/pond3r/ggpo)
