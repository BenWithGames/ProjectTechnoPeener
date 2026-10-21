# Camera-slot restoration and diagnostics

Pinned executable SHA256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.

## Evidence and change

Report 9356 failed before loading at native frames 823 -> 824. Record 749 changed writer from 321814 (empty, -1 tag) to 3217EC (occupied, type tag). A type-0 camera serializes the 41 base records plus five derived records, matching the observed increase of 46 records. The old report did not contain the type payload, so this correspondence is not proof of the actual type.

Ghidra save/load 321750/321A30, factories 321F40 through 323140, and base/derived save/load methods confirm a manager-owned pool. Native clear 323270 only zeros active table entries; it does not release pool storage. Constructor 31F710 installs manager vtable 3E885A0. Its virtual pool size/count are 31F700 (800 bytes) and 31F480 (16).

CameraSlots captures the tags, fixed serializer schemas, pool identity, active table and manager/arena identity. It maps records outside camera-slot blocks exactly. Slot payloads must match their audited writer/length/source-offset schema. Restoration clears target-empty entries and lets the original loader reconstruct target-present objects in the original pool. Live table/vtable/index checks run before mutation and after loading.

Supplemental camera observations are aligned by owner/type, with missing/retyped non-smoothing pool cameras admitted only under the slot contract. The main smoothing-camera type (3) cannot be born, removed, or replaced by this path; its existing history/counter checks remain. Exact post-load and replay endpoint comparisons remain enabled, including the existing narrowly audited compatibility rules. No generic record-count relaxation, ignored active payload bytes, or changed round admission.

## Diagnostics

continuous_camera_slots_* reports both captured layouts: type, first/end record, payload offset/length, pool and active pointer. For the first changed slot it includes raw little-endian payload words and retained-byte counts. The 1024-byte bound covers all audited type payloads. Reports distinguish full payload length from retained length.

## Validation

Test-CameraSlots.ps1 is included in Build-Native.ps1. Tests cover all 100 pairs of empty/types 0..8, rejection of main smoothing-camera replacement, the 46-record birth, malformed schemas, unknown owners, stale live occupancy, exact non-slot source checks, and raw payload diagnostics. Native in-game validation is still required; local tests do not certify every camera transition in a match.

Ghidra output: slot-layout-audit, slot-factory-audit, slot-all-factories, camera-slot-payload-audit, slot-retirement-audit (323270), slot-pool-contract-audit and slot-pool-init-audit (31F710). Earlier speculative addresses 323240 and 321C70 are not evidence for the implementation.
