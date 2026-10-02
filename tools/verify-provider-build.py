#!/usr/bin/env python3
"""Bind staged provider DLLs to frozen inputs and validated PE resource hashes."""
import argparse
import hashlib
import json
from pathlib import Path
from pe_resources import PEImage
from asset_compression import frame_content_size


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    parser.add_argument('artifacts', type=Path)
    args = parser.parse_args()
    provenance = json.loads((args.stage/'frozen-inputs.json').read_text())
    for path, digest in provenance['files'].items():
        if sha((args.stage/path).read_bytes()) != digest:
            raise ValueError(f'frozen input changed during build: {path}')
    expected = {'ffxCreateContext', 'ffxDestroyContext', 'ffxConfigure', 'ffxQuery', 'ffxDispatch'}
    identities = []
    binaries = {}
    for mode, name in (('release', 'amd_fidelityfx_upscaler_vk.dll'), ('debug', 'amd_fidelityfx_upscaler_vk_debug.dll')):
        data = (args.artifacts/name).read_bytes()
        image = PEImage(data)
        exports = image.exports()
        if not expected <= set(exports):
            raise ValueError('missing provider exports: '+name)
        result = dict(sha256=sha(data), bytes=len(data), machine='AMD64', exports=exports)
        manifest = args.artifacts/(mode+'-embedded')/'embedded-assets.json'
        if manifest.exists():
            metadata = json.loads(manifest.read_text())
            if metadata['compression'] != ('zstd' if mode == 'release' else 'none'):
                raise ValueError('release/debug codec mode swapped')
            if not metadata['entries']:
                raise ValueError('empty embedding manifest')
            resources, seen = image.rcdata(), set()
            logical_paths = set()
            for entry in metadata['entries']:
                if entry['path'] in logical_paths or not 0 < entry['size'] <= 64 * 1024 * 1024:
                    raise ValueError('duplicate logical path or invalid original size')
                logical_paths.add(entry['path'])
                payload = resources[entry['resource_id']]
                if len(payload) != entry['stored_size'] or sha(payload) != entry['stored_sha256']:
                    raise ValueError('PE stored payload mismatch: '+entry['path'])
                if entry['compressed']:
                    if frame_content_size(payload) != entry['size'] or not payload[4] & 4:
                        raise ValueError('compressed payload lacks runtime size/checksum contract: '+entry['path'])
                elif len(payload) != entry['size'] or sha(payload) != entry['sha256']:
                    raise ValueError('raw payload differs from logical asset')
                if mode == 'debug' and (entry['compressed'] or sha(payload) != entry['sha256']):
                    raise ValueError('debug payload is not the original raw asset')
                seen.add(entry['resource_id'])
            if set(resources) != seen:
                raise ValueError('unbound RCDATA payload')
            identities.append([(entry['path'], entry['size'], entry['sha256']) for entry in metadata['entries']])
            result.update(codec=metadata['compression'], embedding_manifest_sha256=sha(manifest.read_bytes()),
                          logical_assets=len(metadata['entries']), unique_resources=len(resources))
        else:
            result['codec'] = 'external-assets'
        binaries[name] = result
    if len(identities) == 1 or len(identities) == 2 and identities[0] != identities[1]:
        raise ValueError('normal/debug logical payload mismatch')
    alias = (args.artifacts/'amd_fidelityfx_vk.dll').read_bytes()
    if sha(alias) != binaries['amd_fidelityfx_upscaler_vk.dll']['sha256']:
        raise ValueError('stock-name alias differs')
    provenance.update(binaries=binaries, models=['4.0.2', '4.1.1'], source_stage=str(args.stage.resolve()))
    with (args.artifacts/'build-provenance.json').open('x') as stream:
        stream.write(json.dumps(provenance, indent=2, sort_keys=True)+'\n')
    print('PASS: frozen inputs, AMD64 exports, asset identities and normal/debug modes')


if __name__ == '__main__':
    main()
