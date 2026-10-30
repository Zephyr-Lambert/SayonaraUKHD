#!/usr/bin/env python3
"""SayonaraUKHD texture helper: DDS dumps -> PNG previews, edited PNG -> replacement DDS.

The mod also loads PNG replacements directly, so 'encode' is optional.

Needs Python 3 and Pillow (pip install pillow). The mod itself does not need Python.

  python3 texture_tool.py preview Textures/dump
  python3 texture_tool.py encode edited.png Textures/replace/tex_<hash>_<w>x<h>_f<n>.dds
"""
import argparse
from array import array
import sys
from pathlib import Path
import struct
from PIL import Image

MAX_BYTES=64*1024*1024

def _channel_lut(mask):
    """Maps a masked channel value to 0..255 using the same rounding as the DLL-side formats."""
    if not mask:
        return None
    shift=(mask&-mask).bit_length()-1
    maximum=mask>>shift
    return shift,maximum,[(v*255+maximum//2)//maximum for v in range(maximum+1)]

def _decode_masked(payload,width,height,unit,masks):
    """Decodes uncompressed 16/32-bit DDS pixels with arbitrary channel masks to RGBA bytes."""
    count=width*height
    if unit==4 and all(m in (0,0xff,0xff00,0xff0000,0xff000000) for m in masks):
        # Byte-aligned channels (RGBA8, BGRA8, BGRX8): plain byte slicing, exact and fast.
        bands=[]
        for channel,mask in enumerate(masks):
            if mask:
                bands.append(payload[(mask.bit_length()-8)//8::4])
            else:
                bands.append(bytes([255 if channel==3 else 0])*count)
        out=bytearray(count*4)
        for channel,band in enumerate(bands):
            out[channel::4]=band
        return Image.frombytes('RGBA',(width,height),bytes(out))
    if unit==2:
        # 16-bit formats (RGB565, RGB5551, RGBA4444): one lookup table over all 65536 values.
        luts=[_channel_lut(m) for m in masks]
        table=[]
        for value in range(65536):
            px=bytearray(4)
            for channel,lut in enumerate(luts):
                if lut:
                    shift,maximum,scale=lut
                    px[channel]=scale[(value>>shift)&maximum]
                elif channel==3:
                    px[channel]=255
            table.append(bytes(px))
        values=array('H')
        values.frombytes(payload)
        if sys.byteorder!='little':
            values.byteswap()
        return Image.frombytes('RGBA',(width,height),b''.join(map(table.__getitem__,values)))
    # Generic fallback (unusual 32-bit masks).
    info=[]
    for mask in masks:
        shift=(mask&-mask).bit_length()-1 if mask else 0
        info.append((mask,shift,mask>>shift))
    out=bytearray(count*4)
    for i in range(count):
        value=int.from_bytes(payload[i*unit:(i+1)*unit],'little')
        for channel,(mask,shift,maximum) in enumerate(info):
            out[i*4+channel]=(((value&mask)>>shift)*255+maximum//2)//maximum if mask else (255 if channel==3 else 0)
    return Image.frombytes('RGBA',(width,height),bytes(out))

def dds_to_image(path):
    path=Path(path)
    if path.stat().st_size>MAX_BYTES+148:
        raise ValueError('DDS exceeds the 64 MiB payload limit')
    data=path.read_bytes()
    if len(data)<128 or data[:4]!=b'DDS ':
        raise ValueError('not a DDS file')
    header=struct.unpack_from('<32I',data)
    height,width=header[3:5]
    if not 1<=width<=8192 or not 1<=height<=8192:
        raise ValueError('dimensions must be 1..8192')
    flags,bits=header[20],header[22]
    if not flags&4 and bits in (8,16,32):
        unit=bits//8
        if len(data)!=128+width*height*unit:
            raise ValueError('expected tightly packed single-mip DDS')
        masks=header[23:27]
        payload=data[128:]
        if flags&0x20000 and bits==8:
            return Image.frombytes('L',(width,height),payload).convert('RGBA')
        return _decode_masked(payload,width,height,unit,masks)
    with Image.open(path) as image:
        return image.convert('RGBA')

def png_to_dds(source,destination):
    destination=Path(destination)
    if destination.exists():raise FileExistsError('output already exists: '+str(destination))
    with Image.open(source) as image:
        w,h=image.size
        if not 1<=w<=8192 or not 1<=h<=8192 or w*h*4>MAX_BYTES:
            raise ValueError('replacement exceeds dimension or 64 MiB payload limit')
        payload=image.convert('RGBA').tobytes()
    header=[0]*32
    header[0]=0x20534444;header[1]=124;header[2]=0x100f
    header[3]=h;header[4]=w;header[5]=w*4;header[7]=1
    header[19]=32;header[20]=0x41;header[22]=32
    header[23:27]=[0xff,0xff00,0xff0000,0xff000000];header[27]=0x1000
    destination.parent.mkdir(parents=True,exist_ok=True)
    with destination.open('xb') as output:
        output.write(struct.pack('<32I',*header));output.write(payload)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    preview=commands.add_parser('preview',help='make PNG previews of a DDS file or dump directory')
    preview.add_argument('source',type=Path)
    preview.add_argument('--output',type=Path,help='output directory (default: sibling previews directory)')
    encode=commands.add_parser('encode',help='convert edited PNG to single-mip RGBA8 DDS')
    encode.add_argument('source',type=Path);encode.add_argument('destination',type=Path)
    args=parser.parse_args()
    if args.command=='encode':
        try:
            png_to_dds(args.source,args.destination)
        except (OSError,ValueError) as error:
            raise SystemExit('error: '+str(error))
        print('wrote',args.destination);return
    folder=args.source if args.source.is_dir() else args.source.parent
    output=args.output or folder.parent/'previews';output.mkdir(parents=True,exist_ok=True)
    files=sorted(args.source.glob('*.dds')) if args.source.is_dir() else [args.source]
    made=skipped=failed=0
    for path in files:
        dest=output/(path.stem+'.png')
        if dest.exists():skipped+=1;continue
        try:
            image=dds_to_image(path)
            with dest.open('xb') as f:image.save(f,format='PNG')
            made+=1
        except Exception as error:
            failed+=1;print(str(path)+': '+str(error))
    print(f'{made} previews created; {skipped} existing files skipped; {failed} failed. Output: {output}')
    if failed:raise SystemExit(1)

if __name__=='__main__':main()
