from pathlib import Path
import json
import os
import re
import select
import signal
import sys


def clock_range(text):
    section = text.split('OD_SCLK:', 1)[1].split('OD_RANGE:', 1)[0]
    values = dict((int(i), int(mhz)) for i, mhz in re.findall(r'^\s*([01]):\s*(\d+)Mhz', section, re.M))
    return values[0], values[1]


def try_clock(mode, od, wait_for_test):
    previous_mode = mode.read_text().strip()
    previous_range = clock_range(od.read_text())
    if previous_mode != 'auto' or previous_range != (600, 3000):
        raise RuntimeError('GPU settings changed since inspection; leaving them untouched.')
    try:
        mode.write_text('manual\n')
        od.write_text('s 0 2000\n')
        od.write_text('s 1 2000\n')
        od.write_text('c\n')
        actual = clock_range(od.read_text())
        if actual != (2000, 2000):
            raise RuntimeError(f'Driver did not accept 2000 MHz limits: {actual}')
        wait_for_test(actual)
    finally:
        # Restore captured limits before returning to automatic management, even after a failed write.
        try:
            od.write_text(f's 1 {previous_range[1]}\n')
            od.write_text(f's 0 {previous_range[0]}\n')
            od.write_text('c\n')
        finally:
            mode.write_text(previous_mode + '\n')
        restored = (mode.read_text().strip(), clock_range(od.read_text()))
        if restored != (previous_mode, previous_range):
            raise RuntimeError(f'GPU restore verification failed: {restored}')
        print('RESTORED ' + json.dumps(dict(mode=restored[0], limits=restored[1])), flush=True)


def wait_for_test(actual):
    print('READY ' + json.dumps(dict(limits=actual)), flush=True)
    # EOF or a 1800-second deadline also restores settings if the unprivileged controller exits.
    if select.select([sys.stdin], [], [], 1800)[0]:
        sys.stdin.readline()


if __name__ == '__main__':
    if os.geteuid() != 0:
        raise SystemExit('Administrator authentication is required.')
    base = Path('/sys/class/drm/card1/device')
    if (base / 'vendor').read_text().strip() != '0x1002' or (base / 'device').read_text().strip() != '0x1114':
        raise SystemExit('The inspected Radeon 860M device was not found; no settings changed.')
    for event in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(event, lambda *_: sys.exit(1))
    try_clock(base / 'power_dpm_force_performance_level', base / 'pp_od_clk_voltage', wait_for_test)
