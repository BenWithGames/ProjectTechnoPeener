# Animation list traversal correction

Both reports for match 9de0510ddcf8dea5bc0b1173f2f07d90 reject SaveProbe phase 1 at the 64-node cap, before first simulation advancement. Their final observed nodes advance by 0xA0; the short diagnostic tail cannot prove the entire list acyclic.

Ghidra read-only audit on the pinned executable: native save RVA 30EC90 and load RVA 30EDC0 traverse heads at nested player object +400/+420/+440 via node+18 until null. Assembly 30ED3F/30ED6F/30ED9F and 30EE6F/30EE9F/30EECF confirms the next links; there is no 64-node bound. Save calls slot30, load slot38. This validates traversal semantics, not an unlimited safe capture size.

SaveProbe and FullStateProbe now share an allocation-free Brent cycle guard and a defensive 4096-node work budget per list. That budget is a mod safety limit, not a native capacity claim. Every existing writer and loader method whitelist remains checked; snapshot stream size/record limits, ownership, exact verification and round checks remain in force. Invalid memory is still handled by existing guarded capture. Rejection observation kind4 means budget exceeded; kind5 means a cycle, with address, visited count, budget and list offset. No extra next-pointer reads are introduced.

Regression coverage accepts 64,65,128,768,4096 finite nodes, rejects node4097, and rejects self/multi-node cycles across 6320 prefix/length combinations. Full native build includes the regression runner. Live two-PC verification remains pending. Adaptive 0-2 delay and 3-frame prediction unchanged.

Evidence: work/ghidra-sc6-focus/animation-list-traversal/{30ec90.c,30edc0.c,references.txt}; headless run readOnly/noanalysis. No game binary modifications.
