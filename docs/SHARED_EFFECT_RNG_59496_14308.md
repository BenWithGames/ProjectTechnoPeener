# Shared effect RNG / replay audit — 59496 and 14308

Both reports passed cycle 1, then failed the third replay update of cycle 2 (source 776, expected 777). Restoration was verified. Camera bytes, native payload, blend buffers, ring state, main RNG and player scalars matched. Transform comparison bit 128 alone was missing (mask 895).

Active differences: both roots at 1F0/1F4/1F8; shared generator cursor at 485EB30+64; six generator words at 470E2B0. This is not evidence that the previous camera-limit repair was exercised: camera equality only shows this run had no camera mismatch.

## Ghidra evidence

Focused scripts SC6TransformRngAudit.java and SC6WindQueryAudit.java, outputs workspace work/ghidra-sc6-focus/transform-rng-59496 and wind-query-59496. Query functions were re-run with PE unwind/chained body extents rather than relying on one-byte function bodies. Pinned executable is unchanged.

- 3342A0 computes the root vector by visiting the effect manager's list, calling each object's query, accumulating vectors, and applying manager scales. Copying the expected endpoint over the vector would hide an upstream divergence.
- 331DD0 returns the stored ambient vector. 331DF0 additionally uses stage query 3300C0. 332B60 and 333C20 evaluate spatial effects.
- 34F130 consumes a 25-word random stream and cursor at 485EB30/485EB94. This is distinct from the gameplay RNG at 485EB28. The failed expected cursor is 10; actual is 11, with the same 25 words.
- 34F3C0 evolves six words at 470E2B0 through four arithmetic rounds per call. Emulating its signed 32-bit recurrence shows that the actual failed state is exactly one call beyond the expected state.
- 331980 consumes this state while updating ambient vectors; 332400 and 333550 have their own draw counts and conditional secondary-generator calls. 334960 draws when an emitter countdown expires. These generators have callers outside this narrow effect path too; attributing the extra draw to a specific object is not yet supported.

This identifies a shared RNG consumption divergence, not the specific extra call or its cause. Existing main-RNG diagnostics do not observe these two helpers. No gameplay fix is claimed based solely on matching a later RNG value.

## Corrections made to the test

1. Removed short-circuiting of effect verification after a transform mismatch. The retained effect image and emitter comparison now run independently, preserving evidence that was previously skipped.
2. Added exact source/entry/expected/after observations of both full RNG states at each retained replay update. Before invocation, current shared state must match the archived source; a mismatch stops before that update and is recorded. No RNG value is changed by this diagnostic.
3. Added wind.rng_boundary to raw failure evidence and the evidence decoder. Unknown or malformed record shapes reject. Existing raw evidence, endpoint checks, ownership and round checks remain.

The observations separate a wrong starting state from extra consumption during the replayed update, while independent image comparison identifies the first affected effect object. They do not log every individual shared-RNG caller. If the objects and boundaries cannot distinguish the cause, ABI-preserving caller traces will still be needed.

## Validation

FullStateProbe regression coverage checks all 32 shared-state words individually, no-write rejection, matched/mismatched endpoints, missing records, wrong addresses, invalid cursor, and independent effect verification on a failed transform check. Decoder tests cover complete state extraction and malformed sizes. No expected endpoint is copied into live state and no new comparison mask is introduced.

Full Build-Native.ps1 and shared-state decoder tests passed. This is an investigation build; the extra caller and gameplay correction remain unproven.
