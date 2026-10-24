# Preparing rollback before fighting

The previous automatic path waited for Normal/fight-ready side detection, two
completed fighting frame pairs, and a future target at least 60 frames beyond
the first pair. Installing hooks after those prerequisites could miss that
target and move it another 60 frames later.

This build installs the inactive, byte-verified hooks immediately after owned
transport attachment. Native updates remain unchanged until activation. Side
discovery also accepts the verified Start mode as preparation evidence only:
vtable RVA `3E87378`, update RVA `384040`, owner-thread observation, valid native
clock/slot layout, matching round generation, no native playback or result.
Existing Ghidra output in `prediction-round-admission/00384040_Origin_384040.c`
shows that Start controls startup input; it is not Normal combat permission.

When resources and side agreement are ready in that state, peers exchange a
round-scoped first-fight intent. No future clock frame is guessed. The queued
owner-thread callback lets native intro updates continue and waits for the
first freshly verified Normal fighting boundary. It assembles the configuration
again using the live native clock, battle counter, round/event/generation and
slot. The ordinary exact bilateral held-frame agreement, adaptive delay
agreement, and synchronized release remain required before prediction starts.

If one peer could not prepare in intros, its ordinary paired-target proposal
makes the early peer fall back before queuing. Delayed intro intent cannot
overwrite a fallback target. A round change, stale identity, failed fresh
assembly or conflicting post-queue fallback remains a rejection, not permission
to restore across an uncertain boundary. The automatic compatibility version
and continuous transport signatures changed; both players must update.

This targets the first safe fighting frame when both PCs prepare in time. It
does not guarantee that skipped intros, late connections or slow resource
preparation will finish in time. Nor does it eliminate the short bilateral
hold needed to confirm the exact boundary and synchronize release. The
existing late-start fallback is still reported explicitly.

New report fields:

- `continuous_startup_first_fight`: early intent path selected.
- `continuous_startup_minimum_pairs` / `target_lead_frames`: zero for the early
  path; two / 60 for the scheduled fallback.
- `continuous_intro_waits`: owner callbacks that correctly left non-fighting
  states native-owned.
- `continuous_first_safe_clock` / `continuous_first_safe_battle`: actual live
  boundary selected, rather than the zero placeholders in the intro intent.
- `continuous_fresh_assembly_rejection`: exact configuration result.
- Existing synchronized-start planned/actual release timings remain available.

Dispatch rejection 10 means changed intro round identity, 11 unreadable battle
counter, 12 failed fresh configuration assembly, and 13 unreadable readiness.

Regression coverage includes intro-only side selection, wrong thread/round/
handler, playback/result exclusion, two-peer intent and fallback negotiation,
malformed intent, fresh-frame assembly after multiple intro updates, changed
round/slot rejection, and the existing synchronized-release tests. Real two-PC
first-match, skipped-intro and rematch validation remains necessary.
