# Private rollback and original matchmaking

Rollback is reserved for private sessions where both players explicitly opt in.
Ranked, casual matchmaking, offline, and unidentified modes retain the original
game behavior. Installing the mod alone never enables rollback.

`SessionPolicy.hpp` implements the pre-match decision only. The DLL does not
yet call it: no mode detection, handshake, or network hooks exist yet.

| Context | Decision |
| --- | --- |
| Ranked, casual, offline, or unknown | Original networking |
| Private without local rollback opt-in | Original networking |
| Private rollback requested, adapter unverified | Reject rollback session |
| Private rollback requested, handshake pending | Wait before starting |
| Both opt in, verified adapter, matching compatibility | Rollback |
| Missing peer, timeout, rejected consent, or mismatch | Reject rollback session |

For a rejected private rollback session, users can create a new normal private
match. Do not silently downgrade an explicitly requested rollback session.

## Integration requirements

- Detect the actual match mode before changing inputs, frame timing, state,
  or networking. Unknown mode means original behavior. UI labels alone are
  insufficient evidence; the relevant game state must be verified.
- Use a separate opt-in private-session handshake. Do not put discovery or
  custom packets into ranked traffic or require unmodded opponents to reply.
- Verify protocol, snapshot schema, executable build, and simulation build
  identifiers. Include gameplay-affecting configuration/content in the latter.
- Bind consent and readiness to the current authenticated peer and fresh session
  identifier. Both sides must acknowledge the same settings and start boundary.
  An advertised mod version alone is not a completed handshake.
- Bound the handshake with a timeout, represented as Unavailable by the caller.
- Latch the chosen mode for the match. Losing the peer or detecting a desync in
  rollback must end/abort that session; switching to original networking halfway
  through a match is not an implemented recovery mechanism.
- Clear all session and peer state on exit. The next match begins with original
  behavior. Prevent stale private-session state from carrying into ranked.
- Gate every mutating hook behind the active rollback session. Normal modes
  must preserve original calls and timing, with research controls disabled.

The fallback here means selecting the original networking before a match.
It is not a claim that ranked compatibility is already proven or that any
loader is accepted by the game's online services. Validate loader-only and
disabled-mod behavior before claiming support; no concealment or bypass is part
of this design.

## Validation still needed in SC6

Test clean game versus loaded-but-inactive mod; unknown mode handling; private
to ranked transitions; rematches; peer changes; mismatched builds; handshake
timeouts; and rollback disconnects. Confirm that original modes produce no
custom packets, injected inputs, restored state, or simulation scheduling changes.
The current unit test verifies policy decisions only.
