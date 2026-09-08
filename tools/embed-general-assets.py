#!/usr/bin/env python3
"""Generate deduplicated PE resources from verified private bundle manifests."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('general', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
subprocess.run([sys.executable, str(Path(__file__).with_name('verify-general-assets.py')), str(a.general)], check=True)
a.output.mkdir(parents=True, exist_ok=True)
resources, index, entries = {}, [], []
for manifest in sorted(a.general.glob('*/*/manifest.json')):
    data = json.loads(manifest.read_text())
    for name in [s['file'] for s in data['shaders']] + ['initializers.bin', 'weights.bin']:
        path = manifest.parent/name
        payload = path.read_bytes()
        digest = hashlib.sha256(payload).hexdigest()
        if digest not in resources:
            resources[digest] = (len(resources)+100, path.resolve())
        rid = resources[digest][0]
        key = 'general/'+path.relative_to(a.general).as_posix()
        index.append(f'    {{{json.dumps(key)}, {rid}, {len(payload)}}},')
        entries.append(dict(path=key, resource_id=rid, size=len(payload), sha256=digest))
(a.output/'embedded-assets.rc').write_text('\n'.join(f'{rid} RCDATA {json.dumps(str(path))}' for rid,path in resources.values())+'\n')
(a.output/'embedded-asset-index.hpp').write_text(
    '#pragma once\nnamespace fsr4assets {\nstruct EmbeddedEntry { const char* path; unsigned id; unsigned size; };\n'
    'inline constexpr EmbeddedEntry embedded_index[] = {\n'+'\n'.join(index)+'\n};\n}\n')
(a.output/'embedded-assets.json').write_text(json.dumps(dict(schema=1, entries=entries), indent=2)+'\n')
print(f'Embedded {len(entries)} paths using {len(resources)} unique payloads.')
