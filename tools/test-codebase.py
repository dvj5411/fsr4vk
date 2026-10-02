#!/usr/bin/env python3
"""One owned CPU gate. No game launches, GPU jobs, downloads, or releases."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vulkan-include', type=Path, required=True)
    parser.add_argument('--opti-reader', type=Path, required=True)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--mingw-cxx', help='optional Windows cross compiler for the Windows-only external-DLL harness')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--no-sanitizers', action='store_true')
    args = parser.parse_args()
    build = ROOT/'build'
    build.mkdir(exist_ok=True)
    output = args.output or Path(tempfile.mkdtemp(prefix='cpu-gate-', dir=build))
    output.mkdir(parents=True, exist_ok=True)
    if (output/'results.json').exists():
        parser.error('output already contains results; refusing overwrite')
    if not (args.vulkan_include/'vulkan/vulkan.h').is_file():
        parser.error('--vulkan-include must contain vulkan/vulkan.h')
    for name in ('ffx_vk_preset_query.h', 'ffx_vk_create_device_features.h'):
        if (ROOT/'provider'/name).read_bytes() != (args.opti_reader/name).read_bytes():
            parser.error(f'provider/host ABI header mismatch: {name}')
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', CXX=args.cxx)
    results = []

    def run(label, command):
        print(label, flush=True)
        start = time.monotonic()
        task = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, timeout=300)
        (output/(label+'.log')).write_text(task.stdout)
        results.append(dict(check=label, command=list(map(str, command)), exit_code=task.returncode,
                            seconds=time.monotonic()-start))
        if task.returncode:
            print(task.stdout, flush=True)
            raise RuntimeError(f'{label} failed; see {output}')

    tests = ('buffer-memory-policy', 'color-space', 'resolution-plan', 'vulkan-compat',
             'fsr4-device-features', 'provider-diagnostic-options', 'provider-diagnostics',
             'descriptor-layout', 'resource-adoption', 'enabled-device-state', 'timestamp-policy', 'host-input-read', 'preset-reporting')
    common = [args.cxx, '-std=c++20', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra',
              '-Wno-missing-field-initializers', '-ffunction-sections', '-fdata-sections',
              '-I'+str(args.vulkan_include), '-I'+str(args.opti_reader)]
    if not args.no_sanitizers:
        common += ['-fsanitize=address,undefined,float-cast-overflow', '-fno-sanitize-recover=all']
    linker = ['-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections']
    try:
        for name in tests:
            binary = output/name
            run(name+'-build', common+[str(ROOT/'native'/(name+'-test.cpp')), *linker, '-o', str(binary)])
            run(name, [str(binary)])
        for flags, label in (([], 'python-gates'), (['-O'], 'python-gates-optimized')):
            run(label, [sys.executable, *flags, '-m', 'unittest', 'discover', '-s', 'tools', '-p', 'test_hardening.py', '-v'])
        run('optimization', [sys.executable, '-m', 'unittest', 'discover', '-s', 'tools/optimization', '-p', 'test_*.py', '-v'])
        generated = []
        for name, script, flags in (
                ('memory', 'prepare_memory_placement.py', []),
                ('memory-cpu', 'prepare_memory_placement.py', ['--cpu-timing']),
                ('memory-cached', 'prepare_memory_placement.py', ['--cpu-timing', '--cache-readback']),
                ('wave', 'prepare_wave_size.py', [])):
            source = output/(name+'.cpp')
            run(name+'-generate', [sys.executable, 'tools/optimization/'+script,
                                  str(ROOT/'native/dispatch-smoke.cpp'), str(source), *flags])
            generated.append((name, source))
        compile_generated = [args.cxx, '-std=c++20', '-O0', '-I'+str(args.vulkan_include),
                             '-I'+str(ROOT/'native'), '-I'+str(ROOT/'tools/optimization'), '-c']
        for name, source in generated:
            run(name+'-compile', [*compile_generated, str(source), '-o', str(output/(name+'.o'))])
        # The cached-readback generator instruments both compile-time collectors.
        if args.mingw_cxx or platform.system() == 'Windows':
            external_compile = [args.mingw_cxx or args.cxx, *compile_generated[1:]]
            run('memory-cached-external-compile', [*external_compile, '-DFSR4_EXTERNAL_PROVIDER',
                        str(output/'memory-cached.cpp'), '-o', str(output/'memory-cached-external.o')])
        else:
            results.append(dict(check='memory-cached-external-compile', skipped=True,
                                reason='Windows-only harness; supply --mingw-cxx (Linux CI does)', exit_code=None))
        run('reference-inputs', [sys.executable, 'tools/check-reference-inputs.py', '--cxx', args.cxx])
        run('asset-codec', [sys.executable, 'tools/test-asset-codec.py', '--output', str(output/'asset-codec')])
    finally:
        source_hashes = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in [ROOT/'provider/ffx_vk_provider.cpp', ROOT/'native/quality-context.hpp',
                                      *sorted((ROOT/'native').glob('*-test.cpp'))]}
        (output/'results.json').write_text(json.dumps(dict(platform=platform.platform(),
            sanitizer=not args.no_sanitizers, source_sha256=source_hashes, results=results), indent=2)+'\n')
    print(f'PASS: CPU evidence in {output}')


if __name__ == '__main__':
    main()
