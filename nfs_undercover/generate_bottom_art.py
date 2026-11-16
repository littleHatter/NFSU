"""Embed the supplied PNG as the 3DS bottom screen's rotated BGR8 framebuffer."""
from pathlib import Path
import sys
from PIL import Image

root=Path(__file__).resolve().parent
source=Path(sys.argv[1]) if len(sys.argv)>1 else root/'bottomscreen.png'
target=Path(sys.argv[2]) if len(sys.argv)>2 else root/'bottom_art.h'
rgba=Image.open(source).convert('RGBA')
if rgba.size!=(320,240):raise SystemExit('Bottom artwork must be 320x240')
im=Image.alpha_composite(Image.new('RGBA',rgba.size,(0,0,0,255)),rgba).convert('RGB')
data=bytearray(320*240*3)
for y in range(240):
 for x in range(320):
  r,g,b=im.getpixel((x,y));i=(x*240+239-y)*3;data[i:i+3]=bytes((b,g,r))
lines=['#pragma once','#include <cstddef>','alignas(4) inline constexpr unsigned char kBottomArt[] = {']
lines.extend(','.join(str(v) for v in data[i:i+32])+',' for i in range(0,len(data),32))
lines.extend(['};','static_assert(sizeof(kBottomArt)==320*240*3);',''])
target.write_text('\n'.join(lines))
print('Embedded',source.name,len(data),'BGR8 bytes')
