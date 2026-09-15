"""Run actual Usb initialization against a linked fake libusb; no hardware access."""
import os
import sys
import tempfile
import subprocess
import time
import pathlib
import shutil

binary = os.path.abspath(sys.argv[1])
checks = 0
failure_cases = [
    ('', None),
    ('CAPS', 'read CAN capabilities'),
    ('TIMING', 'set CAN timing'),
    ('START', 'start CAN'),
]
for fail, stage in failure_cases:
    runtime = tempfile.mkdtemp(prefix='meatcan-usb-', dir='/private/tmp')
    os.chmod(runtime, 0o700)
    log = pathlib.Path(runtime) / 'usb.log'
    env = {
        **os.environ,
        'MEATCAN_RUNTIME_DIR': runtime,
        'MEATCAN_SHIM_LOG': str(log),
        'MEATCAN_SHIM_FAIL': fail,
    }
    daemon = subprocess.Popen(
        [binary, '__daemon', '--bitrate', '25k'],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        for _ in range(100):
            if log.exists() and ('CLOSE' if fail else 'START') in log.read_text():
                break
            time.sleep(0.01)
        else:
            raise AssertionError('initialization did not reach expected stage')
        daemon.terminate()
        _, err = daemon.communicate(timeout=3)
        lines = log.read_text().splitlines()
        expected = ['OPEN', 'CONFIG', 'CLAIM', 'CAPS']
        if fail != 'CAPS':
            expected += ['TIMING']
        if fail not in ('CAPS', 'TIMING'):
            expected += ['START']
        if not fail:
            expected += ['STOP']
        expected += ['RELEASE', 'CLOSE']
        assert lines == expected, (fail, lines, expected)
        checks += 1
        if stage:
            assert stage in err, (stage, err)
            checks += 1
        assert daemon.returncode == 0, daemon.returncode
        checks += 1
    finally:
        if daemon.poll() is None:
            daemon.kill()
            daemon.wait()
        shutil.rmtree(runtime, ignore_errors=True)

runtime = tempfile.mkdtemp(prefix='meatcan-usb-status-', dir='/private/tmp')
os.chmod(runtime, 0o700)
env = {
    **os.environ,
    'MEATCAN_RUNTIME_DIR': runtime,
    'MEATCAN_SHIM_LOG': str(pathlib.Path(runtime) / 'usb.log'),
    'MEATCAN_SHIM_FAIL': 'CAPS',
}
try:
    up = subprocess.run(
        [binary, 'up', '--bitrate', '25k'],
        env=env,
        capture_output=True,
        text=True,
        timeout=5,
    )
    assert up.returncode == 0, up.stderr
    assert 'State       waiting' in up.stdout, up.stdout
    assert 'Adapter     waiting' in up.stdout, up.stdout
    assert 'Bitrate     25,000 bps' in up.stdout, up.stdout
    checks += 2
    down = subprocess.run(
        [binary, 'down'],
        env=env,
        capture_output=True,
        text=True,
        timeout=5,
    )
    assert down.returncode == 0, down.stderr
    assert 'MeatCAN stopped' in down.stdout
    assert 'Previous    waiting' in down.stdout
    assert 'Adapter     waiting' in down.stdout
    checks += 2
finally:
    shutil.rmtree(runtime, ignore_errors=True)

runtime = tempfile.mkdtemp(prefix='meatcan-usb-scan-', dir='/private/tmp')
os.chmod(runtime, 0o700)
log = pathlib.Path(runtime) / 'usb.log'
env = {
    **os.environ,
    'MEATCAN_RUNTIME_DIR': runtime,
    'MEATCAN_SHIM_LOG': str(log),
    'MEATCAN_SHIM_RX': 'yes',
}
try:
    scan = subprocess.run(
        [
            binary,
            'scan',
            '--rates',
            '25k,25k',
            '--timeout',
            '100ms',
            '--min-frames',
            '2',
        ],
        env=env,
        capture_output=True,
        text=True,
        timeout=5,
    )
    assert scan.returncode == 0, (scan.stdout, scan.stderr)
    assert 'Mode        receive + ACK' in scan.stdout
    assert 'Candidates  1' in scan.stdout
    assert 'Bitrate detected' in scan.stdout
    assert 'Bitrate     25,000 bps' in scan.stdout
    lines = log.read_text().splitlines()
    assert 'START' in lines and 'START_LISTEN_ONLY' not in lines, lines
    assert lines.count('RX_FRAME') == 2, lines
    assert 'STOP' in lines, lines
    checks += 7
finally:
    shutil.rmtree(runtime, ignore_errors=True)

runtime = tempfile.mkdtemp(prefix='meatcan-usb-scan-empty-', dir='/private/tmp')
os.chmod(runtime, 0o700)
env = {
    **os.environ,
    'MEATCAN_RUNTIME_DIR': runtime,
    'MEATCAN_SHIM_LOG': str(pathlib.Path(runtime) / 'usb.log'),
}
try:
    scan = subprocess.run(
        [binary, 'scan', '--rate', '25k', '-t', '10ms', '-m', '1'],
        env=env,
        capture_output=True,
        text=True,
        timeout=5,
    )
    assert scan.returncode == 1, (scan.stdout, scan.stderr)
    assert 'No bitrate detected' in scan.stdout
    assert '25,000 bps  no traffic' in scan.stdout
    checks += 3
finally:
    shutil.rmtree(runtime, ignore_errors=True)

# Isolated shim-backed scan cases; never accesses real USB.
def scan_case(arguments, variables=None):
    case_runtime = tempfile.mkdtemp(prefix='meatcan-scan-check-', dir='/private/tmp')
    os.chmod(case_runtime, 0o700)
    case_log = pathlib.Path(case_runtime) / 'usb.log'
    case_env = {
        **os.environ,
        'MEATCAN_RUNTIME_DIR': case_runtime,
        'MEATCAN_SHIM_LOG': str(case_log),
        'MEATCAN_SHIM_LOG_RATE': 'yes',
        **(variables or {}),
    }
    try:
        result = subprocess.run(
            [binary, 'scan', *arguments], env=case_env,
            capture_output=True, text=True, timeout=5,
        )
        trace = case_log.read_text().splitlines() if case_log.exists() else []
        return result, trace
    finally:
        shutil.rmtree(case_runtime, ignore_errors=True)

result, trace = scan_case(['-t', '1ms'])
assert result.returncode == 1 and 'Candidates  10' in result.stdout, result
assert [line for line in trace if line.startswith('RATE ')] == [
    f'RATE {rate}' for rate in [500000, 250000, 125000, 1000000, 800000, 100000, 50000, 25000, 20000, 10000]
], trace
assert trace.count('START') == trace.count('STOP') == 10, trace
assert trace.count('OPEN') == trace.count('CLOSE') == 10, trace
checks += 3

result, trace = scan_case(['--active'])
assert result.returncode != 0, result
assert 'requires --rate or --rates' in result.stderr, result.stderr
assert not trace, trace
checks += 3

result, trace = scan_case(['--active', '--passive', '--rate', '25k'])
assert result.returncode != 0 and 'cannot be combined' in result.stderr, result
assert not trace, trace
checks += 2

result, trace = scan_case(
    ['--passive', '--rate', '25k', '-t', '10ms'],
    {'MEATCAN_SHIM_RX': 'yes'},
)
assert result.returncode == 0 and 'Mode        listen-only' in result.stdout, result
assert trace.count('START_LISTEN_ONLY') == trace.count('STOP') == 1, trace
assert trace.count('OPEN') == trace.count('CLOSE') == 1, trace
checks += 3

result, trace = scan_case(
    ['--rates', '125k,250k,125k', '--rate', '500k', '-t', '10ms', '-m', '2'],
    {'MEATCAN_SHIM_RX': 'yes', 'MEATCAN_SHIM_RX_RATE': '250000'},
)
assert result.returncode == 0 and 'Candidates  3' in result.stdout, result
assert 'Bitrate     250,000 bps' in result.stdout, result.stdout
assert [line for line in trace if line.startswith('RATE ')] == ['RATE 125000', 'RATE 250000'], trace
assert trace.count('START') == trace.count('STOP') == 2, trace
assert trace.count('OPEN') == trace.count('CLOSE') == 2, trace
checks += 4

result, trace = scan_case(
    ['--active', '--rates', '250k,500k', '-t', '100ms'],
    {'MEATCAN_SHIM_ACK_RATE': '500000'},
)
assert result.returncode == 0 and 'Mode        active probe' in result.stdout, result
assert 'Bitrate     500,000 bps' in result.stdout, result.stdout
assert '250,000 bps  no probe echo' in result.stdout, result.stdout
assert 'Probe       acknowledged' in result.stdout, result.stdout
assert trace.count('START') == trace.count('STOP') == 2, trace
assert trace.count('OPEN') == trace.count('CLOSE') == 2, trace
assert trace.count('TX_PROBE') == 2 and trace.count('RX_PROBE_ECHO') == 1, trace
assert 'START_LISTEN_ONLY' not in trace, trace
checks += 7

# Active mode requires the probe's TX echo; unrelated RX traffic is not a match.
result, trace = scan_case(
    ['--active', '--rate', '500k', '-t', '10ms'],
    {'MEATCAN_SHIM_RX': 'yes', 'MEATCAN_SHIM_RX_RATE': '500000',
     'MEATCAN_SHIM_ALLOW_ACTIVE_RX': 'yes'},
)
assert result.returncode == 1 and '500,000 bps  no probe echo' in result.stdout, result
assert trace.count('TX_PROBE') == 1 and 'RX_PROBE_ECHO' not in trace, trace
checks += 2

for pattern in ['echo', 'error', 'once', 'mixed', 'invalid_standard']:
    result, trace = scan_case(
        ['--rate', '25k', '-t', '10ms', '-m', '2'],
        {'MEATCAN_SHIM_RX': pattern},
    )
    if pattern == 'mixed':
        assert result.returncode == 0 and trace.count('RX_FRAME') == 4, (result, trace)
    else:
        assert result.returncode == 1 and 'No bitrate detected' in result.stdout, result
    assert trace.count('STOP') == trace.count('CLOSE') == 1, trace
    checks += 2

for options in [
    ['--timeout', '0'], ['--timeout', '-1'], ['--timeout', '1.5s'],
    ['--timeout', '86401s'], ['--timeout', '18446744073709552s'],
    ['--min-frames', '0'], ['--min-frames', '-1'], ['--min-frames', '1.5'],
    ['--min-frames', '18446744073709551616'], ['--rates', ''],
    ['--rates', '25k,'], ['--rates', ',25k'], ['--rates', '25k,,50k'],
    ['--rate'], ['--timeout'], ['--min-frames'], ['--rate', '25k,50k'],
]:
    result, trace = scan_case(options)
    assert result.returncode != 0 and not trace, (options, result, trace)
    checks += 1

result, trace = scan_case(
    ['--passive', '--rate', '25k', '-t', '1000us'],
    {'MEATCAN_SHIM_NO_LISTEN': 'yes'},
)
assert result.returncode != 0 and 'START_LISTEN_ONLY' not in trace, (result, trace)
assert trace.count('CLOSE') == 1 and 'STOP' not in trace, trace
checks += 2

# A long scan must own the daemon lock throughout candidate transitions.
runtime = tempfile.mkdtemp(prefix='meatcan-scan-lock-', dir='/private/tmp')
os.chmod(runtime, 0o700)
log = pathlib.Path(runtime) / 'usb.log'
env = {
    **os.environ,
    'MEATCAN_RUNTIME_DIR': runtime,
    'MEATCAN_SHIM_LOG': str(log),
}
first = subprocess.Popen(
    [binary, 'scan', '--rate', '25k', '-t', '30s'], env=env,
    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
)
try:
    for _ in range(100):
        if log.exists() and 'START' in log.read_text():
            break
        time.sleep(0.01)
    else:
        raise AssertionError('scan did not initialize')
    second = subprocess.run(
        [binary, 'scan', '--rate', '50k', '-t', '1ms'], env=env,
        capture_output=True, text=True, timeout=3,
    )
    assert second.returncode != 0, second
    assert log.read_text().splitlines().count('OPEN') == 1, log.read_text()
    checks += 2
    first.terminate()
    output, errors = first.communicate(timeout=3)
    assert first.returncode == 130 and 'scan stopped' in output, (output, errors)
    trace = log.read_text().splitlines()
    assert trace[-3:] == ['STOP', 'RELEASE', 'CLOSE'], trace
    checks += 2
    third = subprocess.run(
        [binary, 'scan', '--rate', '25k', '-t', '1ms'], env=env,
        capture_output=True, text=True, timeout=3,
    )
    assert third.returncode == 1 and 'No bitrate detected' in third.stdout, third
    assert log.read_text().splitlines().count('OPEN') == 2, log.read_text()
    checks += 2
finally:
    if first.poll() is None:
        first.kill()
        first.wait()
    shutil.rmtree(runtime, ignore_errors=True)

print(f'{checks} USB initialization/cleanup checks passed; no physical USB calls')
