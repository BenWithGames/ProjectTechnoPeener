# Fresh local input delivery

## Evidence
Pinned SC6 image SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553. Fresh read-only Ghidra output: work/ghidra-sc6-focus/input-buffer-audit.

3FDB80 calls vtable +638 (3FDF30) to sample the local raw words into clock+3B8/3BC. It then calls +6C0 (3F8410), which in active phase 2 publishes into the native ring while local_cursor <= current_frame + lead. The +650 callback (3F6070) can transform the sampled words through registered native input processors before publication; this path uses the final post-service words, preserving those transformations. 3FE8F0 and 3FEBA0 advance the tagged local and all-player cursors respectively (input-cursor-audit). 3F2B60 stores the raw word under local_cursor, preserving already-valid tagged entries. 3F8390 adjusts lead toward a clamped value derived from 4408 and 43EC. The native input filter previously selected current_frame from that future-populated ring. Thus early network publication alone retained native local buffering.

## Change
Prediction-enabled sessions read the freshly sampled word after native service and commit it to the next mod simulation-frame index. Native cursors, queues, network packets and confirmations are not rewritten. The sender, native filter override, previous-input edge formation, confirmation reconciliation and replay all use the same route-owned frame. Once sent, that word is immutable across admission stalls; later controller samples cannot silently change it. Freshness reduces scheduled buffering, not waits caused by exhausted prediction capacity.

The filter requires the committed outgoing entry and rejects missing publication instead of silently falling back to delayed input. Existing native identity/boundary checks remain. Baseline mode continues using native delayed local inputs and native confirmed remote inputs. The enabled handshake signature changes to reject old enabled peers; mixed modes already reject. New round sessions establish new histories under existing round guards. No increase to the three-frame prediction budget.

## Measurement
input_latency_fresh_local=1 identifies fresh-sample timing. It brackets the service that sampled the word and ends at native input-formation completion. Baseline reports fresh_local=0 and retains the existing native-ring measurement. These are not physical button-to-screen measurements. Service waits remain measured, and immutable first publication includes any subsequent admission stall. No claimed latency gain until A/B validation.

## Tests and remaining issue
Tests cover both local slots, delayed cursor unchanged, read rejected outside a serviced boundary, outgoing/filter/replay equality despite differing native delayed words, absent publication rejection, and round identity rejection. Existing baseline behavior is covered too.

The prior animation-blend pointer transition at frames 5060->5061 remains unresolved; this latency change does not claim to fix it. All exact restoration and round safety checks remain enabled.
