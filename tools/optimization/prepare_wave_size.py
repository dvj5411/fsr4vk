#!/usr/bin/env python3
"""Generate an isolated cached-readback harness with per-pass wave32 control."""
import argparse
import hashlib
import json
from pathlib import Path
from prepare_memory_placement import prepare as memory_prepare


def prepare(source):
    source=memory_prepare(source,cpu_timing=True,cache_readback=True)
    edits={
        '#include "research-memory-placement.hpp"\n':
            '#include "research-memory-placement.hpp"\n#include "research-wave-size.hpp"\n',
        '#include "quality-context.hpp"\n':
            '#define vkCreateComputePipelines fsr4wavetest::create\n'
            '#include "quality-context.hpp"\n#undef vkCreateComputePipelines\n',
        '        fsr4vk::DeviceFeatures enabled_features(\n':
            '        auto research_subgroups=fsr4wavetest::enable(physical_device,device_info,application.apiVersion);\n'
            '        device_info.pNext=&research_subgroups;\n'
            '        fsr4vk::DeviceFeatures enabled_features(\n',
    }
    for old,new in edits.items():
        if source.count(old)!=1: raise ValueError('unexpected wave experiment anchor: '+old)
        source=source.replace(old,new)
    return source


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path); p.add_argument('output',type=Path)
    a=p.parse_args(); original=a.source.read_bytes(); generated=prepare(original.decode()).encode()
    with a.output.open('xb') as f: f.write(generated)
    print(json.dumps(dict(source_sha256=hashlib.sha256(original).hexdigest(),
                          generated_sha256=hashlib.sha256(generated).hexdigest())))


if __name__=='__main__': main()
