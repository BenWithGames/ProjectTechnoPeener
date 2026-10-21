# Guarded direct target restoration

The live history restorer now tries one native load from the current boundary directly to the requested target for two- or three-frame corrections. One-frame corrections retain their existing path. Replay still advances through every required frame and retains endpoint verification.

Every intermediate snapshot must first pass the existing round identity, ownership, structural, input, timer and blend checks. The direct candidate additionally passes target-to-current preparation. Unsupported candidates fall back to adjacent loads only before any game writes; a failure after mutation stops the session.

## Binary evidence

Audited against pinned SoulcaliburVI.exe SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553 using Ghidra:

- Player loader RVA 30AE80 preserves live player words at 244, 248 and 324 while loading the serialized blocks. Direct admission requires these words to match in target and current archives, identified through writer RVA 30A9CE and the 3510-byte player block. Missing, duplicate or malformed records reject direct admission.
- Ring initialization RVAs 30B7D0 and 30B6E0 establish three aligned slots. Span validation composes the existing per-frame rotation, including the previous-slot relationship.
- Camera history advance RVA 31B1C0 increments the count with signed-positive saturation and wraps the index modulo 16. Direct validation composes this operation for the bounded span; restoration still writes the saved state.

Local evidence: work/ghidra-sc6-focus/direct-restore-audit and direct-restore-counters in the workspace.

## Diagnostics and limits

Added direct_attempted, direct_used, direct_rejection, direct_guard_reason, direct_guard_offset and native_load_attempts to restoration details, plus restore_direct timing. Existing raw evidence, retained-object checks, exact verification and round safety remain.

This reduces native loads when admitted; it does not promise a proportional improvement in total correction time. All-hop validation and forward replay remain. Live performance and coverage require another match. The previously observed attack blend-table transition rejection is not claimed fixed by this performance change.

Regression coverage includes depth-two/depth-three direct routing, pre-load fallback, no fallback after failed loading, all-hop rejection, cross-round rejection, preserved-field mismatches, malformed archives, ring indices, camera counter saturation and index wrap. The full native suite also retains one-frame restoration coverage.
