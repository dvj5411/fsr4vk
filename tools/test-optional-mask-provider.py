#!/usr/bin/env python3
"""Run the Windows DLL mask-compatibility regression on an isolated Proton prefix.

Arguments: test-root output-name [--expect-rejection]. Requires assets/,
preset-dispatch-smoke.exe, rgba16f-metrics and amd_fidelityfx_upscaler_vk.dll in test-root.
Set WINEPREFIX and PROTONPATH explicitly; never use a game prefix.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
out = root / sys.argv[2]
out.mkdir(exist_ok=False)
reject = len(sys.argv) == 4 and sys.argv[3] == '--expect-rejection'
assert os.environ.get('WINEPREFIX') and os.environ.get('PROTONPATH')
bundle = root / 'assets/general/1080/quality'
win = lambda p: 'Z:' + str(p).replace('/', '\\')
results = []
reference = {}
for exposure in ('auto', 'external'):
    for variant in (('both',) if reject else ('none', 'none-repeat', 'reactive', 'transparency', 'both')):
        masks = 'none' if variant == 'none-repeat' else variant
        name = f'{exposure}-{variant}'
        log, diag, image = (out / (name + suffix) for suffix in ('.log', '.provider.log', '.rgba16f'))
        env = os.environ.copy()
        env.pop('FSR4_VK_ASSET_ROOT', None)
        env.pop('FSR4_TEST_EXTERNAL_EXPOSURE', None)
        env.update(FSR4_TEST_OPTIONAL_MASKS=masks, FSR4_TEST_VULKAN_1_1='1',
                   FSR4_TEST_PRESET='auto', FSR4_VK_LOG_PATH=win(diag),
                   GAMEID='umu-default', STORE='none', PROTONFIXES_DISABLE='1',
                   UMU_RUNTIME_UPDATE='0', WINEDEBUG='-all', VK_LOADER_LAYERS_DISABLE='~implicit~')
        if exposure == 'external':
            env['FSR4_TEST_EXTERNAL_EXPOSURE'] = '1'
        args = [win(root / 'preset-dispatch-smoke.exe'), win(bundle),
                win(bundle / 'initializers.bin'), win(bundle / 'weights.bin'), win(image), '--provider-temporal']
        command = ['umu-run', 'cmd.exe', '/d', '/c',
                   subprocess.list2cmdline(args) + ' > ' + subprocess.list2cmdline([win(log)]) + ' 2>&1']
        with (out / (name + '.umu.log')).open('w') as stream:
            proc = subprocess.run(command, cwd=root, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=90)
        text = log.read_text(errors='replace') if log.exists() else ''
        diagnostic = diag.read_text(errors='replace') if diag.exists() else ''
        entry = dict(case=name, exit=proc.returncode)
        if reject:
            passed = 'optional masks are unsupported' in diagnostic and 'provider dispatch failed' in text
        else:
            frames = [image, Path(str(image) + '.frame0.rgba16f')]
            hashes = [hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None for p in frames]
            if variant == 'none':
                reference[exposure] = frames
            entry['frame_sha256'] = hashes
            metrics = [json.loads(subprocess.check_output([str(root / 'rgba16f-metrics'), str(a), str(b)], text=True))
                       for a, b in zip(reference[exposure], frames)] if all(hashes) else []
            entry['frame_metrics'] = metrics
            # Existing project numerical gates, not a byte-exact promise for
            # independent native mixed-dot GPU runs. Include a no-mask repeat.
            numerical_pass = len(metrics) == 2 and all(m['nonfinite_pairs'] == 0 and m['rmse'] <= .002
                                      and m['above_tolerance_fraction'] <= .05 for m in metrics)
            warning_count = diagnostic.count('ignores optional reactive/transparency masks')
            entry['mask_warning_count'] = warning_count
            passed = (proc.returncode == 0 and 'cleanup=complete' in text and 'nonfinite=0' in text
                      and text.count('preset_frame=') == 2 and all(hashes)
                      and numerical_pass and 'VUID-' not in text
                      and 'Validation Error' not in text and 'first dispatch recorded' in diagnostic
                      and warning_count == (0 if masks == 'none' else 1)
                      and (masks == 'none' or text.count('dispatch=accepted') == 2))
        entry['passed'] = passed
        results.append(entry)
        (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(json.dumps(entry), flush=True)
        if not passed:
            raise SystemExit(1)
