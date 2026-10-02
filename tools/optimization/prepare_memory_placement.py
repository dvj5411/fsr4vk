#!/usr/bin/env python3
"""Create a separate instrumented harness, never edit the provider or its header."""
import argparse
import hashlib
import json
from pathlib import Path


def prepare(source, cpu_timing=False, cache_readback=False):
    first = '#include <vulkan/vulkan.h>\n'
    target = '#include "quality-context.hpp"\n'
    if source.count(first) != 1 or source.count(target) != 1:
        raise ValueError('unexpected harness include layout')
    if 'research-memory-placement.hpp' in source:
        raise ValueError('already instrumented')
    if cache_readback and not cpu_timing:
        raise ValueError('cached readback requires CPU timing')
    source=source.replace(first, first+'#include "research-memory-placement.hpp"\n')
    names = {'vkCreateBuffer': 'create_buffer',
             'vkGetBufferMemoryRequirements': 'buffer_requirements',
             'vkGetPhysicalDeviceMemoryProperties': 'memory_properties',
             'vkAllocateMemory': 'allocate_memory'}
    replacement=''.join(f'#define {name} fsr4memorytest::{value}\n' for name,value in names.items())
    replacement+=target
    replacement+=''.join(f'#undef {name}\n' for name in names)
    source=source.replace(target,replacement)
    if cpu_timing:
        edits={
            '        const auto record_profiled_context = [&] {':
                '        double research_cpu_record_us=0;\n        const auto record_profiled_context = [&] {',
            '            record_context();\n':
                '            const auto research_cpu_start=std::chrono::steady_clock::now();\n'
                '            record_context();\n'
                '            research_cpu_record_us=std::chrono::duration<double,std::micro>(\n'
                '                std::chrono::steady_clock::now()-research_cpu_start).count();\n',
            '<< ",\\"context_gpu_ms\\":" << ms(whole[0],whole[1])':
                '<< ",\\"cpu_record_us\\":" << research_cpu_record_us\n                << ",\\"context_gpu_ms\\":" << ms(whole[0],whole[1])',
        }
        for old,new in edits.items():
            if source.count(old)!=1:
                raise ValueError('unexpected CPU timing anchor: '+old)
            source=source.replace(old,new)
        source=source.replace(first,first+'#include <chrono>\n')
        pixel_line=('            std::vector<uint16_t> cached_pixels(output_size/2);\n'
                    '            std::memcpy(cached_pixels.data(),readback.mapped,output_size);\n'
                    '            const auto* pixels=cached_pixels.data();')
        hash_expr='fnv1a64(reinterpret_cast<const uint8_t*>(pixels),output_size)'
        log_line='            benchmark_log << std::setprecision(12)'
        for anchor in (pixel_line,hash_expr,log_line):
            # Internal and external-provider collectors each own a timing/hash
            # scope. Do not accidentally instrument just one compile-time path.
            if source.count(anchor)!=2: raise ValueError('unexpected readback timing anchor')
        pixel_replacement='            const auto research_readback_start=std::chrono::steady_clock::now();\n'
        if cache_readback:
            anchor='        const auto collect_profile = [&](unsigned frame_index) {'
            if source.count(anchor)!=1: raise ValueError('unexpected collect-profile anchor')
            source=source.replace(anchor,'        std::vector<uint16_t> research_cached_pixels(benchmark_queries ? output_size/2 : 0);\n'+anchor)
            pixel_replacement+='            std::memcpy(research_cached_pixels.data(),readback.mapped,output_size);\n'
            pixel_replacement+='            const auto* pixels=research_cached_pixels.data();'
        else: pixel_replacement+='            const auto* pixels=static_cast<const uint16_t*>(readback.mapped);'
        source=source.replace(pixel_line,pixel_replacement)
        source=source.replace(hash_expr,'research_output_hash')
        source=source.replace(log_line,
            '            const auto research_output_hash=fnv1a64(reinterpret_cast<const uint8_t*>(pixels),output_size);\n'
            '            const auto research_readback_us=std::chrono::duration<double,std::micro>(\n'
            '                std::chrono::steady_clock::now()-research_readback_start).count();\n'+log_line)
        anchor='<< ",\\"cpu_record_us\\":"'
        source=source.replace(anchor,'<< ",\\"cpu_readback_us\\":" << research_readback_us\n                '+anchor)
        external='<< ",\\"context_gpu_ms\\":" << gpu_ms << ",\\"record_cpu_ms\\":" << record_cpu_ms'
        if source.count(external)!=1: raise ValueError('unexpected external-provider timing anchor')
        source=source.replace(external,external+' << ",\\"cpu_readback_us\\":" << research_readback_us')
    return source


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--cpu-timing',action='store_true')
    parser.add_argument('--cache-readback',action='store_true')
    args=parser.parse_args()
    original=args.source.read_bytes()
    result=prepare(original.decode(),args.cpu_timing,args.cache_readback).encode()
    with args.output.open('xb') as stream: stream.write(result)
    print(json.dumps(dict(source_sha256=hashlib.sha256(original).hexdigest(),
                          generated_sha256=hashlib.sha256(result).hexdigest())))


if __name__=='__main__': main()
