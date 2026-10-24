# Sway subtype 0x3E88D78 restoration

## Evidence (2026-10-22)

Pinned executable SHA-256: F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553.
Latest local failure: match-00d51f0ab48f4a42bb6af58da2f0cde0, restore target 368/live 370,
record 606, kind 2, side 0, position offset 0x11. The actual vtable was 0x3E88D78.
The prior position fix covered only 0x3E88DB8 and 0x3E88DF8.

Read-only Ghidra artifacts: work/ghidra-sc6-focus/sway-d78-audit,
sway-d78-dependencies and sway-d78-callers. A raw .text relative-call scan located
constructor call 0x34A49C inside 0x34A100; existing Ghidra xrefs missed this call.

- 0x33AB60 calls the shared base constructor 0x33A3A0, sets D78's vtable and
  stores owner+0x2C and owner+0x38 references at +0x2B0/+0x2B8.
- 0x33AC80 advances +0x90..0x9F using +0x80 history, +0xA0 velocity, and
  the owner mode control. 0x33AF60 updates history and velocity and, on its
  parent branch, calls 0x2DE6D0 with destination node+0x10. This directly
  explains the rejected XYZ changes; XYZ is not an identity pointer.
- 0x33AF60 calls shared constraint solver 0x338870. Base construction and
  shared virtual 0x334DA0 use the same bounded link/contact layout.
- +0x2A0..0x2AF contains four solver clamp scalars. The next 16 bytes are
  control references. They are not disposable padding.
- Shared scale setter 0x334CA0 changes XYZ and +0xD4. Scale/configuration
  changes remain guarded; this patch does not broadly admit new transitions.

## Change

Capture D78's base motion payload and bounded link/contact slots, and a new
kind-28, 32-byte suffix at +0x2A0. Bind its control references to its already
captured owner; require any non-null parent at +0x50 to be a captured supported
sway node on the same side. No new arbitrary external memory writes or pointer
resurrection. Reject changed controls/configuration or missing dependencies.

Restore audited XYZ, branch flags, existing base motion history/velocity, and
bounded constraint state. Preserve pointer/topology checks, current-live checks,
validation before writes, exact post-write comparison and endpoint verification.
The intermediate refresh path also checks the new references and dependencies.
Adaptive delay, prediction limit, scheduling, diagnostics, and round safety are
unchanged.

## Validation

ConstraintSlotsTest exercises D78 on both sides with a non-null captured parent,
all 25 contact-count transitions, differing XYZ/history, exact restored records,
read-only preflight, intermediate refresh, changed clamps/controls, rebound
references and missing parents. The latter reject before restoration.
Full release suite and DLL build are required before delivery. Live validation
of this subtype remains pending; local tests do not prove an entire match.

## Follow-up

The immutable-control assumption above was superseded by SWAY_CONTROL_RESTORE.md.
The references and configuration stay guarded, but owner mode and blend are mutable
state and are restored from the snapshot.
