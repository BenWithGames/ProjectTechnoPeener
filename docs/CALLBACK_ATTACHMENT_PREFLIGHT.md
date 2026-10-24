# Callback attachment preflight, 2026-10-22

The paired reports for match `1b9eaad495964c3260f474d3a21573fc`
(processes 83820 and 16952) rejected the first snapshot before any live update,
correction, or restoration. Both stopped in callback preflight, phase 2, at the
old `target + 0x528 == 0` condition. The recorded value was 2. Loading agreement
had completed; this was not a network timeout or a sway-restoration rejection.

## Pinned native evidence

Executable SHA-256:
`F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553`.
Addresses below are RVAs in this executable. Ghidra exports were made read-only
from `SC6_AnimationFocus`, using chained `.pdata` ranges so the functions include
their separated code regions. Assembly was checked alongside decompilation.
Exports are under `work/ghidra-sc6-focus/callback528-audit`,
`callback-attachments-audit`, and `callback-particle-audit` in the workspace.

| Location | Evidence |
| --- | --- |
| Owner vtable `3268078` | The callback owner already admitted by the existing exact type check. |
| Owner `+520/+528/+52C` | Buffer, signed count, signed capacity of a 16-byte-entry array. The count is not a readiness flag. |
| `3CAEB0`, including `3CB5BB` | Creates components, stores weak object references plus an eight-byte socket name, increments count and grows capacity as needed. |
| `AFDA00`, `8CB8C0` | Class registration names `LuxParticleSystemComponent`; constructor installs primary vtable `335DB28`. |
| `3C6050 -> 3C90B0` | Existing post-load callback refreshes native presentation state and traverses this attachment array. |
| `3C98B5/3C98BC` | Reads array count/buffer and iterates with 16-byte stride. |
| `F823F0` | Weak resolution rejects serial zero, negative/out-of-range index, mismatched serial, and object flags masked by `30000000`; null results are skipped. |
| `3C90B0 -> 1D92B60` | Resolves the named parent and performs native attachment updates when present. The entry's last eight bytes are a name, not an object pointer. |
| Component slot `3A8 -> 1DACA60` | Native scale update for the audited particle component. |
| `3CCB70`, `3D5030` | Other native consumers also traverse the nonempty weak-reference list. |

## Change

`CallbackAttachmentGuard` replaces only the empty-list assumption. It validates
nonnegative count, capacity at least count, bounded storage arithmetic, weak
resolution, and the exact audited live component vtable and scale method.
Expired/deleted/null weak targets are skipped using the native resolver's rules;
their old addresses are never dereferenced or resurrected. Unknown live component
types and methods still reject. The explicit 4,096-entry read budget is a mod
safety limit, not a claim about a native array-capacity limit.

The guard is read-only, bounded, and allocation-free. It does not zero the count,
change references, replace native callbacks, or add ownership of particle objects.
Existing callback-owner identity, delegate layout, method checks, primary
components, snapshot verification, ownership, adaptive delay, and round checks
remain in place. This supports the native attachment-refresh path; it does not
claim new general particle-state snapshot coverage.

Preflight report schema 2 preserves the original observation ring and appends
per-owner count, capacity, live/expired totals, last entry/object/vtable, and exact
attachment rejection reason. Reasons: 0 none; 1 shape/budget; 2 storage;
3 registry; 4 live object address; 5 component type; 6 scale method.

## Validation

`SnapshotPreflightTest` exercises both callback owners with two live attachments,
native expired-reference cases, null references, unchanged owner/entry bytes,
unknown types/methods, invalid addresses, malformed array shape/storage, and the
explicit traversal budget. A separate helper check covers a corrupt global
registry that the outer preflight already rejects earlier. Existing preflight
exceptions and all earlier identity/callback/component checks remain covered.
All 68 test groups in the full local suite passed, including `FullStateProbeTest`.
The test manifest result is `build-native/test-runs/20261022-223837-81568.json`;
DLL compilation and linking are logged in `build-native/callback-attachments-build.log`.

The reports identify the old zero-only rejection exactly. They do not contain
the resolved particle entries themselves, so the new type checks require a live
two-PC run to confirm this particular match proceeds. New evidence identifies any
remaining unexpected entry without accepting an unknown callback blindly.
