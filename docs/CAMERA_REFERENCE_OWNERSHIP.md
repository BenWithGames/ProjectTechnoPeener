# Camera reference ownership and layout restoration

Failure: paired match 1456808c5a388c484aa7fdd64cdeb32f. The local fifth-round restore rejected stream layout before calling the native loader. Auxiliary reference records 32AC76/32ACAC appeared with a non-null target; the previous optional-record rule supported only null references. The friend's stop followed the local failure. The failed archive did not retain the raw auxiliary reference target, so this change supports the audited ownership case without claiming the missing target was recovered.

Ghidra audit of the pinned executable (SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553):

- 322610 initializes auxiliary slot 4 in the manager-owned arena at +0xF80, stride 0x3E0, vtable 0x3E88858. It stores that address in owner+0x30. It does not allocate a separate controller.
- 324320 assigns the player argument to controller+0x3B8 after calling that initializer. Subsequent reads include player+0x19EC, +0x973E8 and position fields. There is no reference-count acquisition at this assignment.
- 3841E0 builds relocation entry 0 = null, entry 1 = P1, entry 2 = P2; both player ranges have size 0x973F0. 384540 rebuilds the same indexed roots from the live player globals before loading the players and camera.
- 32AC00 encodes the reference as a 32-bit relocation index and 64-bit offset, followed by the 64-bit value at referenced player+0x95710. Descriptor bytes 4..7 are previously audited temporary padding, not an ownership field.
- 3249E0 loads the auxiliary header (0x2F0) and arena (0x41E0) before invoking the restored objects' virtual readers.
- 32ACE0 reconstructs +0x3B8 from the relocation root and offset. For a non-null reference it adjusts +0x3C8 by the unsigned 32-bit difference between the current player+0x95710 value and the saved value, clears +0x3C0, and sets +0x3D0. 32EB50 uses the pending flag to initialize the dependent state before normal updates. We retain that native behavior; there is no pointer patch, invented AddRef, or new equality exclusion.

Fresh decompilation and assembly: work/ghidra-sc6-focus/camera-reference-ownership-audit in the parent workspace. The script reconstructs chained unwind bodies and runs with -noanalysis -readOnly. The 0x3B8 writer scan covers runtime-function ranges in the camera region, not a whole-game proof that no other writer exists.

Implementation:

1. Production snapshots bind the captured player roots and auxiliary owner/arena to live identities, including all 17 fixed object addresses. The absent-auxiliary path still works and grants no ownership permission.
2. Optional non-null reference records can appear or disappear only for the audited slot 4 controller, with relocation index 1 or 2, an offset within the native inclusive 0x973F0 range, and a resolved address equal to an exact captured player root. Resolution uses checked subtraction to reject wrapping addition. Arbitrary, interior, unbound or stale references remain rejected. The saved and current auxiliary block sources must match their captured ownership.
3. Before native loading, live identity checks verify the players and auxiliary arena again. Existing round, session, thread, resource, source and replay-span guards still apply. Player lifetime is borrowed only within this validated round; snapshots do not keep a player alive across teardown.
4. The existing native loader restores the controller and its reference. A new post-load check requires all auxiliary object types and serialized reference targets to match the saved snapshot. Existing full restoration and replay endpoint verification remain unchanged.

Diagnostics retain all earlier evidence and append continuous_camera_references_schema=1, per-sample owner/arena/player roots, and per-reference record, target, relocation index, offset, saved resource value and ownership result. continuous_restore_reference_check_* records post-load stage, reason, slot, expected and actual values. Check reasons: 1 ownership changed; 2 reference schema; 3 restored type; 4 restored target; 5 access exception. Stage 2 confirms the reference postcheck. Map reason 5 remains unsupported non-null ownership; reason 7 identifies an auxiliary source that disagrees with its captured ownership.

Regression coverage includes both player roots, both appearance/disappearance directions, unknown targets, wrong relocation side/index/offset, unbound and stale ownership, exact native-load result checks, null references, the absent-auxiliary path, and previous primary-camera schemas. The native-load unit fixture models the audited arena-copy/relocation contract; it is not an execution of the game binary. A live two-PC match is still needed to validate this build in gameplay.

Adaptive 0-2 frame delay, the 3-frame prediction limit, existing optimizations, ownership cleanup and round safety are unchanged.

Follow-up: match 39785d88711dc93f4aa15bccfa575a56, local frames 8431 -> 8432.
The camera reference at auxiliary slot 4 resolved exactly to P2 but encoded relocation
index 1 + offset 619504 (0x973F0). The prior zero-offset guard rejected map reason 5
before invoking native loading. The paired peer had zero local failures and stopped.
The existing Ghidra assembly at 32AC47 uses JBE after comparing the target-root
difference with the relocation extent. Thus the P1 range includes adjacent P2's
base; 32ACE0 adds the indexed root to the saved offset. This is an alternate exact
player encoding, not permission for arbitrary interior pointers.
Regression coverage now includes this encoding in both layout mapping directions,
the post-load exact-target check, offsets one below/above the endpoint, UINT64_MAX,
and a self-consistent interior pointer. Existing stale ownership and source checks
remain. Live validation of this follow-up is pending.
