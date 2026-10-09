"""Exercise packaged launchers without changing the local firewall."""
import pathlib
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import time

package = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="sc6 launcher test ") as temp:
    root = pathlib.Path(temp)
    for name in ('host folder', 'join folder'):
        shutil.copytree(package, root / name)
    host, join = root / 'host folder', root / 'join folder'
    key = root / 'test session.key'
    key.write_text(secrets.token_hex(32))
    sockets = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(3)]
    try:
        for s in sockets:
            s.bind(('127.0.0.1', 0))
        relay_port, host_port, join_port = [s.getsockname()[1] for s in sockets]
    finally:
        for s in sockets:
            s.close()
    processes = []
    try:
        common = ['-KeyFile', str(key), '-RelayPort', str(relay_port), '-SkipFirewall', '-NonInteractive']
        processes.append(subprocess.Popen(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(host / 'Network-Test.ps1'), '-Mode', 'Host', '-ClientPort', str(host_port), *common], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
        deadline = time.monotonic() + 15
        while not any('RELAY READY' in p.read_text() for p in host.glob('logs/*/relay.txt')):
            assert processes[0].poll() is None, processes[0].communicate()[0]
            assert time.monotonic() < deadline, 'Host relay did not start'
            time.sleep(0.1)
        processes.append(subprocess.Popen(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(join / 'Network-Test.ps1'), '-Mode', 'Join', '-Peer', '127.0.0.1', '-ClientPort', str(join_port), *common], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
        for p in processes:
            output = p.communicate(timeout=35)[0]
            assert p.returncode == 0 and 'PASS on this PC' in output, output
            print(output, flush=True)
        for directory in (host, join):
            log = next(directory.glob('logs/*/client.txt'))
            assert 'peer_match=1' in log.read_text()
            assert key.read_text() not in log.read_text(), 'Session key leaked to log'
        # Successful host completion must release its relay socket.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.bind(('127.0.0.1', relay_port))
        print('PASS: packaged host/join launchers, paths with spaces, saved results, no key in client logs, relay cleanup')
    finally:
        for p in processes:
            if p.poll() is None:
                p.kill()
            p.wait()
