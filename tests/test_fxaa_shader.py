"""Numerical smoke test of vkd3d's disassembly of the shipped PS bytecode.
Usage: python3 test_fxaa_shader.py fxaa-ps.asm
This is a small CPU interpreter for this shader's instructions, not a GPU test.
Requires numpy. Fails closed on any unsupported instruction.
"""
import re
import sys
from pathlib import Path
import numpy as np

def run(assembly, image, active_width):
    height, width, _ = image.shape
    yy, xx = np.mgrid[:height, :active_width]
    count = xx.size
    def bits(a):
        return np.asarray(a, dtype=np.float32).view(np.uint32)
    registers = {'v0': bits(np.stack((xx.ravel()+.5, yy.ravel()+.5,
                                    np.zeros(count), np.ones(count)), axis=1)),
                 'cb0[0]': np.tile(bits([1/width,1/height,
                                       (active_width-.5)/width,(height-.5)/height]),(count,1))}
    active = np.ones(count, dtype=bool)
    parents = []
    channels = 'xyzw'
    def read(arg):
        negate = arg.startswith('-')
        if negate: arg=arg[1:]
        absolute = arg.startswith('|')
        if absolute: arg=arg[1:-1]
        if arg.startswith('l('):
            tokens=arg[2:-1].split(',')
            vals=[]
            for token in tokens:
                token=token.strip()
                vals.append(bits(float(token)).item() if '.' in token else int(token,0))
            if len(vals)==1: vals*=4
            result=np.tile(np.array(vals,dtype=np.uint32),(count,1))
        else:
            name, _, swizzle=arg.partition('.')
            base=registers.setdefault(name,np.zeros((count,4),dtype=np.uint32))
            swizzle=(swizzle or 'xyzw')
            swizzle=(swizzle*4)[:4]
            result=base[:,[channels.index(c) for c in swizzle]].copy()
        if absolute:result=bits(np.abs(result.view(np.float32)))
        if negate:result=bits(-result.view(np.float32))
        return result
    for line in assembly.splitlines():
        line=line.strip()
        if not line or line.startswith(('ps_', 'dcl_', '//')):continue
        op, _, rest=line.partition(' ')
        args=re.split(r',\s*(?![^()]*\))',rest)
        if op=='if_nz':
            parents.append(active.copy());active=active & (read(args[0])[:,0]!=0);continue
        if op=='endif':active=parents.pop();continue
        if op=='ret':break
        target=args[0];src=[read(x) for x in args[1:]] if op!='sample_l' else [read(args[1])]
        f=[x.view(np.float32) for x in src]
        if op in ('mov','mov_sat'):value=src[0]
        elif op=='add':value=bits(f[0]+f[1])
        elif op=='mul':value=bits(f[0]*f[1])
        elif op=='div':value=bits(f[0]/f[1])
        elif op=='min':value=bits(np.minimum(f[0],f[1]))
        elif op=='max':value=bits(np.maximum(f[0],f[1]))
        elif op=='dp3':value=np.repeat(bits(np.sum(f[0][:,:3]*f[1][:,:3],axis=1))[:,None],4,axis=1)
        elif op=='lt':value=np.where(f[0]<f[1],np.uint32(0xffffffff),np.uint32(0))
        elif op=='ge':value=np.where(f[0]>=f[1],np.uint32(0xffffffff),np.uint32(0))
        elif op=='ine':value=np.where(src[0]!=src[1],np.uint32(0xffffffff),np.uint32(0))
        elif op=='not':value=~src[0]
        elif op=='and':value=src[0]&src[1]
        elif op=='movc':value=np.where(src[0]!=0,src[1],src[2])
        elif op=='sample_l':
            u=np.clip(f[0][:,0]*width-.5,0,width-1)
            v=np.clip(f[0][:,1]*height-.5,0,height-1)
            x0=np.floor(u).astype(int);y0=np.floor(v).astype(int)
            x1=np.minimum(x0+1,width-1);y1=np.minimum(y0+1,height-1)
            tx=(u-x0)[:,None];ty=(v-y0)[:,None]
            value=bits((image[y0,x0]*(1-tx)+image[y0,x1]*tx)*(1-ty)+
                       (image[y1,x0]*(1-tx)+image[y1,x1]*tx)*ty)
        else:raise AssertionError('unsupported instruction '+line)
        if op=='mov_sat':value=bits(np.clip(value.view(np.float32),0,1))
        name, _, mask=target.partition('.')
        output=registers.setdefault(name,np.zeros((count,4),dtype=np.uint32))
        for c in mask or channels:
            j=channels.index(c);output[active,j]=value[active,j]
    assert not parents
    return registers['o0'].view(np.float32).reshape(height,active_width,4)

assembly=Path(sys.argv[1]).read_text()
np.seterr(invalid='ignore',divide='ignore') # inactive branches still evaluated by this CPU interpreter
image=np.empty((33,33,4),dtype=np.float32);image[:,:,:3]=[.2,.4,.6]
image[:,:,3]=np.linspace(0,1,33)[None,:]
result=run(assembly,image,33)
assert np.allclose(result,image,atol=2e-6), 'constant color changed'
yy,xx=np.mgrid[:33,:33]
image[:,:,:3]=(xx>yy*.5+7)[:,:,None]
result=run(assembly,image,33)
assert np.isfinite(result).all()
assert np.allclose(result[:,:,3],image[:,:,3],atol=2e-6), 'alpha changed'
assert np.count_nonzero((result[:,:,0]>.01)&(result[:,:,0]<.99))>20, 'no edge smoothing'
assert np.allclose(result[:,:4,:3],image[:,:4,:3],atol=2e-6), 'flat distant area changed'
image[:,:,:3]=.3;image[:,24:,:3]=[1,0,0]
result=run(assembly,image,24)
assert np.allclose(result[:,:,:3],.3,atol=2e-6), 'sampling leaked from unused texture padding'
print('PASS: compiled PS numerical smoke tests: flat colors, diagonal smoothing, alpha, padding clamp.')
