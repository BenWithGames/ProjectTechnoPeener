# Real input observation (no gameplay rollback)

This is a separate opt-in mode. Network-Test.cmd remains the synthetic test.
Observe-Inputs.cmd exchanges real 40-byte input envelopes copied at the pinned
SC6 update boundary. No received input is applied to the game, and no game state
is restored. It does not replace SC6 networking or enable online rollback.

## Setup on both PCs

1. Close SC6. Extract the new complete ZIP to a separate folder.
2. Run Install-Observer.cmd and provide the game's Steam installation folder.
   The installer checks the supported game hash and loader, backs up replaced
   settings/mod DLL, preserves other mod-list entries, and verifies the new DLL.
   An incompatible existing loader stops installation instead of being replaced.
3. Launch SC6 through Steam. For the first smoke test use offline Training on
   both PCs. Later, use only a private match where both players consent.
4. Run Observe-Inputs.cmd on both PCs. Confirm observation, then use H/J and the
   shared session code as before. On your current LAN the join address is the
   host's local IPv4. Restart SC6 before each new observation attempt.
5. At the final prompt, start together, switch back to SC6, and leave Training
   unpaused. The observer records 120 frame boundaries (normally about two
   seconds). First confirm an idle capture works before designing input patterns.
6. Send both new log folders, including the copied local/remote CSV files.

The first real-game capture has not yet been validated. Compilation, mock IPC,
network retransmission and the existing native test suite are separate evidence.

## Interpretation and limits

- PASS observation_transport_only means each side received all of the other's
  records exactly, confirmed by crossed checksums. Their inputs are expected to
  differ. This is not a deterministic state comparison.
- Each record contains the local outer counter, all ten raw input words, and
  local QPC timestamp. QPC clocks are not synchronized between machines.
- Sample zero is each PC's first boundary after arming, not an agreed game frame.
  Outer-counter gaps are reported. They do not silently pass as alignment proof.
- Protocol roles 0/1 are transport roles, not verified P1/P2 assignments. The
  recorded envelopes contain both player slots; mapping still needs analysis.
- Match mode is user-confirmed, not automatically detected. Do not use ranked,
  casual matchmaking, or private games with an unsuspecting opponent.
- The game hash and hook prologue are verified before recording. The callback
  uses bounded memory copies; sockets and CSV writes run in a separate process.
  It exits before research restore/injection paths. A read failure, thread change,
  or 20-second capture timeout stops observation. One observation per game process.
- Observation compatibility uses its own simulation ID, so synthetic clients
  cannot join it. The mapping is read-only to the sender and contains no pointers.
- This is still an experimental hook. Loader-only online compatibility and a
  game-enforced private-session gate remain unverified.
