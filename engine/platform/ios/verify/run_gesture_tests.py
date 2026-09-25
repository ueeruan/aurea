"""Preserve host-side iOS Simulator thread samples during real XCTest gestures.

The app, gestures and test timeouts remain unchanged. A stalled main thread can
prevent XCTest from exporting its accessibility hierarchy, so sample the actual
Simulator app process independently of its UI event loop. Keep at most four
one-second samples per PID, including the first appearance of a decoded video.
"""
import json
from pathlib import Path
import subprocess
import sys
import threading
import time


def collect_samples(output, stop, report):
    counts = {}
    while not stop.is_set():
        try:
            probe = subprocess.run(['pgrep', '-x', 'Aurea'], capture_output=True,
                                   text=True, timeout=5)
            for value in probe.stdout.split():
                if not value.isdigit() or counts.get(value, 0) >= 4:
                    continue
                process = subprocess.run(['ps', '-p', value, '-o', 'command='],
                                         capture_output=True, text=True, timeout=5)
                if '.app/Aurea' not in process.stdout:
                    continue
                index = counts.get(value, 0) + 1
                counts[value] = index
                destination = output / f'Aurea-{value}-{index}.sample.txt'
                result = subprocess.run(['/usr/bin/sample', value, '1', '10',
                                         '-file', str(destination)],
                                        capture_output=True, text=True, timeout=10)
                report['samples'].append({'pid': int(value), 'time': time.time(),
                                          'path': destination.name,
                                          'exitCode': result.returncode,
                                          'stderr': result.stderr[-2000:]})
        except (OSError, subprocess.TimeoutExpired) as error:
            report['errors'].append(str(error))
        (output / 'samples.json').write_text(json.dumps(report, indent=2))
        stop.wait(15)


def main():
    if len(sys.argv) < 4 or sys.argv[2] != '--':
        print('Usage: run_gesture_tests.py OUTPUT -- TEST_COMMAND [ARGS...]', file=sys.stderr)
        return 2
    output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    stop = threading.Event()
    report = {'samples': [], 'errors': []}
    watcher = threading.Thread(target=collect_samples, args=(output, stop, report), daemon=True)
    watcher.start()
    process = None
    try:
        process = subprocess.Popen(sys.argv[3:])
        return process.wait()
    finally:
        stop.set()
        if process is not None and process.poll() is None:
            process.terminate()
        watcher.join(timeout=12)
        (output / 'samples.json').write_text(json.dumps(report, indent=2))


if __name__ == '__main__':
    sys.exit(main())
