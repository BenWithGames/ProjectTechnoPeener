# Inline RNG and replay seed diagnostics

The retained-object reference replay suite now requires the new profile-12 signature on both peers.

## Coverage

Eight pinned stores to module+0x485EB28 are observed: 2F9F1E, 2FA159, 33154E, 331927, 33220D, 3330A4, 3346B3, and 34F867. The last is a seed-initialization store, not a random-number draw. Existing 34F550 helper tracing remains enabled.

Each event includes the site, thread, native frame, phase, current seed, native store operand, and all 16 general registers. Registers are raw context, not validated object pointers. Events occur immediately BEFORE the original store; the reported `after` is its intended source-register value. Native trampolines execute the original store unchanged. This is not a hardware watchpoint and does not claim coverage of arbitrary indirect writes.

Before every retained-object reference replay hop, the live seed must equal the immutable source snapshot seed. Mismatch or unreadable seed records a site-8 event and stops before advancing that hop. Site 9 records the seed after the update against the expected endpoint. Existing endpoint validation still determines success; no RNG correction is applied by these diagnostics.

## Evidence

Reports include installed/event/drop/invalid counters and pre/post seed-check counts. `*-inline-rng.bin` is captured on retirement and collected with the existing report ZIP. Failure evidence also embeds `rng.inline`. Decode either with `python -B tools/analyze_replay_evidence.py PATH --output OUTPUT.json`.

The bounded ring holds 8,192 events and reports overwritten/dropped events. Observation uses a nonblocking lock, performs no allocations in native hook callbacks, and preserves LastError. All wrappers preserve flags, GPRs, and legacy FP/SSE state. Interior stack alignment is 16 bytes at the pinned sites.

## Validation

Test-InlineRng.ps1 checks all eight wrappers against an unhooked machine-state baseline under a clobbering callback, native source-register selection, pre-replay mismatch rejection, phase tagging, invalid reads, ring overflow/drop reporting, and real MinHook RIP-relative store relocation. Python tests verify evidence decoding and malformed-size rejection. Build-Native invokes these alongside the existing suite.

This adds diagnostics and a pre-replay guard, not a repair for the extra RNG consumption. Retained replay remains bounded, with the existing warmup/private-match procedure and restart requirement after unsafe failures.
