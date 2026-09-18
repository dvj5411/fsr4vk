#!/usr/bin/env python3
"""Verify embedded Windows provider logging with the isolated context probe.

Arguments: test-root output-name. Requires vulkan-device-negotiation-probe.exe and the provider
DLL in test-root, plus explicit WINEPREFIX and PROTONPATH environment variables.
No game prefix or game installation should be used.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
out = root / sys.argv[2]
out.mkdir(exist_ok=False)
prefix = Path(os.environ['WINEPREFIX'])
assert os.environ.get('PROTONPATH')
win = lambda p: 'Z:' + str(p).replace('/', '\\')
def temp_snapshot():
    return {str(p): (p.stat().st_size, p.stat().st_mtime_ns)
            for p in (prefix / 'drive_c/users').glob('*/AppData/Local/Temp/fsr4vk-provider-*.log')}

def directory_snapshot(directory):
    return {str(p): (p.stat().st_size, p.stat().st_mtime_ns)
            for p in directory.glob('fsr4vk-provider-*.log')}

cases = [('default-off', None, None, 'none'), ('zero-off', '0', None, 'none'),
         ('enabled', '1', None, 'game'), ('empty-path-enabled', '1', '', 'game'),
         ('explicit-path', None, 'file', 'explicit'), ('explicit-path-with-zero', '0', 'file', 'explicit'),
         ('readonly-game-fallback', '1', None, 'temp')]
results = []
for name, enabled, path, expected in cases:
    game=out/name/'game directory'
    working=out/name/'working-directory'
    game.mkdir(parents=True);working.mkdir()
    probe=game/'vulkan-device-negotiation-probe.exe'
    shutil.copyfile(root/'vulkan-device-negotiation-probe.exe',probe)
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
    args = [win(probe),
            win(root / 'amd_fidelityfx_upscaler_vk.dll'), '--rdr2-context']
    # A batch file preserves cmd quoting through Proton/umu's argument layers
    # and redirects the Windows console even when umu does not inherit stdout.
    batch = out / (name + '.cmd')
    batch.write_text('@echo off\n' + subprocess.list2cmdline(args) +
                     ' > ' + subprocess.list2cmdline([win(log)]) + ' 2>&1\nexit /b %errorlevel%\n')
    before = temp_snapshot()
    provider_before=directory_snapshot(root)
    if expected=='temp': game.chmod(0o555)
    try:
        with (out / (name + '.umu.log')).open('w') as stream:
            proc = subprocess.run(['umu-run', 'cmd.exe', '/d', '/c', win(batch)],
                                  cwd=working, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=60)
    finally:
        game.chmod(0o755)
    after = temp_snapshot()
    changed = [p for p, state in after.items() if before.get(p) != state]
    game_logs=list(game.glob('fsr4vk-provider-*.log'))
    text = log.read_text(errors='replace') if log.exists() else ''
    passed = (proc.returncode == 0 and 'rdr2_context_flags=9 external_exposure=true negotiated=1 result=0' in text and
              bool(changed) == (expected=='temp') and diagnostic.exists() == (path == 'file') and
              bool(game_logs) == (expected=='game') and not directory_snapshot(working) and
              directory_snapshot(root)==provider_before)
    if diagnostic.exists():
        passed &= 'context created flags=9' in diagnostic.read_text(errors='replace')
    for log_path in game_logs+[Path(p) for p in changed]:
        passed &= 'context created flags=9' in log_path.read_text(errors='replace')
    entry = dict(case=name, passed=passed, changed_temp_logs=changed,
                 game_logs=[str(p) for p in game_logs],explicit_log=diagnostic.exists())
    results.append(entry)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(entry), flush=True)
    if not passed:
        raise SystemExit(1)
