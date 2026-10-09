"""Three-process integration: two clients can address only the relay endpoint."""
import pathlib
import secrets
import socket
import subprocess
import sys
import tempfile

exe = str(pathlib.Path(sys.argv[1]).resolve())


def free_ports():
    sockets = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(3)]
    try:
        for s in sockets:
            s.bind(('127.0.0.1', 0))
        return [s.getsockname()[1] for s in sockets]
    finally:
        for s in sockets:
            s.close()


with tempfile.TemporaryDirectory(prefix='sc6-relay-') as temp:
    key = pathlib.Path(temp) / 'session.key'
    key.write_text(secrets.token_hex(32))
    wrong = pathlib.Path(temp) / 'wrong.key'
    wrong.write_text(secrets.token_hex(32))
    for name, delay, drop, bad_key in [('relay', 0, 0, False), ('relay-loss', 60, 5, False), ('relay-auth', 0, 0, True)]:
        relay_port, a, b = free_ports()
        relay = subprocess.Popen([exe, '--relay-server', '--port', str(relay_port), '--key-file', str(key), '--timeout-seconds', '12'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        children = []
        try:
            assert 'RELAY READY' in relay.stdout.readline()
            # Unauthenticated garbage must not reserve either role or be reflected.
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as junk:
                junk.sendto(bytes(96), ('127.0.0.1', relay_port))
            for role, port in [(0, a), (1, b)]:
                args = [exe, '--role', str(role), '--port', str(port), '--peer', '127.0.0.1', '--peer-port', str(relay_port),
                        '--key-file', str(wrong if bad_key and role else key), '--timeout-seconds', '4' if bad_key else '10',
                        '--delay-ms', str(delay), '--drop-every', str(drop)]
                children.append(subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
            for child in children:
                output = child.communicate(timeout=15)[0]
                if bad_key:
                    assert child.returncode != 0 and 'Handshake confirmed' not in output, output
                else:
                    assert child.returncode == 0 and 'peer_match=1' in output, output
                    if delay:
                        assert 'corrections=0 ' not in output, output
                print(name + ': ' + output.strip().replace('\n', ' | '), flush=True)
        finally:
            for process in children + [relay]:
                if process.poll() is None:
                    process.kill()
                process.wait()
print('PASS: relay-only addressing, corrected replay with loss/latency, garbage and wrong-key rejection')
