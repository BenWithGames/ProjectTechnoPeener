# Live correction workspace reuse

The partner's 42960 report measured 30 first-round restorations averaging 70.260 ms. Workspace allocation/construction averaged 55.7591 ms. Replay updates averaged 0.281 ms. These are overlapping inclusive timings, not additive phases.

NativeSchedulerStorage previously constructed a NativeHistoryRestore on every correction. That constructor creates three NativeAdjacentRestore objects, each with two 37,447,504-byte FullStateProbe buffers. The new storage retains these six buffers for the session and allocates them before activation. This trades approximately 214 MiB of persistent scratch per session for avoiding correction-time allocation and initialization.

NativeHistoryRestore resets only successful, fully verified load transactions before reuse. Every used adjacent workspace must pass its fault, ownership, load-completion, camera, stream and payload guards before any reset occurs. Failed workspaces remain one-shot. The history owns retained dependencies; scratch with an effect binding cannot be recycled. Existing replay completion checks and round/session identity checks remain unchanged.

This API is separate from fixture recycle_after_replay: the history loader previously destroyed its scratch at the end of restore, before scheduler replay. Reusing that detached scratch does not authorize skipping or accepting replay. Diagnostics are copied before reset. Constructors rebuild self-referencing headers and patch lists; FullStateProbe's existing reset restores defaults while skipping only unpublished transform-record capacity.

Ghidra was not needed for this allocation change: it modifies our scratch storage lifetime, not native serializer behavior, field interpretation or ownership. All audited native restoration paths remain unchanged.

Regression coverage exercises repeated scheduler corrections without reconstructing Restore, pointer retention, default-state reset, rejection of incomplete loads, faults and retained bindings, and preservation of failed-attempt semantics. The full build also runs existing ownership, replay, round and camera checks.

A separate optimized synthetic benchmark on Ben's PC measured allocation averaging 24.2252 ms (five attempts) versus resetting three hops averaging 0.513563 ms (30 attempts). It uses synthetic completed-load flags, not native game replay; it establishes buffer-management cost only and does not predict the partner's exact improvement. Source/output are work/WorkspaceReuseBenchmark.cpp and .log in the workspace.

Reports retain the workspace timing bucket but now identify continuous_workspace_reuse=1 and continuous_workspace_timing_is_reset=1. That bucket measures reset rather than fresh allocation. Initial allocation occurs before activation and is outside correction timings. All existing diagnostics, exact verification, round guards and the camera-readiness fix remain included.
