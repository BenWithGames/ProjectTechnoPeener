"""Two mock game mappings -> two read-only clients -> relay -> exact CSVs."""
import csv
import pathlib
import secrets
import socket
import subprocess
import sys
import tempfile

probe, fixture = [str(pathlib.Path(p).resolve()) for p in sys.argv[1:]]
with tempfile.TemporaryDirectory(prefix='sc6-observation-') as folder:
    root = pathlib.Path(folder)
    key = root / 'session.key'
    key.write_text(secrets.token_hex(32))
    captures = [root / 'a', root / 'b']
    for directory in captures:
        directory.mkdir()
    sockets = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(3)]
    for s in sockets:
        s.bind(('127.0.0.1', 0))
    port, *local_ports = [s.getsockname()[1] for s in sockets]
    for s in sockets:
        s.close()
    processes = []
    try:
        pids = []
        for tag, directory in enumerate(captures, 1):
            p = subprocess.Popen([fixture, str(directory), str(tag)], stdout=subprocess.PIPE, text=True)
            processes.append(p)
            pids.append(p.stdout.readline().strip())
        relay = subprocess.Popen([probe, '--relay-server', '--observation', '--port', str(port), '--key-file', str(key), '--timeout-seconds', '15'], stdout=subprocess.PIPE, text=True)
        processes.append(relay)
        assert 'RELAY READY' in relay.stdout.readline()
        clients = []
        for role in (0, 1):
            p = subprocess.Popen([probe, '--role', str(role), '--peer', '127.0.0.1', '--peer-port', str(port), '--port', str(local_ports[role]), '--key-file', str(key), '--observe-pid', pids[role], '--capture-dir', str(captures[role]), '--timeout-seconds', '12', '--drop-every', '5'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            processes.append(p)
            clients.append(p)
        for p in clients:
            output = p.communicate(timeout=15)[0]
            assert p.returncode == 0 and 'PASS observation_transport_only=1' in output, output
            assert 'game_alignment_verified=0 restore=0 injection=0' in output, output
            print(output, flush=True)
        for role, directory in enumerate(captures):
            for label, tag in [('local', role+1), ('remote', 2-role)]:
                with next(directory.glob(f'network-*-{label}.csv')).open() as f:
                    rows = list(csv.DictReader(f))
                assert len(rows) == 120
                columns = ['p0_word0', 'p0_word1', 'p1_word0', 'p1_word1', 'aux0', 'aux1', 'aux2', 'aux3', 'aux4', 'aux5']
                for frame, row in enumerate(rows):
                    assert int(row['outer']) == 1000+tag*100+frame
                    for word, col in enumerate(columns, 1):
                        assert int(row[col]) == (tag<<24)|(frame<<8)|word
        print('PASS: 120 distinct records per peer, every input word and counter delivered exactly; read-only IPC; no game involved')
    finally:
        for p in processes:
            if p.poll() is None:
                p.kill()
            p.wait()
