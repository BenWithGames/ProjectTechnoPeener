# Record 749 camera-slot layout audit — 2026-10-20

Report: SC6Rollback-Report-steam-inputs-9356-1970859b.zip. Correction frame 579, native boundaries 823 -> 824, depth 3. Saved/next stream counts 2248/2294, byte counts 110416/110744. First mismatch record 749, length 4 on both sides, same temporary source. No native load invoked for this failed correction.

## Verified binary evidence

Module-relative writer return addresses are 0x321814 (saved) and 0x3217EC (next). Ghidra decompilation plus disassembly of 0x321750 confirms:
- Manager at module+0x470DF28; slot table at manager->+0x90, offset +0x270, up to sixteen slots.
- Null slot writes uint32 0xFFFFFFFF at return address 0x321814.
- Occupied slot writes virtual +0xE0 type result at return address 0x3217EC, then invokes virtual +0x100 to serialize the object.
- Both writes use the same stack-local address. This is an actual slot birth, not a temporary source-address relocation.

Native loader 0x321A30 reads the tag and dispatches types 0..8 to factory functions, then virtual +0x108 to load the object. Its default branch (including -1) skips without clearing the occupied slot table entry. Thus ignoring shape mismatch alone cannot rewind an absent -> present transition.

Factories 0x321F40,0x322020,0x322C60,0x322D30,0x322E00,0x322ED0,0x322FA0,0x323070,0x323140 were decompiled. They use manager-owned slot storage, initialize it, and publish it in the active table. This is not evidence that arbitrary live objects can be detached/reconstructed without auditing their supplemental dependencies.

## Original investigation status (superseded by implementation)

Current IdleTrajectoryAudit::same_shape demands positional writer/length equality. FullStateProbe::delayed_sources_ok also compares positional records; CameraSnapshot requires matching owner/count. These assumptions must be addressed together with native slot occupancy restoration. Do not simply relax same_shape or suppress the 46 additional records.

Required implementation: capture slot type/occupancy and backing-storage identity; validate pool/dependencies; restore empty-slot state before native load; map variable camera blocks for preparation without weakening non-camera source checks; preserve exact endpoint verification and round guards. Validate both birth and removal and type replacement, including rejection before mutation for unsupported cases.

The report ZIP has text/CSV evidence only, no serialized snapshot payload. It does not identify the new type, slot index, or its full block. Those must be retained in failure diagnostics for a reproducible fixture.

Fresh read-only Ghidra output: work/ghidra-sc6-focus/slot-layout-audit, slot-factory-audit, slot-all-factories. No runtime behavior, DLL, installation, or friend package changed in this investigation.

Implementation follow-up: CAMERA_SLOT_RESTORATION.md documents the implemented slot restoration, exact schema and backing-store checks, diagnostics, and regression coverage. Live validation remains pending.
