#!/usr/bin/env python3
"""Copy owned compiler inputs and assets into a fresh, hash-bound build stage."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('stage', type=Path, help='existing empty directory made by mktemp')
    parser.add_argument('--assets', type=Path, help='general/ directory and its sibling bundles')
    parser.add_argument('--vulkan-include', type=Path, help='copy SDK include files instead of compiling against live headers')
    parser.add_argument('--tool', action='append', default=[], help='record name=executable version and binary hash; not a hermetic sysroot')
    args = parser.parse_args()
    root = args.source.resolve()
    stage = args.stage.resolve()
    if not stage.is_dir() or any(stage.iterdir()):
        parser.error('stage must be an existing empty directory')
    paths = []
    for directory in ('native', 'provider', 'probes', 'third_party/zstd',
                      'amd-fidelityfx-sdk/Kits/FidelityFX/api/include',
                      'amd-fidelityfx-sdk/Kits/FidelityFX/upscalers/include', 'tools'):
        base = root/directory
        paths.extend(path for path in base.rglob('*') if path.is_file() and
                     (path.suffix in ('.cpp', '.hpp', '.h', '.c', '.inl', '.py', '.comp', '.sh') or path.name == 'LICENSE') and
                     '__pycache__' not in path.parts)
    source_status=subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'], text=True).splitlines()
    report = dict(schema=1, source_commit=subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
                  source_dirty=bool(source_status), source_status=source_status, files={})
    for path in sorted(set(paths)):
        relative = path.relative_to(root)
        target = stage/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        data = path.read_bytes()
        target.write_bytes(data)
        report['files'][relative.as_posix()] = hashlib.sha256(data).hexdigest()
    if args.vulkan_include:
        include = args.vulkan_include.resolve()
        if not (include/'vulkan/vulkan.h').is_file():
            parser.error('--vulkan-include must contain vulkan/vulkan.h')
        shutil.copytree(include, stage/'sdk-include', symlinks=False)
        for path in sorted((stage/'sdk-include').rglob('*')):
            if path.is_file():
                report['files'][path.relative_to(stage).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    report['toolchain'] = {}
    for item in args.tool:
        name, separator, executable = item.partition('=')
        resolved = shutil.which(executable)
        if not separator or not name or name in report['toolchain'] or not resolved:
            parser.error('each --tool needs a unique name=existing-executable')
        binary = Path(resolved).resolve()
        version = subprocess.check_output([str(binary), '--version'], stderr=subprocess.STDOUT, text=True, timeout=15)
        report['toolchain'][name] = dict(path=str(binary), version=version.splitlines()[0],
                                       sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    if args.assets:
        asset_root = args.assets.resolve().parent
        if args.assets.name != 'general':
            parser.error('--assets must name general/ with portable/colors/fsr411 siblings')
        for name in ('general', 'portable', 'colors', 'fsr411'):
            # A copied symlink would keep a live dependency outside the stage.
            shutil.copytree(asset_root/name, stage/'assets'/name, symlinks=False)
        for path in sorted((stage/'assets').rglob('*')):
            if path.is_file():
                report['files'][path.relative_to(stage).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    (stage/'frozen-inputs.json').write_text(json.dumps(report, indent=2, sort_keys=True)+'\n')
    print(f'Frozen {len(report["files"])} compiler/asset inputs at {stage}')


if __name__ == '__main__':
    main()
