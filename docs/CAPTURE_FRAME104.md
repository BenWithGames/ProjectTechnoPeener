# Frame 104 capture failure (2026-10-19)

Ben 54476 completed 104 live updates and three corrections (nine replayed frames), then adapter failure 2 / capture. Friend 6272 completed 108 live updates and nine corrections (26 replayed frames), with no local failure, and received the stop packet. Both admitted two fight-ready startup pairs.

The stored restore diagnostic belongs to the previous restoration. It cannot identify the subsequent capture failure. No failed snapshot payload or capture substage was exported by that build. Therefore the native rejection is not established; no acceptance guard is relaxed and no root-cause fix is claimed.

The new continuous_capture_schema=1 fields are owner-thread history evidence copied into report atomics before teardown. They include attempted frame, oldest required frame, retained count, native capture stage/exception/fault/validity, observed frame, and saved result. Cleanup preserves them. Reports never read the history from the worker thread.

History stage codes: 0 success; 1 initial admission; 2 effect owner; 3 effect begin; 4 descriptor read; 5 dependency seed; 6 fingerprint; 7 pool visit; 8 effect capture; 9 native capture or frame match (native_stage/fault/observed_frame distinguish); 10 effect compare; 11 descriptor reread; 12 reference bind; 13 retained guard; 14 descriptor changed; 15 reference coverage; 16 fingerprint changed; 17 generated buffer bind; 21 replay sequence; 22 frame sequence; 23 eviction protection; 24 pool thread; 25 retirement; 26 birth compaction; 27 acquire.

Native stages retain the existing NativeBoundarySnapshot mapping, including 11 blend, 12 camera, 16 selector, 13 final keys, and 15 complete. This build fixes missing evidence, not the unproven native capture cause. Ghidra investigation should target the failing native stage once identified rather than guess a writer from the old restore report.
