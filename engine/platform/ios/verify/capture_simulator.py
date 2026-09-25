"""Capture the real SwiftUI app and preserve diagnostics even if a scene fails."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import traceback
import zlib


BUNDLE = 'com.aurea.aurea'
SCENES = ('home', 'home-scroll', 'editor-empty', 'layer-dock', 'transform', 'effects',
          'export', 'project-settings', 'android-project', 'text-2d', 'text-3d',
          'vector', 'shape-edit', 'appearance', 'presets', 'mask', 'android-edited',
          'android-complex-3d', 'android-complex-particles',
          'android-hdri',
          'metal-face-control', 'metal-face-culling',
          'android-precomp', 'android-precomp-inside', 'export-render')
# A identidade de um fixture de TEXTO é a forma LF, que é o que o repositório
# guarda (`* text=auto eol=lf`) e o que QUALQUER checkout entrega — no Windows o
# arquivo em disco fica com CRLF e o hash do disco não é o que o CI vê. Foi
# exatamente isso que derrubou as duas cenas de face: a auditoria registrava o
# hash do CRLF e o CI recebia LF. Binário não se normaliza (ver o mesmo critério
# em tools/parity_fixtures.py, que grava estas auditorias).
TEXTO = {'.gltf', '.json', '.obj', '.mtl', '.txt', '.xml', '.svg', '.md'}


def bytes_canonicos(path):
    raw = path.read_bytes()
    # `\r\n` escrito assim, e não como quebra de linha de verdade: um heredoc de
    # shell já mastigou estes dois literais uma vez e o arquivo foi para o CI com
    # um `b'` aberto no fim da linha (SyntaxError na linha 35, run 35929858022).
    return raw.replace(b'\r\n', b'\n') if path.suffix.lower() in TEXTO else raw


def digest_canonico(path):
    return hashlib.sha256(bytes_canonicos(path)).hexdigest()


READY_TIMEOUT = 90
POLL_INTERVAL = 0.25


def save_json(path, value):
    """Replace atomically, so a cancelled capture leaves a readable manifest."""
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2), encoding='utf-8')
    temporary.replace(path)


def run(report, *args, timeout=120, check=True):
    command = {'argv': list(args), 'startedAt': time.time()}
    report.setdefault('commands', []).append(command)
    started = time.monotonic()
    try:
        result = subprocess.run(args, capture_output=True, text=True,
                                errors='replace', timeout=timeout)
        command.update(returncode=result.returncode, stdout=result.stdout,
                       stderr=result.stderr)
        if check and result.returncode != 0:
            raise RuntimeError(f'{" ".join(args)} exited {result.returncode}: '
                               f'{result.stderr.strip() or result.stdout.strip()}')
        return result.stdout.strip()
    except subprocess.TimeoutExpired as error:
        def decoded(value):
            return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else (value or '')
        command.update(timedOut=True, timeoutSeconds=timeout,
                       stdout=decoded(error.stdout), stderr=decoded(error.stderr))
        raise
    except OSError as error:
        command['error'] = str(error)
        raise
    finally:
        command['durationSeconds'] = round(time.monotonic() - started, 3)


def wait_ready(ready, process, record, timeout=READY_TIMEOUT):
    started = time.monotonic()
    last_error = None
    while time.monotonic() - started < timeout:
        if ready.exists():
            try:
                state = json.loads(ready.read_text(encoding='utf-8'))
                if not isinstance(state, dict):
                    raise ValueError('readiness JSON is not an object')
                record['readyAfterSeconds'] = round(time.monotonic() - started, 3)
                return state
            except (OSError, ValueError) as error:
                last_error = str(error)
        code = process.poll()
        if code is not None:
            record['exitBeforeReady'] = code
            raise RuntimeError(f'app console process exited {code} before readiness'
                               + (f'; last readiness error: {last_error}' if last_error else ''))
        time.sleep(POLL_INTERVAL)
    record.update(timedOut=True, timeoutSeconds=timeout)
    if last_error:
        record['readinessError'] = last_error
    raise TimeoutError(f'app did not reach the capture state in {timeout} seconds')


def collect_export_artifacts(documents, output, record):
    """Keep the last completed phase even when the app exits before readiness."""
    probe = record.get('state', {}).get('exportProbe', {})
    progress = documents / 'parity-export-ready.json'
    if progress.is_file():
        shutil.copy2(progress, output / 'export-result.json')
        record['exportResult'] = 'export-result.json'
        try:
            probe = json.loads(progress.read_text(encoding='utf-8'))
        except (OSError, ValueError) as error:
            record['exportResultReadError'] = str(error)
    elif probe:
        save_json(output / 'export-result.json', probe)
        record['exportResult'] = 'export-result.json'
    record['exportArtifacts'] = []
    for key in ('movieFile', 'frameFile', 'projectFile'):
        name = probe.get(key)
        if isinstance(name, str) and Path(name).name == name and (documents / name).is_file():
            shutil.copy2(documents / name, output / name)
            record['exportArtifacts'].append(name)


def collect_crash_reports(output, udid, record):
    """Collect only this app's recent reports from the host/device log roots."""
    device = Path.home() / 'Library/Developer/CoreSimulator/Devices' / udid
    roots = [Path.home() / 'Library/Logs/DiagnosticReports',
             Path.home() / 'Library/Logs/CoreSimulator' / udid,
             device / 'data/Library/Logs/DiagnosticReports',
             device / 'data/Library/Logs/CrashReporter']
    record['crashReports'] = []
    seen = set()
    for root in roots:
        if not root.is_dir():
            continue
        for source in root.rglob('Aurea*.ips'):
            if (not source.is_file() or source.stat().st_mtime < record['startedAt'] - 1
                    or source.resolve() in seen):
                continue
            seen.add(source.resolve())
            name = f'{record["scene"]}-crash-{len(seen)}-{source.name}'
            shutil.copy2(source, output / name)
            record['crashReports'].append({'file': name, 'source': str(source)})


def validate_particle_probe(state, documents, output, record, frame_checker):
    probe = state.get('particleProbe', {})
    frames = probe.get('frames', [])
    record['particleFrames'] = []
    # Preserve every actual image before rejecting metadata or pixels.
    for row in frames:
        name = row.get('file')
        if isinstance(name, str) and Path(name).name == name and (documents / name).is_file():
            source = documents / name
            shutil.copy2(source, output / name)
            record['particleFrames'].append({
                'requestedFrame': row.get('requestedFrame'), 'file': name,
                'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'pixels': json.loads(run(record, str(frame_checker), str(source)))})
    if (not probe.get('parametersUnchanged') or state.get('playhead') != 36
            or state.get('detail', {}).get('localPlayhead') != 36
            or [row.get('requestedFrame') for row in frames] != [0, 36, 72, 59]):
        raise RuntimeError('Particular fixture must preserve its Android parameters and reach the actual requested frames')
    for row in frames:
        if (not row.get('statusRead') or row.get('actualFrame') != row.get('requestedFrame')
                or row.get('localFrame') != row.get('requestedFrame')
                or row.get('visibleLayerIDs') != [probe.get('layerID')]):
            raise RuntimeError('Seek/scrub or isolated Particular visibility failed after the real render barrier')
    if [row.get('mode') for row in frames] != ['seek', 'seek', 'seek', 'scrub']:
        raise RuntimeError('Particular probe did not exercise both seek and scrub')
    images = {row['requestedFrame']: row for row in record['particleFrames']}
    if set(images) != {0, 36, 72, 59}:
        raise RuntimeError('Particular probe did not produce every isolated core image')
    if images[0]['pixels'].get('lightPixels', 0) > 4:
        raise RuntimeError('Snow time-zero reference unexpectedly contains visible pixels; inspect its imported parameters')
    for frame in (36, 72, 59):
        pixels = images[frame]['pixels']
        if (pixels.get('width') != 480 or pixels.get('height') != 270
                or pixels.get('lightPixels', 0) < 16 or pixels.get('maxRGB', 0) < 24):
            raise RuntimeError(f'Particular alone has no visible particles at frame {frame}')
    if (probe.get('changedFromZero', 0) < 16 or probe.get('changedBetweenTimes', 0) < 16
            or len({row['sha256'] for row in images.values()}) != 4):
        raise RuntimeError('Particular pixels do not change with time; layer kinds alone cannot validate its renderer')


def validate_face_probe(state, record):
    """Use independently decoded GPU pixels, not the app's success flag."""
    scene = record['scene']
    probe = state.get('faceProbe', {})
    pixels = record.get('pixels', {})
    perf = state.get('renderDiagnostics', {}).get('perf', {})
    control = scene == 'metal-face-control'
    if (not probe.get('imported') or not probe.get('saved')
            or probe.get('fixture') != scene + '.gltf'
            or probe.get('doubleSided') is not control
            or state.get('layerCount') != 1 or perf.get('triangles3D') != 2
            or perf.get('draws3D', 0) < 2):
        raise RuntimeError('Front-face probe did not import and submit both real glTF triangles')
    red = pixels.get('redLeftPixels', 0)
    green = pixels.get('greenRightPixels', 0)
    if (pixels.get('width') != 480 or pixels.get('height') != 270
            or red < 256 or pixels.get('redRightPixels', 0) > 4
            or pixels.get('greenLeftPixels', 0) > 4):
        raise RuntimeError('Front-facing unlit red triangle is missing or misplaced; inspect Metal front-face winding')
    if control:
        if green < 256 or abs(red - green) > max(red, green) * 0.03 + 4:
            raise RuntimeError('Double-sided control must render both equal-area red and green triangles')
    elif green > 4:
        raise RuntimeError('Back-facing green triangle survived back-face culling')


def collect_home_backdrop(documents, output, record):
    record['homeBackdropArtifacts'] = []
    for name in ('home-backdrop.json', 'home-backdrop-tab-source.png',
                 'home-backdrop-tab-blur.png', 'home-backdrop-batch-source.png',
                 'home-backdrop-batch-blur.png'):
        if (documents / name).is_file():
            shutil.copy2(documents / name, output / name)
            record['homeBackdropArtifacts'].append(name)


def aurea_assets(path):
    """Audit original wire sections, not a substitute JSON project format."""
    data = path.read_bytes()
    magic, version, minimum, count, index, total = struct.unpack_from('<IHHIQQ', data)
    if magic != 0x41455255 or total != len(data) or index + count * 40 > len(data):
        raise RuntimeError('Invalid .aurea header in HDRI roundtrip')
    assets = None
    for section in range(count):
        kind, revision, offset, size, raw_size, crc, flags = struct.unpack_from('<HIQQQII', data, index + section * 40)
        raw = data[offset:offset + size]
        if flags or size != raw_size or len(raw) != size or zlib.crc32(raw) != crc:
            raise RuntimeError('Invalid section CRC/size in HDRI roundtrip')
        if kind == 4:
            if revision != 1:
                raise RuntimeError('HDRI asset audit expects the original Assets revision 1')
            assets = raw
    if assets is None or len(assets) < 4:
        raise RuntimeError('HDRI project has no asset section')
    return assets


def collect_hdri_artifacts(documents, output, record):
    names = ('android-hdri.aurea', 'android-hdri-roundtrip.aurea',
             'android-hdri-loaded.png', 'android-hdri-reopened.png')
    record['hdriArtifacts'] = []
    for name in names:
        if (documents / name).is_file():
            shutil.copy2(documents / name, output / name)
            record['hdriArtifacts'].append(name)
    relative = Path(record.get('hdriCompanionPath', ''))
    if relative.parts and not relative.is_absolute() and '..' not in relative.parts and (documents / relative).is_file():
        (output / relative).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(documents / relative, output / relative)
        record['hdriArtifacts'].append(relative.as_posix())


def validate_hdri_probe(state, documents, output, record, frame_checker):
    collect_hdri_artifacts(documents, output, record)
    probe = state.get('hdriProbe', {})
    rows = probe.get('frames', [])
    if (not all(probe.get(key) for key in ('loaded', 'saved', 'reopened', 'originalUnchanged'))
            or probe.get('fixture') != 'android-hdri.aurea'
            or probe.get('roundtripFile') != 'android-hdri-roundtrip.aurea'
            or state.get('project') != 'android-hdri-roundtrip.aurea'
            or state.get('playhead') != 0
            or [row.get('phase') for row in rows] != ['loaded', 'reopened']):
        raise RuntimeError('HDRI probe did not load the original Android project and save/reopen a separate copy')
    original = documents / 'android-hdri.aurea'
    if hashlib.sha256(original.read_bytes()).hexdigest() != record.get('fixtureSHA256'):
        raise RuntimeError('HDRI helper modified the approved Android input')
    companion = documents / record['hdriCompanionPath']
    if hashlib.sha256(companion.read_bytes()).hexdigest() != record.get('companionSHA256'):
        raise RuntimeError('HDRI companion bytes changed during the load/save/render roundtrip')
    original_assets = aurea_assets(original)
    saved_assets = aurea_assets(documents / 'android-hdri-roundtrip.aurea')
    if (struct.unpack_from('<I', original_assets)[0] != 2 or original_assets != saved_assets
            or record['hdriStoredPath'].encode('utf-8') not in saved_assets):
        raise RuntimeError('HDRI roundtrip changed or lost the original procedural/environment assets')
    record['hdriAssetsSHA256'] = hashlib.sha256(saved_assets).hexdigest()
    record['hdriFrames'] = []
    initial_environment = rows[0].get('environment', [])
    initial_object = rows[0].get('objectEnvironment', [])
    for row in rows:
        environment = row.get('environment', [])
        if (not row.get('statusRead') or row.get('actualFrame') != 0
                or sorted(layer.get('kind') for layer in row.get('layers', [])) != [10, 11]
                or len(environment) != 3 or environment[0] != 1 or environment[1] <= 0
                or environment != initial_environment or not initial_object or initial_object[0] != 0
                or row.get('objectEnvironment') != initial_object):
            raise RuntimeError('Original HDRI environment/parameters or frame-zero composition changed after loading')
        name = row.get('file', '')
        if name != f'android-hdri-{row["phase"]}.png':
            raise RuntimeError('HDRI probe did not preserve both actual core images')
        source = documents / name
        pixels = json.loads(run(record, str(frame_checker), str(source)))
        record['hdriFrames'].append({'phase': row['phase'], 'file': name, 'pixels': pixels,
                                    'sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
        sums = pixels.get('rgbSums', [])
        if (pixels.get('width') != 480 or pixels.get('height') != 270 or len(sums) != 3
                or pixels.get('redDominantPixels', 0) < 64 or sums[0] < 1.2 * max(sums[1], sums[2])):
            raise RuntimeError('HDRI did not produce red-dominant illumination; a white neutral fallback does not pass')
    before, after = (frame['pixels'] for frame in record['hdriFrames'])
    if (before.get('contentBounds') != after.get('contentBounds')
            or any(abs(a - b) > max(1, a) * 0.02 for a, b in zip(before['rgbSums'], after['rgbSums']))):
        raise RuntimeError('HDRI pixels changed materially after saving/reopening the same frame')


def validate_home_backdrop(state, documents, output, record, frame_checker):
    probe = state.get('homeScrollProbe', {})
    rows = probe.get('projects', [])
    if probe.get('count') != 12 or len(rows) != 12 or not all(row.get('saved') and row.get('hasThumbnail') for row in rows):
        raise RuntimeError('Home scroll scene must contain twelve saved core projects with actual rendered thumbnails')
    record['homeThumbnails'] = []
    for row in rows:
        name = row.get('thumbnail', '')
        source = documents / 'Thumbs' / name
        if Path(name).name != name or not source.is_file():
            raise RuntimeError('Missing actual project thumbnail in Home scroll scene')
        target = output / ('home-scroll-' + name)
        shutil.copy2(source, target)
        pixels = json.loads(run(record, str(frame_checker), str(source)))
        record['homeThumbnails'].append({'file': target.name, 'pixels': pixels})
        if pixels.get('lightPixels', 0) < 16 or pixels.get('maxRGB', 0) - pixels.get('minRGB', 0) < 24:
            raise RuntimeError('Home thumbnail was not rendered by the core')
    collect_home_backdrop(documents, output, record)
    backdrop = json.loads((documents / 'home-backdrop.json').read_text(encoding='utf-8'))
    record['homeBackdrop'] = backdrop
    if not backdrop.get('sourceOnly') or not backdrop.get('scrollCompleted') or backdrop.get('failedSnapshots') != 0:
        raise RuntimeError('Home backdrop did not complete a real list-only scroll capture')
    for kind, sigma in (('tab', 24), ('batch', 20)):
        bar = backdrop.get('bars', {}).get(kind, {})
        target_sigma = sigma * bar.get('scale', 0)
        if (bar.get('captureCount', 0) < 2 or bar.get('sigmaPoints') != sigma
                or target_sigma <= 0 or abs(bar.get('measuredSigmaPixels', 0) - target_sigma) > target_sigma * 0.05):
            raise RuntimeError(f'{kind} backdrop did not update or its measured Gaussian sigma differs from Android')
        hashes = []
        for stage in ('source', 'blur'):
            source = documents / f'home-backdrop-{kind}-{stage}.png'
            pixels = json.loads(run(record, str(frame_checker), str(source)))
            bar[stage + 'Pixels'] = pixels
            hashes.append(hashlib.sha256(source.read_bytes()).hexdigest())
            if (pixels.get('width') != bar.get('sourceWidth') or pixels.get('height') != bar.get('sourceHeight')
                    or pixels.get('lightPixels', 0) < 16):
                raise RuntimeError(f'{kind} {stage} capture contains no visible list content')
        if hashes[0] == hashes[1]:
            raise RuntimeError(f'{kind} backdrop filter left its input unchanged')


def capture_scene(scene, app, output, udid, console_option, report, frame_checker):
    started = time.monotonic()
    record = {'scene': scene, 'status': 'running', 'startedAt': time.time(),
              'stdout': f'{scene}.stdout.log', 'stderr': f'{scene}.stderr.log'}
    report['scenes'].append(record)
    save_json(output / 'manifest.json', report)
    process = None
    documents = None
    stdout = stderr = None
    try:
        # Keep each scene independent of previous projects/settings/failures.
        # Only this dedicated CI simulator's fixture application is reset.
        run(record, 'xcrun', 'simctl', 'uninstall', udid, BUNDLE, check=False)
        run(record, 'xcrun', 'simctl', 'install', udid, str(app))
        container = Path(run(record, 'xcrun', 'simctl', 'get_app_container', udid, BUNDLE, 'data'))
        documents = container / 'Documents'
        documents.mkdir(parents=True, exist_ok=True)
        if scene == 'export-render':
            shutil.copy2('engine/tests/data/preview-bframes.mp4', documents / 'preview-bframes.mp4')
        ready = documents / 'parity-ready.json'
        record['readyPath'] = str(ready)
        if ready.exists():
            ready.unlink()  # A failed uninstall must not reuse stale readiness.
        if scene in ('android-project', 'android-edited'):
            shutil.copy2('docs/parity/fixtures/android-2103-square-glow.aurea', documents / 'android-reference.aurea')
        if scene.startswith('android-complex-'):
            shutil.copy2('docs/parity/fixtures/android-2103-complex.aurea', documents / 'android-complex.aurea')
        if scene in ('android-precomp', 'android-precomp-inside'):
            shutil.copy2('docs/parity/fixtures/android-2103-precomp.aurea', documents / 'android-precomp.aurea')
        if scene == 'android-hdri':
            fixtures = Path('docs/parity/fixtures')
            fixture = fixtures / 'android-2103-hdri.aurea'
            audit = json.loads(fixture.with_suffix('.audit.json').read_text(encoding='utf-8'))
            companion = fixtures / audit['companionFixture']
            digest = digest_canonico(fixture)
            companion_digest = digest_canonico(companion)
            if digest != audit['sha256'] or companion_digest != audit['companionSHA256']:
                raise RuntimeError('Android HDRI fixture/companion does not match audited original bytes')
            relative = Path(audit['requiredCompanionRelativePath'])
            if relative.is_absolute() or '..' in relative.parts:
                raise RuntimeError('HDRI companion must stay inside the simulator Documents fixture folder')
            target = documents / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(fixture, documents / 'android-hdri.aurea')
            shutil.copy2(companion, target)
            record.update(fixtureSHA256=digest, companionSHA256=companion_digest,
                          hdriStoredPath=audit['environmentStoredPath'], hdriCompanionPath=str(relative))
        if scene in ('metal-face-control', 'metal-face-culling'):
            fixture = Path('docs/parity/fixtures') / (scene + '.gltf')
            audit = json.loads(fixture.with_suffix('.audit.json').read_text(encoding='utf-8'))
            digest = digest_canonico(fixture)
            if digest != audit['sha256'] or audit['externalResources'] != 0:
                raise RuntimeError('Synthetic front-face fixture does not match its audited bytes')
            shutil.copy2(fixture, documents / fixture.name)
            record['fixtureSHA256'] = digest
        argv = ['xcrun', 'simctl', 'launch', console_option, udid, BUNDLE]
        record['launch'] = {'argv': argv, 'consoleOption': console_option}
        stdout = (output / record['stdout']).open('wb')
        stderr = (output / record['stderr']).open('wb')
        environment = dict(os.environ, SIMCTL_CHILD_AUREA_PARITY_SCENE=scene)
        if scene == 'export-render':
            environment['SIMCTL_CHILD_AUREA_PARITY_EXPORT'] = '1'
        process = subprocess.Popen(argv, stdout=stdout, stderr=stderr, env=environment)
        record['launch']['hostProcessPid'] = process.pid
        state = wait_ready(ready, process, record, timeout=210 if scene == 'export-render' else READY_TIMEOUT)
        record['state'] = state
        save_json(output / f'{scene}.json', state)
        report['screens'].append(state)
        if state.get('scene') != scene:
            raise RuntimeError(f'expected scene {scene!r}, received {state.get("scene")!r}')
        if scene == 'home-scroll':
            backdrop_wait = {}
            wait_ready(documents / 'home-backdrop.json', process, backdrop_wait, timeout=30)
            record['backdropReadyAfterSeconds'] = backdrop_wait.get('readyAfterSeconds')
        run(record, 'xcrun', 'simctl', 'io', udid, 'screenshot', str(output / f'{scene}.png'))
        record['screenshot'] = f'{scene}.png'
        core_frame = documents / f'{scene}-core.png'
        if core_frame.exists():
            shutil.copy2(core_frame, output / core_frame.name)
            record['coreFrame'] = core_frame.name
            record['pixels'] = json.loads(run(record, str(frame_checker), str(core_frame)))
        projects = sorted(documents.glob('*.aurea'))
        record['projects'] = []
        for index, project in enumerate(projects):
            name = f'{scene}.aurea' if len(projects) == 1 else f'{scene}-{index}-{project.name}'
            shutil.copy2(project, output / name)
            record['projects'].append(name)
        if scene == 'export-render':
            probe = state.get('exportProbe', {})
            collect_export_artifacts(documents, output, record)
            if not probe.get('passed'):
                raise RuntimeError('Real export/MP4 decode failed: ' + probe.get('error', 'missing terminal export result'))
        if not state.get('coreStarted'):
            raise RuntimeError('Metal/core startup failed; inspect app stdout/stderr and scene JSON')
        if scene not in ('home', 'home-scroll') and (not record.get('coreFrame') or state.get('frameWidth', 0) <= 0 or state.get('frameHeight', 0) <= 0):
            raise RuntimeError('Core did not produce a real preview frame')
        if scene == 'home-scroll':
            validate_home_backdrop(state, documents, output, record, frame_checker)
        if scene == 'android-complex-particles':
            validate_particle_probe(state, documents, output, record, frame_checker)
        if scene == 'android-hdri':
            validate_hdri_probe(state, documents, output, record, frame_checker)
        if scene in ('metal-face-control', 'metal-face-culling'):
            validate_face_probe(state, record)
        if scene not in ('home', 'home-scroll', 'editor-empty') and state.get('layerCount', 0) == 0:
            raise RuntimeError('Core fixture did not create or restore a layer')
        if scene not in ('home', 'home-scroll', 'editor-empty'):
            pixels = record.get('pixels', {})
            if pixels.get('lightPixels', 0) < 16 or pixels.get('maxRGB', 0) - pixels.get('minRGB', 0) < 24:
                raise RuntimeError('Rendered fixture is blank or uniform; startup/layer metadata alone do not prove rendering')
        if scene == 'text-3d':
            pixels = record.get('pixels', {})
            bounds = pixels.get('contentBounds', [])
            if (len(bounds) != 4 or bounds[0] < 10 or bounds[1] < 10
                    or bounds[2] > pixels['width'] - 10 or bounds[3] > pixels['height'] - 10):
                raise RuntimeError('Default Android 3D text must fit inside the frame; inspect depth units and the rendered geometry')
        if scene == 'android-project' and (state.get('layerCount') != 1 or not state.get('effects')):
            raise RuntimeError('Android reference project failed to restore its layer/effect')
        if scene.startswith('android-complex-'):
            if state.get('layerCount') != 2 or sorted(layer.get('kind') for layer in state.get('layers', [])) != [10, 11]:
                raise RuntimeError('Android 3D/Particular project failed to restore its original layer types')
        if scene in ('android-precomp', 'android-precomp-inside'):
            probe = state.get('precompProbe', {})
            if (not probe.get('loaded') or state.get('project') != 'android-precomp.aurea'
                    or [layer.get('kind') for layer in probe.get('rootLayers', [])] != [12]):
                raise RuntimeError('Android precomposition fixture failed to restore its root Composition layer')
            inside = scene == 'android-precomp-inside'
            expected_kind, expected_depth = (5, 1) if inside else (12, 0)
            if (state.get('precompDepth') != expected_depth or state.get('layerCount') != 1
                    or [layer.get('kind') for layer in state.get('layers', [])] != [expected_kind]
                    or (inside and not probe.get('entered'))):
                raise RuntimeError('Android precomposition did not expose the expected real nested/root layer')
        if scene == 'android-edited':
            detail = state.get('detail', {})
            rotation = detail.get('rotation', [])
            if (not state.get('reopenedEditedProject') or len(rotation) != 3
                    or abs(rotation[2] - 15) > 0.001 or abs(detail.get('opacity', 0) - 0.75) > 0.001):
                raise RuntimeError('Saved .aurea lost the queued edits when reopened')
        record['status'] = 'captured'
    except Exception as error:
        record.update(status='failed', error=f'{type(error).__name__}: {error}',
                      traceback=traceback.format_exc())
        report['errors'].append(f'{scene}: {record["error"]}')
    finally:
        try:
            if process is not None:
                record['exitCodeBeforeCleanup'] = process.poll()
                record['terminationRequested'] = True
                try:
                    run(record, 'xcrun', 'simctl', 'terminate', udid, BUNDLE, timeout=20, check=False)
                except Exception as error:
                    record['terminateError'] = str(error)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    # End the host relay only after requesting simulator app exit.
                    record['consoleRelayTerminated'] = True
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        record['consoleRelayKilled'] = True
                        process.kill()
                        process.wait(timeout=5)
                record['exitCodeAfterCleanup'] = process.returncode
        except Exception as error:
            record['cleanupError'] = f'{type(error).__name__}: {error}'
            record['status'] = 'failed'
            report['errors'].append(f'{scene}: cleanup: {record["cleanupError"]}')
        finally:
            if scene == 'android-hdri' and documents is not None:
                try:
                    collect_hdri_artifacts(documents, output, record)
                except Exception as error:
                    record['hdriArtifactError'] = f'{type(error).__name__}: {error}'
            if scene == 'home-scroll' and documents is not None:
                try:
                    collect_home_backdrop(documents, output, record)
                except Exception as error:
                    record['homeBackdropArtifactError'] = f'{type(error).__name__}: {error}'
            if scene == 'export-render' and documents is not None:
                try:
                    collect_export_artifacts(documents, output, record)
                except Exception as error:
                    record['exportArtifactError'] = f'{type(error).__name__}: {error}'
            if record['status'] != 'captured':
                try:
                    collect_crash_reports(output, udid, record)
                except Exception as error:
                    record['crashCollectionError'] = f'{type(error).__name__}: {error}'
            for stream in (stdout, stderr):
                if stream is not None:
                    stream.close()
            if record['status'] == 'running':
                record['status'] = 'interrupted'
            record['durationSeconds'] = round(time.monotonic() - started, 3)
            save_json(output / f'{scene}.capture.json', record)
            save_json(output / 'manifest.json', report)
    return record


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 2:
        print('Usage: capture_simulator.py APP_PATH OUTPUT_DIRECTORY', file=sys.stderr)
        return 2
    app, output = (Path(value).resolve() for value in argv)
    output.mkdir(parents=True, exist_ok=True)
    report = {'status': 'running', 'startedAt': time.time(), 'app': str(app),
              'screens': [], 'scenes': [], 'errors': []}
    started = time.monotonic()
    try:
        frame_checker = output.parent / 'check-core-frame'
        run(report, 'xcrun', 'swiftc', 'engine/platform/ios/verify/check_core_frame.swift',
            '-o', str(frame_checker))
        devices = json.loads(run(report, 'xcrun', 'simctl', 'list', 'devices', 'available', '-j'))['devices']
        phones = [d for group in devices.values() for d in group if 'iPhone' in d['name']]
        if not phones:
            raise RuntimeError('no available iPhone simulator')
        phone = next((d for d in phones if d['name'] == 'iPhone 16'), phones[0])
        udid = phone['udid']
        report['device'] = phone
        if phone['state'] != 'Booted':
            run(report, 'xcrun', 'simctl', 'boot', udid)
        run(report, 'xcrun', 'simctl', 'bootstatus', udid, '-b', timeout=180)
        run(report, 'xcrun', 'simctl', 'status_bar', udid, 'override', '--time', '9:41',
            '--batteryState', 'charged', '--batteryLevel', '100')
        # Probe the installed Xcode, rather than assuming a launch option.
        help_text = run(report, 'xcrun', 'simctl', 'help', 'launch')
        help_text += '\n' + report['commands'][-1].get('stderr', '')
        options = re.findall(r'--[a-z][a-z-]*', help_text)
        console_option = '--console' if '--console' in options else '--console-pty' if '--console-pty' in options else None
        if console_option is None:
            raise RuntimeError('installed simctl has no console launch option; inspect recorded launch help')
        report['consoleOption'] = console_option
        startup_failures = 0
        for scene in SCENES:
            record = capture_scene(scene, app, output, udid, console_option, report, frame_checker)
            print(f'{scene}: {record["status"]} ({record["durationSeconds"]:.1f}s)', flush=True)
            if record['status'] != 'captured':
                print(record.get('error', 'capture interrupted'), file=sys.stderr, flush=True)
                for key in ('stdout', 'stderr'):
                    path = output / record[key]
                    if path.exists():
                        tail = path.read_text(encoding='utf-8', errors='replace')[-6000:]
                        print(f'--- {path.name} (tail) ---\n{tail}', file=sys.stderr, flush=True)
            startup_failed = record.get('timedOut') or (
                record.get('state') is not None and not record['state'].get('coreStarted'))
            startup_failures = startup_failures + 1 if startup_failed else 0
            if startup_failures >= 2:
                report['stoppedAfterRepeatedStartupFailure'] = True
                print('Stopping after two repeated startup/readiness failures; remaining UI scenes cannot validate the editor',
                      file=sys.stderr, flush=True)
                break
        report['status'] = 'failed' if report['errors'] else 'captured'
    except BaseException as error:
        report['status'] = 'failed'
        report['errors'].append(f'{type(error).__name__}: {error}')
        report['traceback'] = traceback.format_exc()
        print(report['errors'][-1], file=sys.stderr, flush=True)
        if isinstance(error, (KeyboardInterrupt, SystemExit)):
            raise
    finally:
        report['durationSeconds'] = round(time.monotonic() - started, 3)
        save_json(output / 'manifest.json', report)
    if report['status'] != 'captured':
        print(f'Capture failed: inspect {output / "manifest.json"} and per-scene console logs', file=sys.stderr)
        return 1
    print(f'Captured {len(report["screens"])} actual iOS application states')
    return 0


if __name__ == '__main__':
    sys.exit(main())
