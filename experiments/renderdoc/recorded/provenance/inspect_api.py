from pathlib import Path
import os
import renderdoc as rd

out = Path('/home/tonumi/Desktop/CGTermProject/docs/verification/renderdoc-captures/python-api.txt')
with out.open('w') as f:
    for owner, names in [(rd, ['OpenCaptureFile']), (rd.CaptureFile, ['OpenFile', 'OpenCapture']),
                         (rd.ReplayController, ['GetRootActions', 'GetBufferData', 'GetBuffers',
                                               'SetFrameEvent', 'GetPipelineState', 'SaveTexture'])]:
        for name in names:
            f.write(name + '\n' + str(getattr(owner, name).__doc__) + '\n\n')
os._exit(0)
