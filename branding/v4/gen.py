import math,sys
def bez(p,t):
    (x0,y0),(x1,y1),(x2,y2),(x3,y3)=p;u=1-t
    return (u**3*x0+3*u*u*t*x1+3*u*t*t*x2+t**3*x3, u**3*y0+3*u*u*t*y1+3*u*t*t*y2+t**3*y3)
def tent(segs,w0,w1,n=60):
    pts=[]
    for i,s in enumerate(segs):
        for k in range(n+ (1 if i==len(segs)-1 else 0)):
            pts.append(bez(s,k/n))
    N=len(pts);L=[];R=[]
    for i,(x,y) in enumerate(pts):
        a=pts[max(i-1,0)];b=pts[min(i+1,N-1)]
        dx,dy=b[0]-a[0],b[1]-a[1];d=math.hypot(dx,dy) or 1
        nx,ny=-dy/d,dx/d;t=i/(N-1);w=(w0+(w1-w0)*t**0.8)/2
        L.append((x+nx*w,y+ny*w));R.append((x-nx*w,y-ny*w))
    ex,ey=pts[-1];r=w1/2
    d="M"+" L".join(f"{x:.1f},{y:.1f}" for x,y in L)
    d+=" L"+" L".join(f"{x:.1f},{y:.1f}" for x,y in reversed(R))+"Z"
    return f'<path d="{d}"/><circle cx="{ex:.1f}" cy="{ey:.1f}" r="{r:.1f}"/>'
GRAIN='''<filter id="grain" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.55" numOctaves="3" seed="7"/><feColorMatrix values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 -14 10.2"/><feComposite in="SourceGraphic" operator="in"/></filter>'''
