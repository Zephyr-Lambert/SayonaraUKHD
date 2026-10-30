"""Regenerate embedded bytecode using vkd3d-compiler (release used 1.17).
Usage: python3 generate_shaders.py /path/to/vkd3d-compiler
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parent
compiler=sys.argv[1] if len(sys.argv)>1 else 'vkd3d-compiler'
header='/* Generated from fxaa.hlsl using vkd3d-shader; vs_4_0 / ps_4_0. */\n'
with tempfile.TemporaryDirectory() as folder:
    for stage,name,entry in [('vs','fxaaVS','VSMain'),('ps','fxaaPS','PSMain')]:
        output=Path(folder)/(stage+'.dxbc')
        subprocess.run([compiler,'-x','hlsl','-b','dxbc-tpf','-p',stage+'_4_0',
                        '-e',entry,str(root/'fxaa.hlsl'),'-o',str(output)],check=True)
        data=output.read_bytes()
        assert data[:4]==b'DXBC'
        header+='static const unsigned char '+name+'[] = {\n'
        header+='\n'.join(','.join('0x%02x'%v for v in data[i:i+16])+','
                          for i in range(0,len(data),16))+'\n};\n'
        print(stage,len(data),'bytes')
(root/'fxaa_bytecode.h').write_text(header)
