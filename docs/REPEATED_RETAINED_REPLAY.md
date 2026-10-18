# Three bounded retained-object replay cycles

Profile 12 requires three successful cycles in the same round. Each cycle scans at most 32 four-frame windows, preserves the reference inputs, restores a naturally removed retained effect object, and verifies all three replay steps. Neutral/no-removal windows retire without restoration and retry. Exhaustion or round changes end as incomplete, never a partial pass. Restoration, replay, ownership-release or workspace-recycling failures terminate immediately with the existing failure evidence and restart guards.

Successful cycles publish immutable frame ranges, diagnostics and separate animation/RNG trace files before the next cycle. Dependencies are released and snapshots/workspaces recycled on the game thread. The aggregate verified flag is published only after all three cycles and final cleanup. A changed protocol signature prevents old single-cycle builds joining this test. Timing policy, RNG checks, animation/camera diagnostics, round identity and restoration guards remain enabled. This remains reference replay, not continuous prediction or reduced-latency networking.

Validation: fixture tests cover all-three success, failure in the second cycle, round change between cycles, 32-window exhaustion after one success, cleanup failures and evidence publication ordering. Live validation requires both reports and normal gameplay; earlier one-cycle passes do not validate repetition.

## Second-cycle capture failure (65188 / 11948)

Both PCs completed frames 926–929 and failed before the next capture. The runner called `release_owned_dependencies()` before `recycle()`. The former calls `NativeSnapshotPool::release_dependencies`, which deliberately poisons every occupied slot. `retire()` reconstructs clean captures but does not clear quarantine; `acquire()` consequently finds no slot. This is a runner lifecycle error, not evidence of a second replay mismatch or round change.

The successful-cycle path now calls only `recycle()`, whose pool retirement already drains effect bindings and resource leases on the owning thread. Terminal/failure release retains its quarantine behavior. No validation bounds, round guards, RNG checks or diagnostic traces were removed. A real-pool regression reproduces exhaustion after terminal release and confirms three full four-slot cycles with ordinary retirement. The runner fixture models persistent quarantine so the previous ordering fails the multi-cycle test. Freeze causality beyond this failure path remains unverified; live testing is still required.
