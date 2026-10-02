#!/usr/bin/env python3
"""Research only: force the large tensor layout without resizing image resources."""
import argparse
import hashlib
import json
from pathlib import Path


def transform(provider,core):
    bucket='const auto bucket=provider->maxUpscaleSize.width>1920 || provider->maxUpscaleSize.height>1080 ? "2160" : "1080";'
    allocation='fsr4::scratch_capacity(max_output_width,max_output_height)'
    if provider.count(bucket)!=1 or core.count(allocation)!=1:
        raise ValueError('unexpected capacity experiment anchors')
    return (provider.replace(bucket,'const auto bucket="2160"; // isolated tensor-layout experiment'),
            core.replace(allocation,'83232256ull /* isolated large tensor layout */'))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args()
    paths=['provider/ffx_vk_provider.cpp','native/quality-context.hpp']
    original=[(a.root/path).read_text() for path in paths]
    changed=transform(*original)
    a.output.mkdir(parents=True,exist_ok=False)
    report={}
    for path,before,after in zip(paths,original,changed):
        target=a.output/path;target.parent.mkdir(exist_ok=True)
        target.write_text(after)
        report[path]={'source_sha256':hashlib.sha256(before.encode()).hexdigest(),
                      'generated_sha256':hashlib.sha256(after.encode()).hexdigest()}
    (a.output/'provenance.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__': main()
