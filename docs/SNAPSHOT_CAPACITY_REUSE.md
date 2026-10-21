# Snapshot capacity reuse

The reset and recorded-copy paths retain unused selector-table and native-stream
payload capacity, in addition to the existing unused transform-record capacity.
All control fields, function tables, callbacks, diagnostic sentinels, stream
lengths/traces, ring snapshots and ownership checks remain unchanged.

Reset copies 3,340,680 bytes instead of 6,093,192 (45.2% less). Recorded copies
copy the full published selector table (`Entry.bytes`) and stream payload
(`write_position`), rejecting oversized ranges before any destination mutation.
Transform counts are also checked before mutation. Exact verification still
compares the entire published state. Native reference cleanup still precedes reuse.

Evidence: the focused Ghidra performance audit of RVAs 30AE80, 384540, 30A980
and 30B6E0 shows native loading performs reference resolution, preserves live
player fields and selects ring storage. Those operations are not bypassed.
The selector capture/restore code gates table reads by the captured length;
stream writes fill their prefix and native reads are bounded by write_position.
Consequently only unused backing capacity is omitted, not native state.

Raw failure dumps retain their full backing arrays and metadata. The added
`snapshot.capacity_policy=1` section explicitly marks that bytes beyond published
lengths/counts may be stale from a prior use. They are not current snapshot data.
This also applies to failed/invalid snapshots: validity and length metadata must
be considered when interpreting a raw dump. No raw failure sections are removed.

Regression coverage poisons reused capacity, compares all remaining bytes against
fresh defaults, checks exact published prefixes and metadata, rejects oversized
selector/stream/transform lengths without partial mutation, and retains existing
ownership, stale-handle, thread and quarantine tests.

A local optimized warm-cache benchmark measured about 51 us/reset versus the
prior 114 us/reset. This is not a whole-correction benchmark or a prediction of
the friend's machine. Live timing categories remain enabled to measure the result.
Input sampling, prediction depth, native waits, round guards, restoration and
endpoint verification are unchanged.
