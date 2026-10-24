# Bilateral match completion

Evidence: match 0c9c8ac7ed0c605915140a7c7abb43c1. The peer reported
continuous_match_complete with verified local cleanup. The local report recorded
650 completed corrections across four rounds, no rollback failures, no native
terminal observation, and continuous_peer_silent_restart_required. Peer retirement
previously happened without a match-completion exchange.

The preparation callback now checks terminal state on any clock callback from the
verified game owner thread, rather than relying solely on rearming the old combat
clock. It requires the current session, retired routes and history, committed native
continuation, actual updates, inactive combat, and no stop/restart/failure. The existing
pinned End-state and ownership checks still authorize cleanup; GameSet and ordinary
round changes do not. Failure or exceptions request stop. Terminal observation and
other-clock poll counters are preserved in reports.

After verified local completion, control packet kind 13 advertises clean completion
and acknowledges the peer's completion. Nonces, sender, signature, size, reserved
fields and packet shape are checked. A received packet never authorizes local native
cleanup. Both local cleanup and peer completion/acknowledgement must be present,
and a local acknowledgement must have been successfully queued before reporting
success. Completion messages use Steam reliable delivery, retry at 250 ms, and have
a 30-second completion deadline. Existing disconnect, peer-silence, and failure-stop
handling remain active. This waiting happens after combat, not on the game thread.

The match protocol signature changes, so both players must install this build.
The completion exchange resets with the existing completed-session retirement.
Signed updater, adaptive delay, correction pipeline and verification are unchanged.

Regression coverage in SteamHandshakeTest includes one-sided completion, malformed
and stale packets, failed sends, retries, acknowledgement gating, duplicate messages,
timeout, and rematch nonce rejection. Existing native resource tests cover terminal
vs round-end state, owner thread, settlement, cancellation and failed cleanup.
Two-PC match-end/rematch validation remains required.
