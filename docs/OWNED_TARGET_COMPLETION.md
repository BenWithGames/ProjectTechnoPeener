# Owned-target restoration completion — 2026-10-19

Both reports (Ben 67252 and friend 1156) stopped at ImmediateIdleCorrection line 43, section 2, record 793, kind 18, on the first three-frame correction. The completion path required a kind-16 contact-pointer parent, but fixed-slot snapshots now use a kind-26 independently owned target parent. No correction completed.

The fix recognizes kind 26 only in the fixed-slot layout. It validates the same-side root owner, exact KSwayHitNode type, all five child record kinds/sizes/addresses/parents, and the exact matrix record position. The common path still checks live target type, saved/next identity, live identity, finite floats, and word-by-word saved-or-next provenance. Only the audited 64-byte matrices are patched. Legacy contact-pointer snapshots retain their original checks.

No raw diagnostics, endpoint verification, ownership lifecycle, round guard, prediction scheduling, or performance optimization is removed. All patches are collected and validated before any write. NativeAdjacentRestore still performs final capture and exact endpoint comparison.

Regression coverage: existing 39 legacy completion scenarios plus 12 owned-layout scenarios covering both matrices, mixed saved/next values, wrong side/root/child/type/layout, stale live type/link, unknown matrix value, NaN, and no partial writes. Focused FullRestore tests passed. Live validation still required; the prior incomplete 5.2 ms restores are not completed correction benchmarks.
