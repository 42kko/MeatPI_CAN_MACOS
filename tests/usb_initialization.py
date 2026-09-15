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

print(f'{checks} USB initialization/cleanup checks passed; no physical USB calls')
