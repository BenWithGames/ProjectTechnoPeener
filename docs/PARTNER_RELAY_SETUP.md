# Run the temporary relay on your partner's Windows PC

## Recommended: double-click launcher

Both players extract the ZIP and double-click **Network-Test.cmd**.

- Partner chooses **H** (Host). The launcher creates a session code to share
  privately, shows local addresses, and asks them to confirm router forwarding.
  Windows requests administrator approval to add a temporary rule limited to
  this executable and UDP port. The relay runs hidden automatically.
- You choose **J** (Join), paste the partner's session code, and enter their
  public IPv4 address.
- Press Enter together at the start prompt. The launcher starts the clients,
  records results in `logs`, and stops the host relay when finished. The host
  gets another UAC prompt to remove the temporary firewall rule. Keep the
  launcher window open until cleanup finishes.

No manual key files, separate relay window, or typed commands are needed.
The host must still configure the router's UDP forwarding; the script cannot
automate arbitrary router administration. If the host has no public IPv4 due
to ISP NAT, public hosting is still needed. The router forwarding rule is not
removed automatically. These Windows firewall changes do not disable the
firewall or open any other application/port. If you force-close the launcher,
its temporary rule can remain; remove the rule named SC6 temporary network
diagnostic relay in Windows Defender Firewall's advanced inbound rules.

The command-line procedure below remains available for troubleshooting.

This package tests a synthetic rollback simulation, not an SC6 match. Both
players can leave SC6 closed. No compiler, Python, or mod installation is needed.

**Your partner hosts the relay and must make UDP 27860 reachable from the internet.**
That normally requires one router port-forward on their side. Your side does not
need forwarding or a VPN. If neither person will forward a port, this relay must
instead run on a public server; there is no hosted server included in this ZIP.
This build uses IPv4 UDP. Some networks block UDP entirely.

## 1. Extract and create the shared key

Both players extract the ZIP. In the extracted folder, your partner opens
PowerShell and runs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\New-NetworkSessionKey.ps1 -Path .\session.key
```

They send you `session.key` privately. Put it beside your extracted executable.
Never commit the key or include it in shared logs. Only create it on one PC:
all three processes (relay and both clients) need the same key.

## 2. Partner prepares the connection

- Run `ipconfig` to find the partner PC's active LAN IPv4 address.
- In the partner's router, forward **UDP external port 27860 to that PC's LAN
  address, internal UDP port 27860**. Router menus vary. Do not use DMZ mode or
  disable the firewall. A router address reservation keeps the destination stable.
- Allow `SC6NetworkProbe.exe` to receive traffic on UDP 27860 on that PC's
  intended Windows network profile. The program does not add firewall rules.
- Obtain the partner router's public IPv4 address and send it privately to you.
  You need the public address, not their `192.168.x.x`/`10.x.x.x` LAN address.

If the ISP uses carrier-grade NAT, or there is another router upstream, a single
forward may not make this PC reachable. A public server or ISP-provided public
IPv4 is then needed. Do not assume a timeout means the rollback code failed.

## 3. Partner starts the relay

In PowerShell in the extracted directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Start-NetworkRelay.ps1 -KeyFile .\session.key
```

Leave this window open. It should say `RELAY READY`. The relay stops after ten
minutes or Ctrl+C. It serves only one pair and pins each role's address and fresh
process nonce. Restart it before every new attempt, including after a timeout.

## 4. Start the two clients within 60 seconds

Partner opens a **second PowerShell window** in the same directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Start-NetworkProbe.ps1 -Role 1 -Peer 127.0.0.1 -Port 27861 -PeerPort 27860 -KeyFile .\session.key
```

You run this, replacing `PARTNER_PUBLIC_IPV4` with the actual numeric address:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Start-NetworkProbe.ps1 -Role 0 -Peer PARTNER_PUBLIC_IPV4 -Port 27861 -PeerPort 27860 -KeyFile .\session.key
```

The two clients send to the relay, never directly to each other. The relay sends
replies along the outbound UDP mapping, so your router does not need forwarding.
Using localhost for the partner's client avoids relying on their router's NAT
loopback behavior. Client port 27861 avoids conflicting with the relay's 27860.

## 5. Collect results and stop

Both clients should exit with `PASS`, the same `final_hash`, and `peer_match=1`.
Send both `logs/peer-*.txt` files to the developer. One passing client alone is
not a successful two-peer result. Stop the relay with Ctrl+C afterward. Remove
the temporary router forwarding rule when finished if it is no longer needed.

No gameplay controls are needed. RTT is measured through the relay; it is not
SC6's native ping. These results validate the diagnostic only, not live SC6
rollback or ranked fallback.

The relay verifies HMAC-SHA256 using the shared key before registering peers or
forwarding packets. It limits accepted traffic and pins two endpoints for its
lifetime. It is not a multi-user public service and does not encrypt packet
contents. A hosted production relay and game integration remain separate work.
