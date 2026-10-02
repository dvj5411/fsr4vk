#!/usr/bin/env python3
"""Package the normal provider and matching OptiScaler; never bundle debug."""
import argparse
from pathlib import Path
import zipfile
from pe_resources import PEImage

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--provider', type=Path, required=True)
parser.add_argument('--optiscaler', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
files = {
    'OptiScaler/amd_fidelityfx_upscaler_vk.dll': args.provider,
    'OptiScaler.dll': args.optiscaler,
    'readme.txt': root / 'readme.txt',
    'LICENSES/AMD-FidelityFX-SDK-MIT.md': root / 'LICENSES/AMD-FidelityFX-SDK-MIT.md',
    'LICENSES/GPL-3.0.txt': root / 'LICENSES/GPL-3.0.txt',
    'LICENSES/Zstandard-BSD.txt': root / 'LICENSES/Zstandard-BSD.txt',
}
for name, path in files.items():
    if not path.is_file():
        parser.error(f'missing input for {name}: {path}')
expected_provider = {'ffxCreateContext', 'ffxDestroyContext', 'ffxConfigure', 'ffxQuery', 'ffxDispatch'}
for path, expected in ((args.provider, expected_provider),
                       (args.optiscaler, {'CreateDXGIFactory', 'CreateDXGIFactory1', 'CreateDXGIFactory2'})):
    try:
        image = PEImage(path.read_bytes())
        missing = expected - set(image.exports())
        if missing:
            raise ValueError('missing required exports: ' + ', '.join(sorted(missing)))
    except ValueError as error:
        parser.error(f'invalid AMD64 Windows DLL {path}: {error}')
args.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.output, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name, path in files.items():
        archive.write(path, name)
with zipfile.ZipFile(args.output) as archive:
    if len(archive.namelist()) != len(files) or set(archive.namelist()) != set(files):
        raise RuntimeError('unexpected or duplicated ZIP entries')
    corrupt = archive.testzip()
    if corrupt is not None:
        raise RuntimeError('ZIP integrity failure: ' + corrupt)
    for name, path in files.items():
        if archive.read(name) != path.read_bytes():
            raise RuntimeError('packaged bytes differ: ' + name)
print(args.output)
