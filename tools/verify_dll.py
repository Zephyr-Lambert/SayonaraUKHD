#!/usr/bin/env python3
"""Check release architecture, dependencies and public exports; no GPU test."""
from pathlib import Path
import re
import struct
import subprocess
p = Path(__file__).resolve().parents[1] / 'build' / 'd3d11.dll'
b = p.read_bytes()
pe = struct.unpack_from('<I', b, 0x3c)[0]
assert b[pe:pe+4] == b'PE\0\0'
assert struct.unpack_from('<H', b, pe+4)[0] == 0x14c, 'Not x86'
assert struct.unpack_from('<H', b, pe+22)[0] & 0x2000, 'Not a DLL'
assert struct.unpack_from('<H', b, pe+24)[0] == 0x10b, 'Not PE32'
out = subprocess.check_output(['objdump', '-p', str(p)], text=True)
imports = set(re.findall(r'DLL Name: (\S+)', out))
assert {x.lower() for x in imports} == {'kernel32.dll', 'msvcrt.dll', 'user32.dll'}, imports
export_block = out.split('[Ordinal/Name Pointer] Table')[1].split('PE File Base Relocations')[0]
exports = set(re.findall(r'\[\s*\d+\]\s+(\S+)', export_block))
assert exports == {'D3D11CreateDevice', 'D3D11CreateDeviceAndSwapChain'}, exports
print('PASS: x86 PE32 DLL, exact device-creation exports, only Windows system dependencies.')
