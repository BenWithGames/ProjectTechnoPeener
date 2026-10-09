"""Real two-process UDP tests; no SC6 process or game files involved."""
import pathlib
import secrets
import socket
import subprocess
import sys
import tempfile

exe = pathlib.Path(sys.argv[1]).resolve()


def ports():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as a, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as b:
        a.bind(('127.0.0.1', 0))
        b.bind(('127.0.0.1', 0))
        return a.getsockname()[1], b.getsockname()[1]


with tempfile.TemporaryDirectory(prefix='sc6-network-') as folder:
    key = pathlib.Path(folder) / 'session.key'
    key.write_text(secrets.token_hex(32))
    wrong = pathlib.Path(folder) / 'wrong.key'
    wrong.write_text(secrets.token_hex(32))
    subprocess.run([str(exe), '--self-test', '--key-file', str(key)], check=True)
    for name, delay, drop, mismatch, same_role in [
        ('baseline', 0, 0, False, False),
        ('delayed-loss', 60, 5, False, False),
        ('wrong-key', 0, 0, True, False),
        ('same-role', 0, 0, False, True),
    ]:
        first, second = ports()
        children = []
        try:
            for role, local, remote in [(0, first, second), (1, second, first)]:
                args = [str(exe), '--role', str(0 if same_role else role), '--port', str(local),
                        '--peer', '127.0.0.1', '--peer-port', str(remote),
                        '--key-file', str(wrong if mismatch and role else key),
                        '--delay-ms', str(delay), '--drop-every', str(drop),
                        '--timeout-seconds', '4' if mismatch or same_role else '15']
                children.append(subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
            outputs = [p.communicate(timeout=20)[0] for p in children]
            for p, output in zip(children, outputs):
                if mismatch or same_role:
                    assert p.returncode != 0 and 'Handshake confirmed' not in output, output
                else:
                    assert p.returncode == 0 and 'peer_match=1' in output, output
                    if delay:
                        assert 'corrections=0 ' not in output, output
                print(name + ': ' + output.strip().replace('\n', ' | '))
        finally:
            for p in children:
                if p.poll() is None:
                    p.kill()
                    p.wait()
print('PASS: real UDP peers converge; latency/loss cause corrected replay; key and role mismatch fail closed')
