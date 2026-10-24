# Round-transition preparation

This document describes the original intent-only path, retained as fallback.
The newer exact-frame reservation path is documented in
[ROUND_TRANSITION_RESERVATION.md](ROUND_TRANSITION_RESERVATION.md).

The next round now acquires successor resource leases and exchanges round
intent during verified native Start state, after the previous session's
corrections, continuation, histories and routes have retired. Native clock and
Start updates continue normally: there is no transition clock hold, snapshot
capture, input injection or prediction.

The intent contains fresh session nonces, round/event identity, complementary
slots, tickets, build, delay proposal and the existing prediction limit. Its
first-frame field is zero and explicitly carries no frame agreement. Packet
kinds 7/8 are authenticated and acknowledged on the existing mod channel;
they cannot publish the activation mailbox. Intent objects have process
lifetime, a 60-second validity bound and bounded per-dispatch storage. Missing,
expired or unacknowledged intent uses the existing fighting-boundary handshake.

At the first safe fighting boundary the owner revalidates the native clock,
round/generation, actual battle frame, peer context and retained dependencies.
Only then are the new session and native hold installed. A matching acknowledged
intent supplies the peer ticket, allowing the first held message to replace
the initial ticket-discovery exchange. Both peers still validate the actual
first frame and full offers, bilateral holds, scheduled release, deadlines and
commit. Adaptive delay is proposed during preparation, agreed at activation,
and never changed mid-round. If prepared dependencies or identity change,
existing failure containment remains in force; they are never trusted blindly.

Ghidra evidence, pinned SC6 executable
------------------------------------
Focused audit: work/ghidra-sc6-focus/round-transition-preparation/.
Script: SC6RoundTransitionPreparationAudit.java (read-only project execution).
RVA 384040 updates both characters' startup input-lock fields, including 34B8;
RVA 3840E0 compares the Start state's elapsed/target values and queues the next
state. RVA 3F5D20 runs service, wait and advance virtual callbacks. RVA 3FC920
handles clock phases, including ring resets outside running phase 2. These
paths must continue during preparation. No native bytes/hooks were changed by
this optimization. Start is recognized by exact vtable 3E87378 and update
384040, with native playback and round-result flags clear. Normal 3E873D0 /
384EE0 remains required for binding, holding and activation.

Diagnostics and validation
--------------------------
Reports include continuous_transition_prepared/used/waits/failure,
round_preparation_<event>_agreed/received/rejected/seeded/failed, and
start_sync_round_<event>_transition_seeded. Existing resource, synchronization,
ownership, replay, frame timing and round-safety diagnostics remain enabled.
Transition failure codes: 1 identity changed, 2 bounded storage exhausted,
3 resource exception, 4 missing acquired source, 5 dependency revalidation.

Simulated 90 ms symmetric RTT with 9-second clock offset: retained-timing hold
275 ms; transition intent reduces it to 230 ms. Dropped intent, acknowledgment,
proposal and commit messages are covered. Tests also reject stale context,
changed intent, injected targets, direct intent activation, stale measurements,
wrong owner, stopped resources and changed dependencies. Native rollback-window
regressions cover snapshots, retirement, native boundaries and failure paths.
These are local tests, not a live latency guarantee. The remaining scheduled
release and fresh boundary validation are deliberately retained. Live multi-round
and rematch validation remains required; this does not eliminate all pauses.
