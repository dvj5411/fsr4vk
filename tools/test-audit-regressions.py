#!/usr/bin/env python3
"""Isolated two-frame GPU accuracy checks against an explicit pre-fix control.

No game installation/prefix changes. These are not performance benchmarks or
original-DLL recaptures. Evidence binds every executable and output hash.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def profile_streams(root, key):
    """Select provider streams, not the harness's separate readback timings."""
    pattern = re.compile(re.escape(key) + r'\.\d+\.\d+\.jsonl')
    return sorted(path for path in root.iterdir() if pattern.fullmatch(path.name))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--metrics', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--backend', choices=('native-mixed-dot', 'portable-int8'), required=True,
                        help='expected 4.0.2 backend; 4.1.1 is always portable')
    parser.add_argument('--exposure-poison', action='store_true')
    parser.add_argument('--group', choices=('standard', 'resolutions', 'exposure', 'profiling', 'border'), default='standard')
    parser.add_argument('--windows-proton', type=Path, help='optional Proton runtime for the candidate PE only')
    parser.add_argument('--wine-prefix', type=Path, help='existing isolated test prefix, never a game prefix')
    parser.add_argument('--windows-dll', type=Path, help='DLL copied next to the candidate in a fresh owned staging directory')
    args = parser.parse_args()
    if bool(args.windows_proton) != bool(args.wine_prefix) or bool(args.windows_proton) != bool(args.windows_dll):
        parser.error('Windows tests require --windows-proton, --wine-prefix and --windows-dll together')
    if args.windows_proton and args.group == 'border':
        parser.error('border invariance uses two runs of the native candidate; keep Windows pairing separate')
    args.output.mkdir(parents=True, exist_ok=False)
    env_base = {key: value for key, value in os.environ.items()
                if not key.startswith(('FSR4_', 'ACO_', 'RADV_', 'VKD3D_', 'VK_INSTANCE_'))}
    env_base.update(VK_LOADER_LAYERS_DISABLE='~implicit~', VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',
                    VK_LAYER_PATH=str(args.sdk/'share/vulkan/explicit_layer.d'),
                    LD_LIBRARY_PATH=str(args.sdk/'lib'), FSR4_VK_ASSET_ROOT=str(args.assets))
    report = dict(schema=1, scope='accuracy-not-performance', baseline_sha256=sha(args.baseline),
                  candidate_sha256=sha(args.candidate), expected_402_backend=args.backend, cases=[])
    windows_candidate = None
    win = lambda path: 'Z:' + str(path.resolve()).replace('/', '\\')
    if args.windows_proton:
        stage = args.output/'windows-probe'
        stage.mkdir()
        windows_candidate = stage/args.candidate.name
        shutil.copyfile(args.candidate, windows_candidate)
        shutil.copyfile(args.windows_dll, stage/'amd_fidelityfx_upscaler_vk.dll')
        report.update(windows_dll_sha256=sha(args.windows_dll), proton_path=str(args.windows_proton),
                      wine_prefix=str(args.wine_prefix), candidate_platform='Windows-Proton')

    def run(name, executable, version, size, extra, mode='--provider-temporal', windows=False):
        image = args.output/(name+'.rgba16f')
        log = args.output/(name+'.log')
        diagnostic = args.output/(name+'.provider.log')
        env = dict(env_base, FSR4_TEST_VERSION=version, FSR4_TEST_RENDER_SIZE=size[0], FSR4_TEST_OUTPUT_SIZE=size[1],
                   FSR4_VK_LOG_PATH=str(diagnostic), **extra)
        bundle = args.assets/'general'/('2160' if int(size[1].split('x')[0])>1920 or int(size[1].split('x')[1])>1080 else '1080')/'quality'
        command = [str(executable), str(bundle), str(bundle/'initializers.bin'), str(bundle/'weights.bin'), str(image), mode]
        if windows:
            env.pop('FSR4_VK_ASSET_ROOT', None)
            env.update(WINEPREFIX=str(args.wine_prefix), PROTONPATH=str(args.windows_proton), GAMEID='umu-default',
                       STORE='none', PROTONFIXES_DISABLE='1', UMU_RUNTIME_UPDATE='0', WINEDEBUG='-all',
                       FSR4_VK_LOG_PATH=win(diagnostic))
            command = [win(Path(part)) for part in command[:-1]] + [mode]
            batch = args.output/(name+'.cmd')
            with batch.open('x') as stream:
                stream.write('@echo off\n' + subprocess.list2cmdline(command) + ' > ' +
                             subprocess.list2cmdline([win(log)]) + ' 2>&1\nexit /b %errorlevel%\n')
            with (args.output/(name+'.umu.log')).open('x') as stream:
                result = subprocess.run(['umu-run', 'cmd.exe', '/d', '/c', win(batch)], env=env,
                                        cwd=executable.parent, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        else:
            with log.open('x') as stream:
                result = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        text = log.read_text(errors='replace')
        trace = diagnostic.read_text(errors='replace') if diagnostic.exists() else ''
        backend = 'portable-int8' if version == '4.1.1' else args.backend
        frames = int(extra.get('FSR4_TEST_PROFILE_FRAMES', '2'))
        if result.returncode or 'VUID-' in text or 'Validation Error' in text or f'context_frames={frames}' not in text or 'shader_backend='+backend not in trace:
            raise RuntimeError(f'{name} failed; inspect {log} and provider log')
        outputs = [Path(str(image)+'.frame0.rgba16f'), image] if frames == 2 else [image]
        return outputs, trace

    if args.group == 'standard':
        cases = [('quality', ('1280x720', '1920x1080'), {}),
                 ('balanced', ('1130x636', '1920x1080'), {}),
                 ('performance', ('960x540', '1920x1080'), {}),
                 ('ultraperf', ('640x360', '1920x1080'), {}),
                 ('fsraa', ('1920x1080', '1920x1080'), {}),
                 ('1440', ('1706x960', '2560x1440'), {}),
                 ('4k', ('1920x1080', '3840x2160'), {}),
                 ('random-odd', ('877x493', '1317x741'), {}),
                 ('small', ('64x64', '128x128'), {}),
                 ('drs', ('1280x720', '1920x1080'), {'FSR4_TEST_DYNAMIC_RESOLUTION':'1','FSR4_TEST_SECOND_RENDER_SIZE':'960x540'}),
                 *[(color, ('1280x720','1920x1080'), {'FSR4_TEST_COLOR_SPACE':color}) for color in ('nonlinear','srgb','pq')],
                 ('color-switch', ('1280x720','1920x1080'), {'FSR4_TEST_COLOR_SWITCH':'1'}),
                 ('snorm', ('1280x720','1920x1080'), {'FSR4_TEST_DEPTH_ONLY':'1','FSR4_TEST_MOTION_STORAGE':'snorm16',
                                                       'FSR4_TEST_SNORM_PATTERN':'16','FSR4_TEST_NATIVE_INPUTS':'1'}),
                 ('api11', ('1280x720','1920x1080'), {'FSR4_TEST_VULKAN_1_1':'1'})]
    elif args.group == 'resolutions':
        cases = [(f'{height}-{preset}', (f'{round(width/factor)}x{round(height/factor)}', f'{width}x{height}'), {})
                 for width, height in ((1280,720), (1920,1080), (2560,1440), (3840,2160))
                 for preset, factor in (('quality',1.5), ('balanced',1.7), ('performance',2), ('ultraperf',3), ('fsraa',1))]
    elif args.group == 'border':
        cases = [(name, size, {'FSR4_TEST_OUTPUT_POISON':'2'}) for name, size in
                 (('even',('1280x720','1920x1080')), ('odd',('877x493','1317x741')),
                  ('width-odd',('877x493','1317x740')), ('height-odd',('877x493','1316x741')),
                  ('odd-fsraa',('1317x741','1317x741')), ('asymmetric',('900x500','1317x741')),
                  ('smallest',('64x64','64x64')), ('small-odd',('64x64','65x65')))]
        report.update(scope='output-border-invariance', baseline_sha256=sha(args.candidate))
    elif args.group == 'exposure':
        cases = [(name+'-'+bits, ('1280x720','1920x1080'), dict(FSR4_TEST_EXTERNAL_EXPOSURE=str(value),
                        FSR4_TEST_SECOND_EXPOSURE='4', **({'FSR4_TEST_R16_EXPOSURE':'1'} if bits=='r16' else {})))
                 for name, value in (('unit',1),('quarter',.25),('zero',0)) for bits in ('r16','r32')]
        if args.exposure_poison:
            cases += [(name+'-'+bits, ('1280x720','1920x1080'), dict(FSR4_TEST_EXTERNAL_EXPOSURE=str(value),
                        FSR4_TEST_SECOND_EXPOSURE='4', FSR4_TEST_EXPOSURE_POISON='1',
                        **({'FSR4_TEST_R16_EXPOSURE':'1'} if bits=='r16' else {})))
                      for name, value in (('negative',-1),('nan','nan'),('infinity','inf')) for bits in ('r16','r32')]
    else:
        cases = [('profile-series', ('1280x720','1920x1080'), {'FSR4_TEST_PROFILE_FRAMES':'24'})]
    try:
        for version in ('4.0.2','4.1.1'):
            for name, size, extra in cases:
                key = version+'-'+name
                control_extra = extra.copy()
                if 'FSR4_TEST_EXPOSURE_POISON' in extra:
                    control_extra.pop('FSR4_TEST_EXPOSURE_POISON')
                    control_extra['FSR4_TEST_EXTERNAL_EXPOSURE']='1'
                candidate_extra = extra.copy()
                if args.group == 'profiling':
                    control_extra['FSR4_VK_PROFILE_PATH']=str(args.output/(key+'-old.csv'))
                    candidate_extra['FSR4_VK_PROFILE_PATH']=str(args.output/(key+'-new'))
                if args.group == 'border':
                    control_extra['FSR4_TEST_OUTPUT_POISON']='1'
                baseline, _ = run(key+'-old', args.candidate if args.group == 'border' else args.baseline, version, size, control_extra)
                candidate, trace = run(key+'-new', windows_candidate or args.candidate, version, size, candidate_extra,
                                       windows=bool(windows_candidate))
                metrics = []
                passed = True
                for frame, (ref, image) in enumerate(zip(baseline, candidate)):
                    if ref.stat().st_size != image.stat().st_size:
                        raise ValueError('different output dimensions')
                    metric = json.loads(subprocess.check_output([str(args.metrics), str(ref), str(image)], timeout=60))
                    if metric['nonfinite_pairs'] or metric['rmse']>.002 or metric['above_tolerance_fraction']>.05:
                        if args.group != 'border':
                            raise ValueError(f'{key} frame {frame} threshold failed: {metric}')
                        passed = False
                    metric.update(frame=frame, reference_sha256=sha(ref), candidate_sha256=sha(image))
                    if args.group == 'border':
                        width, height = size[1].split('x')
                        border = json.loads(subprocess.check_output(['python3', str(Path(__file__).with_name('rgba16f-border-metrics.py')),
                                    str(ref), str(image), '--width', width, '--height', height], timeout=60))
                        metric['border']=border
                        if border['nonfinite_pairs'] or border['max_abs_error'] != 0:
                            passed = False
                    metrics.append(metric)
                if args.group == 'profiling':
                    streams = profile_streams(args.output, key+'-new')
                    if len(streams)!=1: raise ValueError('per-core profile identity missing/overwritten')
                    rows = [json.loads(line) for line in streams[0].read_text().splitlines()]
                    expected_passes = 28 if version=='4.1.1' else 15
                    if [row['frame'] for row in rows]!=list(range(24)) or any(not row['available'] or len(row['passes'])!=expected_passes for row in rows):
                        raise ValueError('incomplete/invalid retired timestamp series')
                report['cases'].append(dict(case=key, metrics=metrics, passed=passed))
                print(key, 'PASS' if passed else 'FAIL: output depends on initialization', flush=True)
        if any(not case['passed'] for case in report['cases']):
            raise RuntimeError('output-border invariance failed; retained all cases in report.json, do not treat as a passing gate')
    finally:
        (args.output/'report.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')


if __name__ == '__main__':
    main()
