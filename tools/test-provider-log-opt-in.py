#!/usr/bin/env python3
"""Verify embedded Windows provider logging with the isolated context probe.

Arguments: test-root output-name. Requires vulkan-device-negotiation-probe.exe and the provider
DLL in test-root, plus explicit WINEPREFIX and PROTONPATH environment variables.
No game prefix or game installation should be used.
"""
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
out = root / sys.argv[2]
out.mkdir(exist_ok=False)
prefix = Path(os.environ['WINEPREFIX'])
assert os.environ.get('PROTONPATH')
win = lambda p: 'Z:' + str(p).replace('/', '\\')
def snapshot():
    return {str(p): (p.stat().st_size, p.stat().st_mtime_ns)
            for p in (prefix / 'drive_c/users').glob('*/AppData/Local/Temp/fsr4vk-provider-*.log')}

cases = [('default-off', None, None, False), ('zero-off', '0', None, False),
         ('enabled', '1', None, True), ('empty-path-enabled', '1', '', True),
         ('explicit-path', None, 'file', False), ('explicit-path-with-zero', '0', 'file', False)]
results = []
for name, enabled, path, temp_expected in cases:
    env = os.environ.copy()
    for key in ('FSR4_VK_LOG', 'FSR4_VK_LOG_PATH', 'FSR4_VK_ASSET_ROOT'):
        env.pop(key, None)
    if enabled is not None:
        env['FSR4_VK_LOG'] = enabled
    diagnostic = out / (name + '.provider.log')
    if path is not None:
        env['FSR4_VK_LOG_PATH'] = win(diagnostic) if path == 'file' else path
    env.update(GAMEID='umu-default', STORE='none', PROTONFIXES_DISABLE='1',
               UMU_RUNTIME_UPDATE='0', WINEDEBUG='-all', VK_LOADER_LAYERS_DISABLE='~implicit~')
    log = out / (name + '.log')
    args = [win(root / 'vulkan-device-negotiation-probe.exe'),
            win(root / 'amd_fidelityfx_upscaler_vk.dll'), '--rdr2-context']
    before = snapshot()
    with (out / (name + '.umu.log')).open('w') as stream:
        proc = subprocess.run(['umu-run', 'cmd.exe', '/d', '/c',
                               subprocess.list2cmdline(args) + ' > ' + subprocess.list2cmdline([win(log)]) + ' 2>&1'],
                              cwd=root, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=60)
    after = snapshot()
    changed = [p for p, state in after.items() if before.get(p) != state]
    text = log.read_text(errors='replace') if log.exists() else ''
    passed = (proc.returncode == 0 and 'rdr2_context_flags=9 external_exposure=true negotiated=1 result=0' in text and
              bool(changed) == temp_expected and diagnostic.exists() == (path == 'file'))
    if diagnostic.exists():
        passed &= 'context created flags=9' in diagnostic.read_text(errors='replace')
    entry = dict(case=name, passed=passed, changed_temp_logs=changed, explicit_log=diagnostic.exists())
    results.append(entry)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(entry), flush=True)
    if not passed:
        raise SystemExit(1)
