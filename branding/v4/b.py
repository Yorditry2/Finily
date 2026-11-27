import sys;sys.path.insert(0,sys.argv[1]);from gen import *
head='<path d="M128,36 C150,66 188,84 188,132 C188,166 160,184 128,184 C96,184 68,166 68,132 C68,84 106,66 128,36Z"/>'
T=[tent([[(84,172),(70,196),(50,196),(46,182)]],22,8),tent([[(112,182),(110,206),(96,218),(84,214)]],22,8),
   tent([[(144,182),(146,206),(160,218),(172,214)]],22,8),tent([[(172,172),(186,196),(206,196),(210,182)]],22,8)]
svg=f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><defs><linearGradient id="g" gradientUnits="userSpaceOnUse" x1="100" y1="36" x2="160" y2="220"><stop offset="0" stop-color="#E5122E"/><stop offset="1" stop-color="#FF5470"/></linearGradient></defs>
<rect width="256" height="256" fill="#fff"/><g fill="url(#g)">{head}{"".join(T)}</g>
<g fill="#fff"><ellipse cx="104" cy="134" rx="20" ry="22"/><ellipse cx="152" cy="134" rx="20" ry="22"/></g>
<g fill="#E5122E"><ellipse cx="110" cy="138" rx="8" ry="12"/><ellipse cx="158" cy="138" rx="8" ry="12"/></g>
<g stroke="#FF5470" stroke-width="6" stroke-linecap="round"><path d="M42,118 L28,112M44,134 L28,136"/><path d="M214,118 L228,112M212,134 L228,136"/></g></svg>'''
open(sys.argv[2],"w").write(svg)
