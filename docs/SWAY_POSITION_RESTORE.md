# Audited sway position restoration

Match 772c972d4a6904b9f112694837a32fe8: remote completed 14 corrections then rejected a depth-two restore 302 -> 300. Both sway and constraint validators reported kind2 record606 offset10 saved byte98/newerE0, preflight mask157 (sway/constraint bits absent). No native load was attempted for this failed restore. The report lacks the node vtable and full vector, so it cannot establish whether this exact record belongs to the audited types below. This is an evidence-scoped fix, not proof the reported instance is resolved.

Read-only Ghidra audits: sway-position-audit and node-position-writer-audit. Native constructor 33A3A0 writes descriptor floats to node+10/+14/+18 at 33A40C/33A412/33A418; +1C is initialized to 1.0. It installs vtable3E88DB8. Variant3E88DF8 shares this base position layout; 33B4B0 calls base update334D10 before its separate scaling. 33A120 and339E20 consume XYZ as position and transform inputs. These bytes are value state, not topology links. This does not prove the specific runtime writer in the failed match.

Both comparison paths permit XYZ changes only for exact kind2,size80,side0/1,vtable3E88DB8 or3E88DF8. restore_sway copies the saved12 bytes after all-node validation, then retains full exact header readback. W at1C, vtable, owner/link pointers, all other unapproved bytes, same-topology checks and live-memory equality stay guarded. Unknown types remain rejected. Existing 7C/7D branch restoration stays intact. No arithmetic, tolerance or inferred position is substituted.

New diagnostics append raw node_vtable and saved/next64-bit words at10 for both rejection paths. This identifies a different type if record606 falls outside the audited pair; old diagnostic fields remain available.

Tests: both types, XYZ changes, no-write validation, exact full-record readback, live mutation rejection, unknown type rejection, existing contact/branch/link-distance cases, bounded transaction tests. Adaptive delay0-2, prediction limit3, ownership and round safety unchanged. Live validation pending.
