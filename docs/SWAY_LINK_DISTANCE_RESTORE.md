# Sway-link distance restoration

Ghidra audit of the pinned SC6 executable: RVA 33A1E0 writes node+E8+j*10 from the distance between linked nodes (MOVSS at 33A25D). RVA 338870 reads that scalar during constraint solving. RVA 33A990 rebases the preceding pointer. Therefore kind14's first eight bytes remain topology, but its final four bytes are mutable simulation state.

The fixed-slot restore path now validates every record before writing, preserves stable target pointers and node types, restores the saved distance exactly, and verifies the full link record afterward. Existing contact dependency validation, live-versus-captured checks, endpoint verification, diagnostics and round guards remain intact. Legacy capture mode remains conservative.

Regression coverage exercises all four link counts, changed distances, dry runs, live mutation rejection without partial restoration, and changed target rejection, alongside the 25 contact-count transitions.

Both latest reports rejected constraint preflight during Reversal Edge (mask 189). They do not contain binary snapshots identifying the exact differing link. This fixes a binary-audited omission, not a proven identification of the historical byte. Live confirmation remains required. Scheduler-arrival diagnostics remain included to investigate slow motion separately.
