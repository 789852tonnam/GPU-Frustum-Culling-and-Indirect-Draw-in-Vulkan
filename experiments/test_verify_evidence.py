from pathlib import Path
import hashlib
import json
import tempfile
from verify_evidence import verify_manifest

with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    (root / 'data').write_bytes(b'original')
    (root / 'manifest.json').write_text(json.dumps({'files':[dict(path='data',bytes=8,
        sha256=hashlib.sha256(b'original').hexdigest())]}))
    assert verify_manifest(root) == 1
    (root / 'data').write_bytes(b'modified')
    try:
        verify_manifest(root)
    except ValueError:
        pass
    else:
        raise AssertionError('A changed evidence file was accepted.')
print('Evidence verification accepts intact files and rejects same-size corruption.')
