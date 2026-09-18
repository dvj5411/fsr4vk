#!/usr/bin/env python3
"""Package a provider and custom OptiScaler fallback for game-root extraction."""
import argparse
from pathlib import Path
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--provider', type=Path, required=True)
parser.add_argument('--optiscaler', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
files = {
    'OptiScaler/amd_fidelityfx_upscaler_vk.dll': args.provider,
    'OptiScaler_fallback.dll': args.optiscaler,
    'readme.txt': root / 'release/readme.txt',
    'LICENSES/AMD-FidelityFX-SDK-MIT.md': root / 'LICENSES/AMD-FidelityFX-SDK-MIT.md',
    'LICENSES/OptiScaler-GPL-3.0.txt': root / 'LICENSES/OptiScaler-GPL-3.0.txt',
}
for name, path in files.items():
    if not path.is_file():
        parser.error(f'missing input for {name}: {path}')
for path in (args.provider, args.optiscaler):
    with path.open('rb') as stream:
        if stream.read(2) != b'MZ':
            parser.error(f'not a Windows DLL: {path}')
args.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.output, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name, path in files.items():
        archive.write(path, name)
with zipfile.ZipFile(args.output) as archive:
    assert set(archive.namelist()) == set(files)
    assert archive.testzip() is None
    for name, path in files.items():
        assert archive.read(name) == path.read_bytes(), name
print(args.output)
