#!/usr/bin/env python3
"""Reproduce the embedded external-exposure adapter from a compiled SPIR-V."""
import argparse
from pathlib import Path
import struct

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('spirv',type=Path)
parser.add_argument('header',type=Path)
args=parser.parse_args()
data=args.spirv.read_bytes()
if len(data)%4 or len(data)<20:raise ValueError('Invalid SPIR-V size')
words=struct.unpack('<'+'I'*(len(data)//4),data)
if words[0]!=0x07230203:raise ValueError('Invalid SPIR-V magic')
lines=['#pragma once','#include <cstdint>',
       '// Generated from external-exposure.comp with glslang -V --target-env vulkan1.1.',
       'namespace fsr4core {','inline constexpr std::uint32_t kExternalExposureSpv[] = {']
lines += ['    '+', '.join(f'0x{x:08x}u' for x in words[i:i+8])+',' for i in range(0,len(words),8)]
lines += ['};','}']
args.header.write_text('\n'.join(lines)+'\n')
