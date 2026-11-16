"""Embed the supplied startup indicator without scaling or recolouring."""
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parent
im=Image.open(root/'artwork/loading.png').convert('RGBA')
assert im.size==(57,12)
data=im.tobytes()
lines=['#pragma once','inline constexpr unsigned kStartupWidth=57,kStartupHeight=12;','inline constexpr unsigned char kStartupRgba[] = {']
lines += [','.join(map(str,data[i:i+32]))+',' for i in range(0,len(data),32)]
lines += ['};','static_assert(sizeof(kStartupRgba)==kStartupWidth*kStartupHeight*4);','']
(root/'startup_art.h').write_text('\n'.join(lines))
