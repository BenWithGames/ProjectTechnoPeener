# Retained replay scalar reset

The pinned SC6 executable (SHA256 F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553) writes 0x3F800000 at animation-wrapper +0x54 in initializer RVA 0x2F90AD. This is player +0x9723C.

Partner evidence steam-inputs-30696 showed saved 0x3F333333 and loaded 0x3F800000. Those bytes were outside the preservation allowance. The previous combined check did not identify its first failing condition; this evidence identifies a definite preservation rejection, not proof of wall-break causation.

Preservation now accepts an unchanged value or that exact native reset, checks live bits still equal the loaded observation, and restores the exact saved four bytes. Identity, RNG, surrounding-byte and final restored-state checks remain. No guessed numeric range is imposed on saved bits.

The suite and binary evidence retain postload_failure_check (1 loader completion, 2 selected equality, 3 postflight, 4 preservation, 5 ring restoration) and both players' reset54 saved/loaded/live/reason/restored fields. Reset reason 1 is unexpected loaded bits; 2 is a live change after observation.

Local tests cover finite values, signed zero and NaN bits; unexpected loaded values, live changes and adjacent-byte changes reject before preservation writes. Live retained-object replay still needs verification on both PCs.
