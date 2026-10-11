"""Read owned instrumentation outputs without requiring adb stdin forwarding."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--adb', default='adb')
parser.add_argument('--serial')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
script = Path(__file__).with_suffix('.sh').read_text(encoding='utf-8')
command = [args.adb]
if args.serial:
    command += ['-s', args.serial]
# exec-out passes the command argument directly; wrapping it in shell quotes
# would turn the entire script into a filename. It forwards output, not stdin.
command += ['exec-out', 'run-as', 'com.aurea.aurea.uitest', 'sh', '-c', script]
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open('wb') as output:
    subprocess.run(command, stdout=output, check=True, timeout=120)
