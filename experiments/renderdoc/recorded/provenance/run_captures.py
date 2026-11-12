from pathlib import Path
import json
import os
import subprocess

HERE = Path(__file__).resolve().parent
TOOLS = Path('/tmp/cg-renderdoc-fedora')
library = TOOLS / 'usr/lib64/renderdoc/librenderdoc.so'
env = dict(os.environ, LD_LIBRARY_PATH=str(library.parent),
           VK_LAYER_PATH=str(HERE / 'layer') + ':/usr/share/vulkan/explicit_layer.d',
           VK_INSTANCE_LAYERS='VK_LAYER_RENDERDOC_Capture:VK_LAYER_KHRONOS_validation',
           VK_VALIDATION_VALIDATE_SYNC='1', ENABLE_VULKAN_RENDERDOC_CAPTURE='1')
env['XDG_SESSION_TYPE'] = 'x11'
# Hide a stale user-layer registration only inside this child process.
env['XDG_DATA_HOME'] = str(HERE / 'runtime-data')
env['XDG_CONFIG_HOME'] = str(HERE / 'runtime-config')
for name in ('runtime-data', 'runtime-config'):
    (HERE / name).mkdir(exist_ok=True)
env.pop('LD_PRELOAD', None)
results = []
for level in (0, 3):
    label = 'grouped' if level else 'per-object'
    env['CG_CAPTURE_PATH'] = str(HERE / label)
    args = [str(TOOLS / 'usr/bin/renderdoccmd'), 'capture', '--wait-for-exit',
            '--opt-api-validation', '--capture-file', str(HERE / label),
            str(HERE / 'gpu_capture'), '--grid-side', '80', '--smoke-test']
    if level:
        args += ['--grouped', '--group-level', str(level)]
    with (HERE / f'{label}.stdout').open('w') as out, (HERE / f'{label}.stderr').open('w') as err:
        result = subprocess.run(args, env=env, stdout=out, stderr=err, timeout=60)
    output = (HERE / f'{label}.stdout').read_text()
    error = (HERE / f'{label}.stderr').read_text()
    print(label, result.returncode, output[-1600:], error[-3000:], flush=True)
    assert result.returncode == 0 and 'RENDERDOC_CAPTURE_COMPLETE 1' in output
    assert 'all commands match CPU reference' in output
    assert not any(marker in output + error for marker in ('VUID-', 'Validation Error', 'SYNC-HAZARD'))
    captures = list(HERE.glob(f'{label}*.rdc'))
    assert len(captures) == 1 and captures[0].stat().st_size > 1000
    results.append(dict(mode=label, path=str(captures[0]), bytes=captures[0].stat().st_size))
(HERE / 'captures.json').write_text(json.dumps(results, indent=2) + '\n')
