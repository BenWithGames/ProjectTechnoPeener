# Effect birth accumulator investigation — 76196 / 36908

Investigation only. No runtime changes, comparison exclusions, new hooks or installation. All diagnostics, endpoint verification and round guards remain intact.

## Evidence

Ben: `steam-inputs-76196-retained-evidence.bin`, SHA256 `aec85072753cd7bb2420bb3dc7fbc8bb238b460cb070c3f54d6fec7440235147`.
Partner: `SC6Rollback-Report-steam-inputs-36908-a9929406.zip`, retained evidence SHA256 `5921314c48ec8d3a8e6c0ae49167d3d5ecb78cc3ebc4c0c289b195266dc252a8`.

Both passed three cycles and failed cycle four, third replay hop, frame 1030 to 1031. Restoration and native comparison completed; retained-effect comparison rejected object zero at +0x138. Type RVA 0x3E88CE8, allocation size 0x1E0.

The four archived snapshots establish six effects in slots 0–2 and seven in slot 3. The new head was absent from the source snapshot. Original and replay allocation addresses differ. All six existing effects agree byte for byte except linked-list pointers to the new head. This is not loss of a captured accumulator on an existing object.

| Field | Ben original | Ben replay | Partner original | Partner replay |
|---|---:|---:|---:|---:|
| +0x134 step | 0.040040046 | 0.040040046 | 0.040040046 | 0.040040046 |
| +0x138 accumulator | -587.924438 | 0.040040046 | 0.040040046 | 19.219286 |
| +0x13C limited accumulator | -587.924438 | 0.040040046 | 0.040040046 | 5 |
| +0x140 reciprocal | -0.001700899 | 24.974997 | 24.974997 | 0.2 |

Both endpoints show update counter +0x60 = 1, initialized +0x68 = 1 and completed countdown +0x148 = 0. The reciprocal calculations reproduce the raw saved float bytes exactly. Existing objects, fixed increment and iteration evidence do not support an extra simulation step as the explanation.

Reproduce with workspace `work/audit_effect_accumulator.py`. It asserts snapshot/endpoint correspondence, new allocation identity, existing-object equality excluding list links, matching increment, one update, and reciprocal arithmetic for both reports. Results: `work/report-36908/accumulator-audit.json` under this repository.

## Fresh Ghidra and instruction audit

Workspace outputs: `work/ghidra-sc6-focus/effect-accumulator-76196/`. Script: `SC6EffectAccumulatorAudit.java`. Read-only, no whole-program analysis. Function bodies use PE chained-unwind ranges. All seven requested decompilations completed. `machine-code.txt` independently records instructions in the constructor/setup/update/query paths.

* 0x334960 allocates 0x1E0 at call 0x334A19, writes base fields and links, calls 0x333E80, then sets countdown +0x148. It does not initialize +0x138.
* 0x333E80 spawn setup calls 0x332D60 base setup. Neither initializes +0x138.
* 0x333130 initial update sets +0x134 at 0x33327F, but not +0x138.
* 0x333550 advance loads +0x134 at 0x3336C7; **adds the existing +0x138 at 0x3336CF before writing it** at 0x3336DE. It limits the result by +0x118, stores +0x13C, and computes 1/result into +0x140 at 0x333702.
* 0x333C20 query consumes +0x140 at 0x333D65. This field is active spatial-effect state, not an audited padding gap.
* 0x4A61C0 delegates to the native allocator's virtual allocation method. Its wrapper contains no payload initialization. The allocator implementation and initial returned bytes were not recorded by these reports, so the exact old payload producing each seed is not proven.

The audited initializer leaves an active read-before-write accumulator dependent on allocation contents. Replay obtains another allocation. These two facts explain the shape of both failures and are strong evidence of heap-history dependence. Raw allocation-entry capture is still needed to prove the exact pre-initialization words, rather than infer them from post-update values. Do not describe arbitrary inverse floating-point subtraction as recovery of the exact seed.

## Timing and shared state

The failing arithmetic uses object-local step and accumulator before later noise/RNG work. Its audited calculation does not read a wall clock. Shared time inputs can set the step, but that step matches exactly here. Saved/next/live manager time-cache words were both 1.0 in the failure evidence. This run therefore does not exercise the earlier 1.0-to-0.4 cache-restoration fix.

| PC | Passing cycle 2 callback max | Passing cycle 3 callback max | Failing cycle 4 callback max | Cycle 4 restore | Cycle 4 replay |
|---|---:|---:|---:|---:|---:|
| Ben | 31.294 ms | 30.328 ms | 31.556 ms | 18.548 ms | 4.295 ms |
| Partner | 35.774 ms | 37.112 ms | 36.159 ms | 19.161 ms | 4.796 ms |

These are nested timing measurements, not additive components. Both reports identify baseline_100ms hold mode. Callback costs exceed a 60 Hz frame budget and can cause stutter. The failure did not coincide with an exceptional measured cost. Timing could indirectly influence allocator reuse through other threads; reports do not prove or exclude that. More importantly, retained snapshots already change which allocations remain available. Replay must handle birth-state dependence regardless of whether a visible pause occurred. No evidence here establishes a timing-induced shared RNG divergence as the cause of +0x138.

## Next implementation boundary

Existing image restoration copies full captured object bytes and verifies the write, but cannot initialize an object not present at the source frame. The allocation observer currently records identity and routes retention bookkeeping without recording initial payload words.

Prefer a bounded birth-state journal at the already audited allocation return site: record the original pre-initialization accumulator, associate it with an unambiguous in-window allocation event, and reproduce that input before native initialization during replay. Validate type, size, site, owner/thread, session/round and allocation sequence; reject ambiguity, overflow or missing events. Retain raw before/after diagnostics and exact endpoint comparison. Validate native construction first with read-only birth evidence and regression coverage before enabling substitution. A broader deterministic initialization policy would change original native behavior and needs separate justification; do not simply zero only the replay allocation or copy the expected endpoint over the failed result.

This report identifies a specific causal gap and a testable correction approach, not a claim that all effect or allocator behavior is audited.
