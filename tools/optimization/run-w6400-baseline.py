#!/usr/bin/env python3
"""Bounded isolated current-source W6400 baseline. Never touches game installs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import threading
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--executable', default='baseline')
    parser.add_argument('--assets', type=Path, help='isolated asset root (defaults to ROOT/assets)')
    parser.add_argument('--label', default='baseline')
    parser.add_argument('--trials', type=int, default=3, choices=range(1, 4))
    parser.add_argument('--cases', nargs='+', choices=('1080', '1440'), default=['1080', '1440'])
    parser.add_argument('--model', choices=('4.0.2', '4.1.1'), default='4.0.2')
    parser.add_argument('--drm-device', type=Path, default=Path('/sys/class/drm/card1/device'))
    parser.add_argument('--expected-pci-vendor', default='0x1002')
    parser.add_argument('--expected-pci-device', default='0x7422')
    args = parser.parse_args()
    root = args.root.resolve()
    assets = args.assets.resolve() if args.assets else root/'assets'
    output = root / (args.label + '-runs')
    output.mkdir()  # Never overwrite an earlier experiment.
    base = args.drm_device.resolve()
    if (base/'vendor').read_text().strip() != args.expected_pci_vendor or (base/'device').read_text().strip() != args.expected_pci_device:
        raise RuntimeError('DRM device does not match requested PCI identity')
    temperature_nodes=sorted((base/'hwmon').glob('hwmon*/temp1_input'))
    exe = root / args.executable
    deadline = time.monotonic() + 480
    cases = [('1080', 1280, 720, 1920, 1080), ('1440', 1706, 960, 2560, 1440)]
    cases = [case for case in cases if case[0] in args.cases]
    results = []
    for trial in range(args.trials):
        for name, rw, rh, ow, oh in (cases if trial % 2 == 0 else cases[::-1]):
            for game in ('NMS.exe', 'RDR2.exe', 'bg3.exe', 'bg3_dx11.exe', 'DOOMTheDarkAges.exe', 'Endfield.exe', 'enshrouded.exe'):
                match = subprocess.run(['pgrep', '-f', r'(^|[/\\])' + re.escape(game) + r'([\s]|$)'], stdout=subprocess.DEVNULL)
                if match.returncode != 1:
                    raise RuntimeError('game running or process check failed')
            if time.monotonic() >= deadline:
                raise RuntimeError('batch time budget reached')
            key = f'{name}-{trial}'
            image = output/(key+'.rgba16f')
            bundle = assets/'general'/('1080' if name == '1080' else '2160')/'quality'
            env = {k:v for k,v in os.environ.items() if not k.startswith(('FSR4_', 'ACO_', 'RADV_'))}
            env.update(FSR4_TEST_PROFILE_FRAMES='48', FSR4_TEST_RENDER_SIZE=f'{rw}x{rh}',
                       FSR4_TEST_VERSION=args.model,
                       FSR4_TEST_OUTPUT_SIZE=f'{ow}x{oh}', FSR4_TEST_PRESET='auto',
                       FSR4_VK_ASSET_ROOT=str(assets), FSR4_VK_PROFILE_PATH=str(output/(key+'.csv')),
                       FSR4_VK_LOG_PATH=str(output/(key+'.provider.log')), VK_LOADER_LAYERS_DISABLE='~implicit~')
            telemetry = []
            stop = threading.Event()
            def sample():
                while not stop.is_set():
                    row = {'elapsed_s': time.monotonic()-start}
                    for file in ('pp_dpm_sclk', 'gpu_busy_percent', 'mem_info_vram_used'):
                        try:
                            row[file] = (base/file).read_text().strip()
                        except OSError:
                            row[file] = None
                    row['temperature_millidegrees'] = []
                    for node in temperature_nodes:
                        try: row['temperature_millidegrees'].append(node.read_text().strip())
                        except OSError: row['temperature_millidegrees'].append(None)
                    telemetry.append(row)
                    stop.wait(0.1)
            start = time.monotonic()
            thread = threading.Thread(target=sample)
            thread.start()
            try:
                with (output/(key+'.stdout')).open('x') as out, (output/(key+'.stderr')).open('x') as err:
                    run = subprocess.run([str(exe), str(bundle), str(bundle/'initializers.bin'),
                                          str(bundle/'weights.bin'), str(image), '--provider-temporal'],
                                         cwd=root, env=env, stdout=out, stderr=err,
                                         timeout=min(240, max(1, deadline-time.monotonic())))
            finally:
                stop.set()
                thread.join()
                (output/(key+'.telemetry.json')).write_text(json.dumps(telemetry, indent=2)+'\n')
            text = (output/(key+'.stdout')).read_text()
            if run.returncode or 'Radeon Pro W6400' not in text or 'context_frames=48' not in text or 'nonfinite=0' not in text:
                raise RuntimeError(f'{key} failed; see retained logs')
            rows = [json.loads(line) for line in Path(str(image)+'.timings.jsonl').read_text().splitlines()]
            if [r['frame'] for r in rows] != list(range(48)):
                raise RuntimeError('incomplete frame series')
            kept = rows[8:]
            record = dict(case=key, executable_sha256=sha(exe), frames=48, warmup=8, model=args.model,
                          drm_device=str(base), pci_vendor=args.expected_pci_vendor, pci_device=args.expected_pci_device,
                          asset_root=str(assets),
                          context_median_ms=statistics.median(r['context_gpu_ms'] for r in kept),
                          context_min_ms=min(r['context_gpu_ms'] for r in kept),
                          context_max_ms=max(r['context_gpu_ms'] for r in kept),
                          passes={p:statistics.median(r['passes'][p] for r in kept) for p in kept[0]['passes']},
                          image_sha256=sha(image), bundle_manifest_sha256=sha(bundle/'manifest.json'),
                          elapsed_s=time.monotonic()-start)
            results.append(record)
            (output/'summary.json').write_text(json.dumps(results, indent=2)+'\n')
            print(json.dumps(record), flush=True)


if __name__ == '__main__':
    main()
