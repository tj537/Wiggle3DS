# A made-up camera pair for tools/preview.c and make test: sky, grass, a
# face and a red ball, a bit dark, the right eye shifted. usage: testpair.py out.raw
import struct, math, sys
W,H=640,480
out=bytearray()
for eye in (0,1):
    for y in range(H):
        for x in range(W):
            xx=x+(8 if eye else 0)
            # sky, ground, a red ball, a person-ish skin blob
            if y<200: r,g,b=0.35+0.2*y/200,0.55+0.2*y/200,0.9
            else: r,g,b=0.25,0.5+0.1*math.sin(xx/30),0.2
            if (xx-320)**2+(y-260)**2<90**2: r,g,b=0.85,0.6,0.5
            if (xx-500)**2+(y-330)**2<50**2: r,g,b=0.9,0.15,0.1
            r*=0.5;g*=0.5;b*=0.45  # underexposed, slightly warm-ish off
            p=(int(r*31)<<11)|(int(g*63)<<5)|int(b*31)
            out+=struct.pack('<H',p)
open(sys.argv[1],'wb').write(out)
