import numpy as np, sys
from PIL import Image, ImageDraw
N=466
for name in ["home","panel","tetris","power","prompt"]:
    a=np.fromfile(f"raw_{name}.rgb565",dtype=np.uint16).reshape(N,N)
    r=((a>>11)&31)*255//31; g=((a>>5)&63)*255//63; b=(a&31)*255//31
    img=Image.fromarray(np.dstack([r,g,b]).astype(np.uint8)).convert("RGBA")
    S=4; m=Image.new("L",(N*S,N*S),0); ImageDraw.Draw(m).ellipse((0,0,N*S-1,N*S-1),fill=255)
    img.putalpha(m.resize((N,N),Image.LANCZOS))
    out=Image.new("RGBA",(N,N),(0,0,0,0)); out.alpha_composite(img)
    out.save(f"../previews/{name}.png")
