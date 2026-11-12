from pathlib import Path
import hashlib
import json
import os
import struct
import traceback
import renderdoc as rd

HERE = Path(os.environ['CG_REPLAY_DIR']).resolve()
results = []
code = 1
try:
    command_hashes = []
    for mode in ('per-object', 'grouped'):
        capture = rd.OpenCaptureFile()
        status = capture.OpenFile(str(HERE / f'{mode}_capture.rdc'), '', None)
        assert status == rd.ResultCode.Succeeded, str(status)
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        assert status == rd.ResultCode.Succeeded, str(status)
        try:
            actions = []
            def flatten(items):
                for item in items:
                    actions.append(item)
                    flatten(item.children)
            flatten(controller.GetRootActions())
            chunks = [c.name for c in controller.GetStructuredFile().chunks]
            assert 'vkCmdDispatch' in chunks and 'vkCmdPipelineBarrier2' in chunks
            assert 'vkCmdDrawIndexedIndirect' in chunks
            dispatches = [a for a in actions if a.flags & rd.ActionFlags.Dispatch]
            assert len(dispatches) == 1
            dispatch = dispatches[0]
            assert list(dispatch.dispatchDimension) == ([100, 1, 1] if mode == 'per-object' else [25, 1, 1])
            controller.SetFrameEvent(dispatch.eventId, True)
            buffers = [b for b in controller.GetBuffers() if b.length == 6400 * 20]
            assert len(buffers) == 1
            data = bytes(controller.GetBufferData(buffers[0].resourceId, 0, 0))
            assert len(data) == 6400 * 20
            commands = list(struct.iter_unpack('<IIIiI', data))
            assert all(index_count == 672 and count in (0, 1) and first_index == 0 and offset == 0 and first_instance == i
                       for i, (index_count, count, first_index, offset, first_instance) in enumerate(commands))
            visible = sum(c[1] for c in commands)
            assert visible == 1623
            digest = hashlib.sha256(data).hexdigest()
            command_hashes.append(digest)
            (HERE / f'{mode}-indirect.bin').write_bytes(data)
            draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall]
            assert draws
            controller.SetFrameEvent(draws[-1].eventId, True)
            targets = controller.GetPipelineState().GetOutputTargets()
            assert targets
            target = targets[0]
            resource = target.resource if hasattr(target, 'resource') else target.resourceId
            texture = rd.TextureSave()
            texture.resourceId = resource
            texture.destType = rd.FileType.PNG
            status = controller.SaveTexture(texture, str(HERE / f'{mode}-replay.png'))
            assert status == rd.ResultCode.Succeeded, str(status)
            messages = [dict(severity=str(m.severity), description=m.description) for m in controller.GetDebugMessages()]
            results.append(dict(mode=mode, dispatchEvent=dispatch.eventId,
                                dispatch=list(dispatch.dispatchDimension), totalCommands=len(commands),
                                visible=visible, rejected=len(commands)-visible,
                                commandsSha256=digest, drawActions=len(draws),
                                indirectActions=sum(bool(a.flags & rd.ActionFlags.Indirect) for a in actions),
                                recordedCalls={name: chunks.count(name) for name in
                                               ('vkCmdDispatch', 'vkCmdPipelineBarrier2', 'vkCmdDrawIndexedIndirect')},
                                messages=messages))
        finally:
            controller.Shutdown()
            capture.Shutdown()
    assert len(set(command_hashes)) == 1, 'Methods produced different replayed commands.'
    (HERE / 'replay-results.json').write_text(json.dumps(results, indent=2) + '\n')
    code = 0
except BaseException:
    (HERE / 'replay-error.txt').write_text(traceback.format_exc())
finally:
    os._exit(code)
