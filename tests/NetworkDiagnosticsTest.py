import hashlib
import hmac
import pathlib
import secrets
import socket
import struct
import subprocess
import sys
import tempfile

exe = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as folder:
    key = secrets.token_bytes(32)
    keyfile = pathlib.Path(folder) / 'session.key'
    keyfile.write_text(key.hex())
    for inject in (False, True):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        relay = subprocess.Popen([exe, '--relay-server', '--port', str(port), '--key-file', str(keyfile), '--timeout-seconds', '3'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            assert 'diagnostics=2' in relay.stdout.readline()
            if inject:
                def packet(nonce=123, schema=1):
                    body = struct.pack('!16I', 0x5343364e, 1, 1, 0, 0, nonce, 0, 0, schema, 1, 120, 0, 0, 1, 0, 0)
                    return body + hmac.digest(key, body, hashlib.sha256)
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                    for data in (b'x', bytes(96), packet(schema=999), packet(), packet(nonce=456)):
                        sender.sendto(data, ('127.0.0.1', port))
            output = relay.communicate(timeout=6)[0]
            assert relay.returncode == 0, output
            if inject:
                for expected in ('received=5 loopback=5 nonloopback=0', 'auth_failed=1', 'malformed_size=1', 'incompatible_or_invalid=1', 'stale_session=1', 'valid_role0=2'):
                    assert expected in output, (expected, output)
            else:
                assert 'received=0 loopback=0 nonloopback=0 auth_failed=0' in output, output
            assert key.hex() not in output
            print('PASS diagnostics: ' + ('rejection reasons and valid registration' if inject else 'no-arrival status'))
        finally:
            if relay.poll() is None:
                relay.kill()
            relay.wait()
