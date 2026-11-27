import sys;sys.path.insert(0,sys.argv[1]);from gen import *
ink="#0E3B34";bg="#9ED6B6"
mantle='<path d="M206,34 C214,70 196,112 168,134 C146,152 112,158 88,150 C58,140 46,110 56,84 C68,52 104,36 140,38 C166,40 188,40 206,34Z"/>'
fl=tent([[(120,150),(126,178),(100,196),(84,190)],[(84,190),(66,182),(80,160),(102,176)],[(102,176),(130,198),(170,206),(214,192)]],30,4)
eye='<path d="M84,104 C92,84 124,82 134,100 C126,120 94,124 84,104Z" fill="%s"/><rect x="104" y="98" width="24" height="9" rx="4.5" fill="%s"/>'%(bg,ink)
svg=f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><rect width="256" height="256" rx="58" fill="{bg}"/>
<g transform="translate(128 128) scale(0.86) translate(-136 -122)"><g fill="{ink}">{mantle}{fl}</g>{eye}</g></svg>'''
open(sys.argv[2],"w").write(svg)
