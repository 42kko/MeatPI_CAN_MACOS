import os
import subprocess
import tempfile
import socket
import concurrent.futures
import time
import json
import shutil
import sys
import signal

binary = os.path.abspath(sys.argv[1])
runtime = tempfile.mkdtemp(prefix='meatcan-qa-', dir='/private/tmp')
os.chmod(runtime, 0o700)
env = {**os.environ, 'MEATCAN_RUNTIME_DIR': runtime}
results = []


def run(*args):
    return subprocess.run([binary, *args], env=env, text=True, capture_output=True, timeout=8)


def check(name, value):
    results.append((name, bool(value)))
    if not value:
        raise AssertionError(name)


def raw(line):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(4)
    s.connect(runtime + '/daemon.sock')
    s.sendall(line)
    return s


try:
    general_help = run('--help')
    check('help', general_help.returncode == 0)
    check(
        'general help lists send options',
        'Send options\n' in general_help.stdout
        and '-r, --repeat <count>' in general_help.stdout
        and '-c, --continuous' in general_help.stdout
        and '-i, --interval <time>' in general_help.stdout
        and '-q, --quiet' in general_help.stdout,
    )
    check('short help', run('-h').returncode == 0)
    check('command short help', run('up', '-h').returncode == 0)
    check(
        'send short help',
        'MeatCAN send\n\n' in run('send', '-h').stdout
        and '--continuous' in run('send', '-h').stdout,
    )
    check('version', run('--version').stdout.strip() == 'meatcan 0.1.0')
    check('absent daemon', run('status').returncode != 0)
    check('overflow bitrate rejected', run('up', '--mock', '--bitrate', '18446744073709552k').returncode != 0)
    up = run('up', '--mock', '--bitrate', '500k')
    check('up mock', up.returncode == 0)
    check('up heading', 'MeatCAN started\n\n' in up.stdout)
    check(
        'up reports connected state',
        '  State       ready' in up.stdout
        and '  Adapter     connected' in up.stdout
        and '  Backend     mock' in up.stdout
        and '  Bitrate     500,000 bps' in up.stdout,
    )
    check(
        'ready status',
        'MeatCAN status\n\n' in run('status').stdout
        and '  State       ready' in run('status').stdout,
    )
    check('duplicate up', run('up', '--mock').returncode != 0)
    subscribers = [raw(b'DUMP\n') for i in range(2)]
    for s in subscribers:
        check('dump handshake', s.recv(20) == b'OK\n')
    sent = run('send', '123#01020304')
    check(
        'send confirmation',
        sent.returncode == 0
        and 'MeatCAN TX complete\n\n' in sent.stdout
        and '  Frame       123#01020304' in sent.stdout
        and not sent.stderr,
    )
    for s in subscribers:
        check('dump frame', b'can0 123#01020304' in s.recv(4096))
        s.close()
    for invalid in ['800#', '123#0', '123#GG', '123##01', '20000000#', '123#010203040506070809']:
        check('invalid ' + invalid, run('send', invalid).returncode != 0)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        sends = list(ex.map(lambda i: run('send', f'{i:03X}#0102'), range(40)))
    check('40 simultaneous sends', all((r.returncode == 0 for r in sends)))
    check('TX counters exact', '|  TX 41' in run('status').stdout)
    for i in range(80):
        s = raw(b'DUMP\n')
        check('subscriber churn handshake', s.recv(20) == b'OK\n')
        s.close()
    check('subscriber cleanup', run('status').returncode == 0)
    s = raw(b'UNKNOWN\n')
    check('unknown IPC command', s.recv(200).startswith(b'ERR '))
    s.close()
    s = raw(b'x' * 300 + b'\n')
    check('oversized IPC command', s.recv(200).startswith(b'ERR '))
    s.close()
    down = run('down')
    check('down', down.returncode == 0)
    check(
        'down reports previous state',
        'MeatCAN stopped\n\n' in down.stdout
        and '  Previous    ready' in down.stdout
        and '  Adapter     connected' in down.stdout
        and '  Bitrate     500,000 bps' in down.stdout,
    )
    time.sleep(0.1)
    check('socket removed', not os.path.exists(runtime + '/daemon.sock'))
    check('restart', run('up', '--mock', '--bitrate', '25k').returncode == 0)
    check('restart resets counters', 'Frames      RX 0  |  TX 0' in run('status').stdout)
    repeated = run('send', '-r', '3', '-i', '1ms', '321#0102')
    check(
        'repeat three frames',
        repeated.returncode == 0
        and 'Mode        repeat x3' in repeated.stdout
        and 'Sent        3 frames' in repeated.stdout
        and '|  TX 3' in run('status').stdout,
    )
    quiet = run('send', '--quiet', '--repeat', '2', '321#0102')
    check('quiet repeated send', quiet.returncode == 0 and not quiet.stdout)
    check('repeat counters', '|  TX 5' in run('status').stdout)
    check(
        'reject repeat with continuous',
        run('send', '-c', '-r', '2', '321#01').returncode != 0,
    )
    check('reject zero repeat', run('send', '-r', '0', '321#01').returncode != 0)
    check(
        'reject invalid interval',
        run('send', '-r', '2', '-i', 'fast', '321#01').returncode != 0,
    )
    continuous = subprocess.Popen(
        [binary, 'send', '-c', '-i', '10ms', '321#01'],
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    time.sleep(0.08)
    continuous.send_signal(signal.SIGINT)
    continuous_out, continuous_err = continuous.communicate(timeout=4)
    check(
        'continuous stops on interrupt',
        continuous.returncode == 0
        and 'Mode        continuous' in continuous_out
        and 'MeatCAN TX stopped' in continuous_out
        and not continuous_err,
    )
    check('down again', run('down').returncode == 0)
    time.sleep(0.1)
    for mode, expected in [('drop', 'TX echo timeout'), ('mismatch', 'mismatched TX echo')]:
        env['MEATCAN_MOCK_ECHO'] = mode
        check(mode + ' up', run('up', '--mock').returncode == 0)
        failed = run('send', '123#0102')
        check(mode + ' send fails', failed.returncode != 0 and expected in failed.stderr)
        check(mode + ' no false completion', '|  TX 0' in run('status').stdout)
        check(mode + ' down', run('down').returncode == 0)
        time.sleep(0.1)
    env['MEATCAN_MOCK_ECHO'] = 'delay'
    check('delay up', run('up', '--mock').returncode == 0)
    started = time.monotonic()
    delayed = run('send', '123#0102')
    check('waits actual echo', delayed.returncode == 0 and time.monotonic() - started >= 0.09)
    orphan = raw(b'SEND 123#0102\n')
    time.sleep(0.025)
    orphan.close()
    for i in range(20):
        check('orphan does not corrupt status', 'State       ready' in run('status').stdout)
    check('subsequent delayed send', run('send', '456#0304').returncode == 0)
    check('delay down', run('down').returncode == 0)
    print(json.dumps({'checks': len(results), 'passed': sum((v for _, v in results)), 'results': results}, indent=2))
finally:
    try:
        run('down')
    except:
        pass
    shutil.rmtree(runtime, ignore_errors=True)
