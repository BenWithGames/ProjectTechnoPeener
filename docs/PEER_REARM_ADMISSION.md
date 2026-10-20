# Peer-context rearm admission

Friend 15964 stopped with rearm failure 2 after settlement, with zero restoration/adapter failure. The old compound check does not identify its subcondition, so exact retrospective attribution is impossible.

Source audit found a timestamp sampled before the snapshot lock, allowing a concurrent publication to appear in the future, plus immediate terminal treatment of missing/stale transport evidence. Rearming now samples time after snapshot copy. Missing/future/stale evidence waits up to nine seconds (the existing peer-liveness interval), without acquiring resources or authorizing a new simulation frame. A valid snapshot with changed peer, local nonce, remote nonce or slot remains terminal; shared-session mismatch is always terminal. Freshness still requires age <=250ms. Native round/clock/fight/resource and bilateral admission checks are unchanged. No Ghidra change is needed for this mod-owned publication logic; existing native clock audit remains applicable.

Report: continuous_rearm_peer_reason 0 ready, 1 missing, 2 future, 3 stale, 4 Steam peer mismatch, 5 local nonce mismatch, 6 remote nonce mismatch, 7 slot mismatch, 8 shared nonce mismatch, 9 wait expired or clock moved backward. rearm_peer_read retains raw read classification on timeout. tick/now and waits accompany the reason. Outer failure remains 2 for compatibility.

Tests cover freshness boundary, old caller-clock race, recovery, exact timeout, backward clock, all identity mismatches despite stale data, and clear. The change is supported by code and regression tests; only a live report can confirm which condition occurred in the historical failure. Earlier sender optimization and all diagnostics, verification and round guards remain enabled.
