"""Generate owned test patterns and run the production Android decoder probe.

Requires FFmpeg as a development tool, adb, and aurea_media_probe built for the
connected device's ABI. No FFmpeg binaries or libraries are bundled in the app.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ffmpeg', required=True)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--probe', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--only', help='Run names containing this text')
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    remote = '/data/local/tmp/aurea-media-matrix'

    def run(command, timeout=120):
        return subprocess.run(command, capture_output=True, text=True, timeout=timeout, check=True)

    run([args.adb, 'shell', 'mkdir', '-p', remote])
    run([args.adb, 'push', args.probe, remote + '/probe'])
    run([args.adb, 'shell', 'chmod', '755', remote + '/probe'])
    cases = [('h264-720p60', 1280, 720, 60, 'libx264'),
             ('hevc-1080p30', 1920, 1080, 30, 'libx265'),
             ('h264-4k24', 3840, 2160, 24, 'libx264'),
             ('h264-vertical30', 720, 1280, 30, 'libx264')]
    cases += [(f'h264-360p{fps}', 640, 360, fps, 'libx264') for fps in (24, 25, 30, 50, 120)]
    cases += [('vp9-360p30', 640, 360, 30, 'libvpx-vp9'), ('av1-96p24', 160, 96, 24, 'libaom-av1')]
    if args.only:
        cases = [case for case in cases if args.only in case[0]]
    if not cases:
        raise SystemExit('No matching cases')
    results = []
    for name, width, height, fps, encoder in cases:
        record = {'name': name, 'width': width, 'height': height, 'fps': fps, 'passed': False}
        try:
            movie = output / (name + '.mp4')
            command = [args.ffmpeg, '-y', '-hide_banner', '-f', 'lavfi', '-i',
                       f'testsrc2=size={width}x{height}:rate={fps}:duration=1',
                       '-c:v', encoder, '-threads', '4',
                       '-pix_fmt', 'yuv420p', '-crf', '28', '-an']
            if encoder in ('libx264', 'libx265'):
                command += ['-preset', 'ultrafast']
            else:
                command += ['-cpu-used', '8', '-b:v', '0']
            if encoder == 'libx265':
                command += ['-x265-params', 'pools=4:frame-threads=2', '-tag:v', 'hvc1']
            command += ['-movflags', '+faststart', str(movie)]
            encoded = run(command)
            (output / (name + '-encode.log')).write_text(encoded.stderr, encoding='utf-8')
            reference = run([args.ffmpeg, '-hide_banner', '-i', str(movie), '-vf', 'showinfo', '-f', 'null', '-'])
            timestamps = [round(float(t) * 1e6) for t in re.findall(
                r'\bn:\s*\d+\s+pts:\s*[-\d]+\s+pts_time:([-\d.e+]+)', reference.stderr)]
            if len(timestamps) != fps:
                raise RuntimeError(f'Generated {len(timestamps)} frames; expected {fps}')
            timing = output / (name + '-pts.txt')
            timing.write_text(''.join(f'{t}\n' for t in timestamps), encoding='utf-8')
            for path in (movie, timing):
                run([args.adb, 'push', str(path), remote + '/' + path.name])
            decoded = subprocess.run([args.adb, 'shell', remote + '/probe', remote + '/' + movie.name,
                                      remote + '/' + timing.name], capture_output=True, text=True, timeout=120)
            log = decoded.stdout + decoded.stderr
            (output / (name + '-decode.log')).write_text(log, encoding='utf-8')
            record.update(exitCode=decoded.returncode, log=log, passed=decoded.returncode == 0 and 'PASS frames=' in log)
        except (subprocess.SubprocessError, OSError, RuntimeError) as error:
            record['error'] = str(error)
        results.append(record)
        print(f'{name}: {"PASS" if record["passed"] else "FAIL"}', flush=True)
        (output / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return 0 if all(row['passed'] for row in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
