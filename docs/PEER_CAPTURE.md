# Capture the joining PC's network traffic

1. Extract SC6PeerCapture.zip on the friend's PC. Both players prepare the usual
   Host/Join diagnostic and stop at the final Enter prompts.
2. The friend double-clicks Capture-Peer.cmd and approves the Windows
   administrator prompt.
3. Wait for the green **CAPTURE READY** message. Both players then press Enter
   in their existing network launchers. Do not start another Host/Join instance.
4. Leave the capture window open. After 90 seconds it automatically stops,
   removes its capture filter and creates a report ZIP inside `peer-captures`.
5. Send that generated ZIP and the friend's Join `client.txt` from this attempt.

SC6 itself stays closed for the synthetic test. This helper does not change
Windows firewall rules, router settings, VPN settings or the diagnostic app.
It captures UDP port 27860 only, up to 64 bytes per packet, with a 16 MB circular
capture limit. Reports can contain IP addresses, interface names and partial
packet headers. It does not read or copy the session.key file.

It refuses to overwrite an existing Packet Monitor session or filter setup.
English Windows status output is currently required; unrecognized output stops
the helper rather than clearing filters. Administrator rights are necessary.
If the visible launcher is closed, its hidden worker still stops after the timed
window. If that worker is forcibly terminated, run `pktmon stop` from an
administrator terminal and review its filters before another capture.

A report containing no packets is not proof that no traffic left the machine:
we also inspect interfaces, capture counters and the matching client log.
