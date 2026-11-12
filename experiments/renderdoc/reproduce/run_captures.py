from pathlib import Path
import json
import os
import subprocess
import shutil
import sys

SOURCE = Path(__file__).resolve().parent
REPO = SOURCE.parents[2]
if len(sys.argv) != 2:
    raise SystemExit('Usage: run_captures.py NEW_OUTPUT_DIRECTORY')
HERE = Path(sys.argv[1]).resolve()
if HERE.exists():
    raise SystemExit('Output directory already exists; recorded evidence must not be overwritten.')
command = os.environ.get('RENDERDOC_COMMAND') or shutil.which('renderdoccmd')
library = Path(os.environ.get('RENDERDOC_LIBRARY', '/usr/lib64/renderdoc/librenderdoc.so')).resolve()
app = REPO / 'build/experiments/gpu_capture'
if not command or not Path(command).is_file() or not library.is_file() or not app.is_file():
    raise SystemExit('Build experiment_tools and provide RenderDoc via PATH/RENDERDOC_COMMAND and RENDERDOC_LIBRARY.')
HERE.mkdir(parents=True)
(HERE / 'layer').mkdir()
layer = json.loads((SOURCE.parent / 'recorded/provenance/layer/renderdoc_capture.json').read_text())
layer['layer']['library_path'] = str(library)
(HERE / 'layer/renderdoc_capture.json').write_text(json.dumps(layer, indent=2) + '\n')
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
    args = [str(command), 'capture', '--wait-for-exit',
            '--opt-api-validation', '--capture-file', str(HERE / label),
            str(app), '--grid-side', '80', '--smoke-test']
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
