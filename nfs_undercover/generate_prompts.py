"""Embed the supplied, unscaled A / Start / B prompt strip as RGBA."""
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parent
images=[Image.open(root/'artwork'/name).convert('RGBA') for name in ['A.png','Start.png','B.png']]
assert [im.size for im in images]==[(83,17),(160,17),(77,17)]
data=b''.join(im.tobytes() for im in images)
lines=['#pragma once','inline constexpr unsigned char kPromptRgba[] = {']
lines += [','.join(map(str,data[i:i+32]))+',' for i in range(0,len(data),32)]
lines += ['};','static_assert(sizeof(kPromptRgba)==320*17*4);','']
(root/'prompt_art.h').write_text('\n'.join(lines))
