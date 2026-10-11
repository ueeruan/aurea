from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--binary', required=True)
p.add_argument('--output', required=True)
a = p.parse_args()
binary = Path(a.binary).resolve()
output = Path(a.output).resolve()
output.mkdir(parents=True, exist_ok=True)
results = []
safe_keys = {'PATH', 'HOME', 'LANG', 'LC_ALL', 'LD_LIBRARY_PATH',
             'VK_DRIVER_FILES', 'VK_ICD_FILENAMES', 'AUREA_BENCH_GLTF',
             'AUREA_TEST_ERRORS', 'XDG_RUNTIME_DIR', 'TMPDIR', 'OMP_NUM_THREADS'}
child_env = {k:v for k,v in os.environ.items() if k in safe_keys}

def run(label, command, timeout=240):
    path = output / (label + '.log')
    started = time.monotonic()
    try:
        r = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, env=child_env)
        raw, code, timed_out = r.stdout, r.returncode, False
    except subprocess.TimeoutExpired as e:
        raw, code, timed_out = e.stdout or b'', None, True
    path.write_bytes(raw)
    row = {'label': label, 'command': command, 'returnCode': code, 'timedOut': timed_out,
           'elapsedSeconds': round(time.monotonic()-started,3), 'bytes':len(raw),
           'sha256':hashlib.sha256(raw).hexdigest(),
           'summaryPass':b'3 testes, 677 verificacoes, 0 falhas' in raw,
           'signalReported':b'Program received signal' in raw}
    results.append(row)
    (output / 'morph-process-exits.json').write_text(json.dumps(results, indent=2))
    print(json.dumps(row), flush=True)
    # Preserve the raw process result even when summary assertions passed.
    return row

for n in range(3):
    run('morph-plain-' + str(n), [str(binary), 'MorphSharing'])
for n in range(2):
    script = output / ('gdb-' + str(n) + '.commands')
    observations = ['thread apply all bt full', 'info sharedlibrary', 'info registers']
    if n == 0:
        observations.append('generate-core-file ' + str(output / 'morph-teardown.core'))
    observations.append('x/8i $pc')
    commands = ['set pagination off', 'set confirm off', 'set print thread-events off',
                'set debuginfod enabled off', 'run',
                'python', 'inferior = gdb.selected_inferior()', 'if inferior.pid:',
                '    for observation in ' + repr(observations) + ':',
                '        try:',
                '            gdb.execute(observation)',
                '        except gdb.error as error:',
                '            print("Diagnostic command failed: %s: %s" % (observation, error))']
    commands += ['end', 'quit']
    script.write_text('\n'.join(commands)+'\n')
    run('morph-gdb-' + str(n), ['gdb','--batch','--return-child-result','-x',str(script),
                              '--args',str(binary),'MorphSharing'], timeout=360)
core = output / 'morph-teardown.core'
if core.exists():
    subprocess.run(['zstd','-T2','-3','--rm',str(core)], check=True)
print('Diagnostic completed; original acceptance gate is unchanged.', flush=True)
