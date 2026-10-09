# Serialization candidates

Static inspection, 2026-10-07. All addresses are RVAs for executable SHA-256
`F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553`.
No routines were invoked in the game. This is a partial state map, not a complete
rollback snapshot or a verified calling interface.

## Paired routines

| State | Writer candidate | Reader candidate |
| --- | --- | --- |
| Shared globals | `31DC00` | `31DE50` |
| Player object | `30A980` | `30AE80` |
| Nested player state | `30EC90` | `30EDC0` (call target; body not yet inspected) |

The first two pairs process identical blocks in identical order. Writers use
stream vtable slot `+28`; readers use `+30`. Readers reconstruct pointers and
write destination state, providing strong evidence for these direction labels.
Concrete stream implementations, owners, callers, and lifecycle remain unknown.
Player routines receive object in RCX, stream in RDX, and range table in R8.

## Shared blocks

| Global RVA | Byte count |
| --- | --- |
| `48462D0` | `58` |
| `4846330` | `140` |
| `48545A8` | `50` |
| `48545F8` | `EF0` |
| `48545A0` | `8` |

Sizes and offsets in this document are hexadecimal. These blocks include the
observed speed gate, scales, countdowns, sources, selector, and side modes.
Four pointers follow separately: `4846470`, `4846478`, `4846480`, `4846488`.

## Player blocks, in stream order

| Object-relative location | Byte count | Access |
| --- | --- | --- |
| `10` | `80` | Direct |
| `90` | `3510` | Direct; includes observed pending speed fields |
| `35A0` | `1840` | Buffer returned by subobject vtable `+28`, argument zero |
| `27760` | `800` | Buffer returned by subobject vtable `+28`, argument zero |
| `2B3E0` | `50` | Direct |
| `43D80` | `50` | Direct |
| `43DF0` | `30` | Direct |
| `444F0` | `D38` | Direct |
| `45230` | `370` | Direct |
| `95FA0` | `11F0` | Direct |
| `971A8` | `14` | Direct |
| `971E8` | `60` | Direct |

Next comes nested serialization at object `+44078`, then nine relocated pointers
in this order: `44040`, `44058`, `44060`, `97190`, `97198`, `44048`, `44050`,
`971A0`, `44068`.

The nested writer emits relative blocks `0/200`, `200/200`, `45C/14`, `470/4`
(offset/size), then four bytes from global `470DEC4`. It traverses three linked
lists headed at nested offsets `400`, `420`, and `440`, following node `+18`
and dispatching each node's vtable `+30`. This node method is not the stream
method despite sharing a slot number. Payload size therefore depends on the
object graph; the fixed blocks alone do not establish snapshot size.

## Relocation format

The range table has begin/end pointers at `0/8`, with 16-byte entries containing
a 64-bit base and length. The writer searches for a pointer within a range,
accepting displacement equal to length, and emits a 16-byte record containing
a 32-bit range index at `0` and 64-bit displacement at `8`.
On no match it emits index zero and the original pointer. Bytes `4..7` are not
visibly initialized in these paths; do not assume deterministic serialized padding.

The reader indexes the range table and adds its base when nonzero; otherwise
it retains the stored value. Range-zero sentinel setup and table bounds must be
verified at the caller before using this format. Raw pointers can remain in the
stream, so portability across processes is not established.

## Reader work beyond byte restoration

Player reader `30AE80..30B54A`:

- Preserves pre-load fields `244`, `248`, and `324` across the block reads.
- Calls `349290` while preserving selected freshly loaded fields around
  `1FF8..207C`, then restores those fields.
- Reconfigures subobject `971E8`, including pointer to object `29120`, and calls
  `2F8FA0`.
- Calls nested reader `30EDC0`.
- Rebuilds self/internal pointers at `43DF0`, `445C8`, `445D0`, `44A30`, and
  `44A38`, plus conditional pointers at `96F60` and `96F68`.
- Calls vtable `+C0` on the global object referenced by `470D188`, unconditionally
  once and conditionally during a three-element loop involving fields `270..278`.
  The purpose and external effects of these calls are not yet established.

These operations rule out treating the candidate reader as a simple memcpy.
They also leave open whether this path is suitable for per-frame rollback.

## Follow-up: callers and stream implementation

The following findings supersede the initial unknowns about callers, stream
implementation, and range-zero initialization above.

Confirmed direct calls link writer `3841E0` and reader `384540` to both player
routines and the shared pair. Both take the stream in RCX and construct three
16-byte range entries: null base/zero length, base from pointer slot `470DE90`,
and base from `470DE98`. Writer player lengths are `973F0`; reader lengths are
zero because reconstruction uses only bases. These slots differ from recorder
slots `470F1A0/470F1A8`; their runtime relationship is not yet verified.

After players, the writer processes global `470E2C8` (size `C`), shared state,
global `470D200` (size `BF0`), an optional `360` bytes through pointer `470DEE8`,
and routines `321750`, `391CA0`, `390690`, and `301820`. Some targets depend on
byte `470DEDC`. Reader counterparts are `321A30`, `391E10`, `390800`, and
`301B10`. The reader also calls `309ED0` for each ordered player pair.
These nested paths remain to be inspected; broader coverage is not completeness.

The writer mutates global `470E870`: it clears it, then adds a difference between
stream write positions. Its purpose is unknown. It also allocates/frees temporary
range storage. Invoking this save routine is therefore not read-only.

Nested reader `30EDC0` is now inspected: it mirrors the nested fixed blocks,
restores global `470DEC4`, and traverses the three existing lists using node
vtable slot `38` (versus writer node slot `30`).

### Fixed-capacity stream

Constructor `2FC1F0` installs vtable `3E87118` and clears both cursors. Owner
constructor `2FA5F0` builds four streams at `+A8`; `37D280` builds five at
`+488`, with stride `28018`. Caller `37E900` passes owner `+488` to the writer.
Another caller, `395808`, is an unwind fragment, not a standalone function entry.

| Stream offset | Meaning |
| --- | --- |
| `0` | Vtable pointer |
| `8` | Write cursor |
| `10` | Read cursor |
| `18` | Inline payload, capacity `28000` (163,840 bytes / 160 KiB) |

| Vtable slot | Routine | Behavior |
| --- | --- | --- |
| `0` | `2FC260` | Destructor/deletion path |
| `8` | `2FC1E0` | Returns capacity |
| `10` | `2FC0F0` | Resets both cursors |
| `18` | `2FC0E0` | Sets write cursor from RDX |
| `20` | `2FC0D0` | Sets read cursor from RDX |
| `28` | `2FC190` | Copies source into payload; advances write cursor |
| `30` | `2FC130` | Copies payload into destination; advances read cursor |
| `38` | `2FC0C0` | Returns write cursor |
| `40` | `2FC0B0` | Returns read cursor |

Transfers return their byte count on success, zero for null buffers or when
cursor plus count exceeds capacity. Reads check capacity, not written length.
The inspected serializers ignore transfer results, so a failed write can leave
an incomplete payload without propagating an error. The addition also has no
general integer-overflow guard. A test must detect failed transfers explicitly.

### Save-only test prerequisites

Use a separately owned stream; resetting a game-owned stream could destroy
state its owner needs. Inspect remaining nested writers and node methods,
resolve the player-pointer relationship, detect every failed transfer, and
account for `470E870` and any additional side effects before invoking the writer.
Then attempt one offline save at the update boundary, measuring payload length,
transfer failures, duration, and recorder continuity. Cursor advance alone is
not evidence of a complete snapshot.

No serializer invocation, game installation, save, or restore was performed in
this investigation. Evidence: workspace `work/serialization-callers.txt`,
`serialization-stream-setup.txt`, `serialization-constructors.txt`,
`serialization-stream-constructor.txt`, and `serialization-stream-methods.txt`.

## Further investigation

Finish mapping stream owner lifecycle and nested reader/node types. Map
remaining battle state and RNG coverage and understand reader callbacks before
any restore test. A deterministic save/restore/replay test remains future work.

Inspection evidence in workspace `work`: `serialization-disassembly.txt` and
`serialization-counterparts.txt`, generated with pefile and Capstone after a
hash check and PE unwind-boundary lookup.
