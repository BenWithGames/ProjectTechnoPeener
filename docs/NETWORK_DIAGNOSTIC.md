# Two-peer network diagnostic

This standalone Windows x64 tool tests real UDP input delivery into the existing
bounded rollback controller using a **synthetic simulation**. It does not load,
attach to, control, or change SC6. SC6 can remain closed. It is not online SC6
rollback, and a PASS does not prove two game instances synchronize.

The executable includes its C++ runtime. Testers do not need Visual Studio,
Python, UE4SS, the mod, or Epic/GitHub authentication to run it.

## Run on two PCs

For a temporary relay hosted on one player's PC, use `PARTNER_RELAY_SETUP.md`
included in the ZIP (source: `docs/PARTNER_RELAY_SETUP.md`). Only the relay host
needs an inbound UDP port; the other player connects outward. The following
steps describe the original direct connection mode.

1. Extract the diagnostic ZIP on both PCs. Use matching copies of the executable.
2. On one PC, create a key in the extracted directory:
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\New-NetworkSessionKey.ps1 -Path .\session.key`
3. Share that key file privately with the other tester. Both must use the same
   file. Generate a new key for a new test session; never upload it to GitHub.
4. Use mutually reachable IPv4 addresses on a LAN or an existing private network.
   This tool has no relay or NAT traversal. Direct internet use requires an
   explicit reachable UDP port on both ends. It does not configure routers or
   firewall rules. Allow the diagnostic executable only on the intended network
   if Windows asks. Do not disable the firewall.
5. Start both commands within 60 seconds, substituting the other PC's address:

   PC 1:
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\Start-NetworkProbe.ps1 -Role 0 -Peer 192.168.1.20 -KeyFile .\session.key`

   PC 2:
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\Start-NetworkProbe.ps1 -Role 1 -Peer 192.168.1.10 -KeyFile .\session.key`

   Both default to UDP 27860. `-Port` selects the local port and `-PeerPort`
   selects the other PC's port. Roles must differ. Peer address/port are pinned.
6. Collect both `logs/peer-*.txt` files. Both must report PASS and the same final
   hash, with `peer_match=1`. The command exits automatically. A timeout or one
   peer passing alone is not a successful two-peer test.

The ExecutionPolicy option applies to that PowerShell invocation only.
It does not change the machine's permanent execution policy.

## What it verifies

- HMAC-SHA256 packet authentication with a random shared 256-bit session key.
- Random per-process nonces and a two-sided challenge handshake; established
  traffic must echo the current session and come from the configured endpoint.
- Matching wire protocol, synthetic snapshot schema, simulation revision,
  frame count, and opposing player roles.
- 120 frame-numbered inputs, bounded queues/history, retransmission, duplicate
  acceptance and conflicting-input rejection.
- Late input correction using `OfflineRollbackSession`, with both corrected
  final states compared against a zero-delay synthetic oracle and each other.
- Round-trip measurement. This is diagnostic RTT, not SC6's opponent ping.

`-DelayMs 60 -DropEvery 5` adds 60 ms outbound latency and drops every fifth
outbound packet on each peer. This is explicit fault injection, not measured
network quality. No encryption or Steam account identity is provided; possession
of the private key authenticates the test peer. The diagnostic is bounded to a
single run and stops on failure instead of changing networking modes.

## Developer verification

Run `scripts/Build-NetworkProbe.ps1`, then:
`python tests/NetworkProbeTest.py build-network/SC6NetworkProbe.exe`

The suite runs two independent processes over loopback, checks baseline and
latency/loss convergence, rejects wrong keys and same-role peers, and tests
packet tampering and authenticated compatibility mismatch.

## Remaining game integration

This is a transport/controller diagnostic, not a production match transport.
The bounded history and retransmission scheme are intentionally small. Integration
still needs verified private-match detection, game/build/content compatibility,
agreed match start and player mapping, actual per-frame input capture/delivery,
a reusable game snapshot/restore/step adapter, bounded rolling history and
prediction limits, disconnect handling, and measured overlay telemetry.
No ranked behavior or fallback has been validated by this test.
