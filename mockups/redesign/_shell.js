/* Base compartida de los mockups 07+: iconos, objetos de biblioteca, sidebar y cabecera.
   Los iconos y colores de carpeta son DATOS (tablas), no dibujos fijos: así se piensa
   desde ya que el usuario podrá reemplazarlos con sus propios paquetes. */
const IC = {
  docs: '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5"/>',
  star: '<path d="M12 3.5l2.6 5.3 5.8.8-4.2 4.1 1 5.8L12 16.8l-5.2 2.7 1-5.8-4.2-4.1 5.8-.8z"/>',
  folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  folderPlus: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><path d="M12 10.5v5M9.5 13h5"/>',
  folderMove: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><path d="M10 13h5M13 10.8l2.2 2.2-2.2 2.2"/>',
  tag: '<path d="M3 12V4h8l9 9-8 8z"/><circle cx="7.5" cy="8.5" r="1.2"/>',
  hash: '<path d="M5 9h15M4 15h15M10 3 8 21M16 3l-2 18"/>',
  trash: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12M9 7V4h6v3"/>',
  restore: '<path d="M4 12a8 8 0 1 0 2.4-5.7L4 8.5"/><path d="M4 4v4.5h4.5"/>',
  moon: '<path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
  monitor: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
  gear: '<circle cx="12" cy="12" r="3"/><path d="M12 2.8v2.4M12 18.8v2.4M4.2 7.5l2.1 1.2M17.7 15.3l2.1 1.2M4.2 16.5l2.1-1.2M17.7 8.7l2.1-1.2"/><circle cx="12" cy="12" r="7"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.5-3.5"/>',
  filter: '<path d="M4 6h16M7 12h10M10 18h4"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  x: '<path d="M6 6l12 12M18 6L6 18"/>',
  chevR: '<path d="M9 6l6 6-6 6"/>',
  chevD: '<path d="M6 9l6 6 6-6"/>',
  chevL: '<path d="M15 6l-6 6 6 6"/>',
  side: '<rect x="3" y="4" width="18" height="16" rx="3"/><path d="M9 4v16"/>',
  pencil: '<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="M13.5 6.5l4 4"/>',
  check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
  open: '<path d="M14 4h6v6M20 4l-9 9"/><path d="M18 14v4a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4"/>',
  tab: '<rect x="3" y="6" width="18" height="14" rx="2"/><path d="M3 10h18M8 6V4h6v2"/>',
  copy: '<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2"/>',
  export: '<path d="M12 15V3M7.5 7.5 12 3l4.5 4.5"/><path d="M5 14v4a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-4"/>',
  download: '<path d="M12 3v12M7.5 10.5 12 15l4.5-4.5"/><path d="M5 19h14"/>',
  palette: '<path d="M12 3a9 9 0 1 0 0 18c1.1 0 1.6-.9 1.2-1.8-.5-1-.1-2.2 1.2-2.2H17a4 4 0 0 0 4-4c0-5.5-4-10-9-10z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10" cy="7" r="1"/><circle cx="15" cy="7.5" r="1"/>',
  cover: '<rect x="5" y="3" width="14" height="18" rx="2"/><path d="M9 3v18M12 8h4"/>',
  info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/>',
  more: '<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
  sort: '<path d="M7 4v16M3.5 16.5 7 20l3.5-3.5M14 6h7M14 12h5M14 18h3"/>',
  board: '<rect x="3" y="5" width="18" height="14" rx="2.5"/><path d="M8 12c1.5-2 3-2 4 0s2.5 2 4 0"/>',
  book: '<path d="M6 3h12a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H6a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M8 3v18"/>',
  pdf: '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5M9 14h6M9 17h4"/>',
  layers: '<path d="M12 3 3 8l9 5 9-5z"/><path d="M3 13l9 5 9-5"/>',
  pen: '<path d="M15 4l5 5-10 10H5v-5z"/><path d="M13 6l5 5"/>',
  hdd: '<rect x="3" y="13" width="18" height="7" rx="2"/><path d="M5 13l2.5-7h9L19 13M7 16.5h.01"/>',
  refresh: '<path d="M20 11a8 8 0 0 0-14.5-4.5L4 8M4 13a8 8 0 0 0 14.5 4.5L20 16"/><path d="M4 4v4h4M20 20v-4h-4"/>',
  sparkle: '<path d="M12 3v4M12 17v4M3 12h4M17 12h4M6 6l2.5 2.5M15.5 15.5 18 18M6 18l2.5-2.5M15.5 8.5 18 6"/>',
  grid: '<rect x="4" y="4" width="7" height="7" rx="1.5"/><rect x="13" y="4" width="7" height="7" rx="1.5"/><rect x="4" y="13" width="7" height="7" rx="1.5"/><rect x="13" y="13" width="7" height="7" rx="1.5"/>',
  alert: '<path d="M12 4 2.5 20h19z"/><path d="M12 10v4M12 17h.01"/>',
  upload: '<path d="M12 16V4M7.5 8.5 12 4l4.5 4.5"/><path d="M5 20h14"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  hand: '<path d="M7 11V6.5a1.5 1.5 0 0 1 3 0V11M10 10V4.5a1.5 1.5 0 0 1 3 0V10M13 10V5.5a1.5 1.5 0 0 1 3 0V12M16 9.5a1.5 1.5 0 0 1 3 0V14a7 7 0 0 1-7 7h-1a6 6 0 0 1-5-3l-2.5-4.5a1.5 1.5 0 0 1 2.6-1.5L7 14"/>',
};
/* Iconos de carpeta (paquete por defecto; reemplazable) */
const FICON = {
  book: '<path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/>',
  shape: '<polygon points="12 3 3 19 21 19 12 3"/>',
  sun: '<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M2 12h3M19 12h3M4.9 19.1l2.1-2.1M17 7l2.1-2.1"/>',
  music: '<path d="M9 18V5l11-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="17" cy="16" r="3"/>',
  flask: '<path d="M9 3h6M10 3v6L4.5 18.5A2 2 0 0 0 6.2 21.5h11.6a2 2 0 0 0 1.7-3L14 9V3"/><path d="M7 15h10"/>',
  none: '',
};
/* Paleta de carpetas 2 tonos (tema por defecto; reemplazable) */
const FCOL = {
  rosa:    { b1: '#F2A4B9', b2: '#E78FA8', flap: '#D57892', ink: '#8C3952' },
  arena:   { b1: '#EDD1B8', b2: '#E1C0A2', flap: '#CCA887', ink: '#7A5739' },
  salvia:  { b1: '#A6CEBB', b2: '#92BFA9', flap: '#77A38E', ink: '#315C48' },
  cielo:   { b1: '#A9C7E8', b2: '#92B5DD', flap: '#7499C6', ink: '#2F5482' },
  lavanda: { b1: '#C5B7E6', b2: '#B3A2DC', flap: '#9682C6', ink: '#4F3D85' },
  durazno: { b1: '#F5BFA0', b2: '#EDA984', flap: '#D98B64', ink: '#8A4526' },
  limon:   { b1: '#EDE09A', b2: '#E3D27C', flap: '#C9B65C', ink: '#6B5B17' },
  grafito: { b1: '#B9BCC6', b2: '#A3A7B3', flap: '#868B99', ink: '#363A45' },
};
const COVERS = { salvia: '#8FBCA6', tinta: '#26272D', rosa: '#E799AE', cielo: '#94B6DD', arena: '#DDBB9C', lavanda: '#B3A2DC' };

const svgI = (k, extra = '') => `<svg class="i" viewBox="0 0 24 24" ${extra}>${IC[k]}</svg>`;
let _gid = 0;

function folderSVG(color, icon = 'none', w = 118, opts = {}) {
  const c = FCOL[color], id = 'fg' + (++_gid);
  const ic = FICON[icon] ? `<g transform="translate(49,45)" fill="none" stroke="${c.ink}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">${FICON[icon]}</g>` : '';
  const lift = opts.open ? 'transform="translate(0,-3) rotate(-4 61 92)"' : '';
  return `<svg width="${w}" height="${w * 92 / 118}" viewBox="0 0 122 96" style="overflow:visible;filter:drop-shadow(0 10px 18px rgba(15,23,42,.13)) drop-shadow(0 2px 5px rgba(15,23,42,.06))"><defs><linearGradient id="${id}" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="${c.b1}"/><stop offset="100%" stop-color="${c.b2}"/></linearGradient></defs><path d="M4 16 C4 9 9 4 16 4 L40 4 C45 4 48 6 52 10 L56 14 L108 14 C114 14 118 18 118 24 L118 82 C118 88 113 92 107 92 L15 92 C9 92 4 88 4 82 Z" fill="${c.flap}"/>${opts.open ? '<rect x="14" y="10" width="92" height="60" rx="5" fill="#fff" opacity=".95"/>' : ''}<g ${lift}><rect x="4" y="21" width="114" height="71" rx="11" fill="url(#${id})"/><line x1="15" y1="22" x2="107" y2="22" stroke="#fff" stroke-opacity=".48" stroke-width="1"/>${ic}</g></svg>`;
}
function boardHTML(kind = 'paper', w = 118) {
  const h = Math.round(w * 86 / 118), dark = kind === 'dark';
  const bg = dark ? '#141720' : '#FAF9F5', dot = dark ? 'rgba(255,255,255,.16)' : 'rgba(15,23,42,.13)';
  return `<div class="board" style="width:${w}px;height:${h}px;background:${bg};background-image:radial-gradient(${dot} .9px,transparent 1px);background-size:9px 9px;color:${dark ? '#F4F4F5' : '#18181B'}"><span class="inf">∞</span></div>`;
}
function bookHTML(cover = 'salvia', w = 84, fmt = 'A4') {
  const h = Math.round(w * 110 / 84);
  return `<div class="book" style="width:${w}px;height:${h}px;background:linear-gradient(160deg,${COVERS[cover]},${COVERS[cover]}dd)"><div class="lbl"></div><span class="fmt">${fmt}</span></div>`;
}
function pdfHTML(w = 80) {
  const h = Math.round(w * 104 / 80);
  const lines = [16, 24, 32, 40, 52, 60, 68].map((t, i) => `<span class="ln${i === 2 || i === 5 ? ' hl' : ''}" style="top:${t * h / 104}px;${i % 3 === 2 ? 'right:30px' : ''}"></span>`).join('');
  return `<div class="pdf" style="width:${w}px;height:${h}px">${lines}<span class="tag">PDF</span></div>`;
}
function objHTML(vis, name, date, cls = '', extra = '') {
  return `<div class="obj ${cls}">${extra}<div class="vis">${vis}</div><div><div class="obj-name">${name}</div><div class="obj-date">${date}</div></div></div>`;
}
/* Biblioteca de ejemplo */
const LIB = [
  () => objHTML(folderSVG('rosa', 'book'), 'Apuntes', 'Hoy · 14:30'),
  () => objHTML(folderSVG('arena', 'shape'), 'Diagramas', 'Ayer'),
  () => objHTML(folderSVG('salvia', 'sun'), 'Clases', '22 nov 2026'),
  () => objHTML(boardHTML('dark'), 'Lluvia de ideas', 'Hoy · 11:02'),
  () => objHTML(bookHTML('salvia'), 'Física II', 'Ayer · 19:40'),
  () => objHTML(bookHTML('tinta', 84, '16:9'), 'Clase 07 — Ondas', '24 nov 2026'),
  () => objHTML(pdfHTML(), 'Guía de laboratorio', '23 nov 2026'),
  () => objHTML(boardHTML('paper'), 'Mapa mental', '20 nov 2026'),
];

/* ---------- Sidebar ---------- */
function sidebarHTML(o = {}) {
  const a = o.active || 'docs';
  const n = (k, ic, label, extra = '') => `<div class="nav${a === k ? ' on' : ''}">${svgI(ic)}${label}${extra}</div>`;
  const tree = o.folders ? `<div class="tree">
      <div class="nav"><span class="tdot" style="background:#E78FA8"></span>Apuntes</div>
      <div class="nav"><span class="tdot" style="background:#E1C0A2"></span>Diagramas</div>
      <div class="nav"><span class="tdot" style="background:#92BFA9"></span>Clases</div></div>` : '';
  const tags = o.tags === false ? '' : o.tagsOpen ? `${n('tags', 'tag', 'Etiquetas', `<span class="chev">${svgI('chevD')}</span>`)}<div class="tree">
      ${[['física', '#7499C6'], ['examen', '#D98B64'], ['ideas', '#9682C6']].map(([t, c]) => `<div class="nav${o.tag === t ? ' on' : ''}"><span class="tagdot" style="background:${c}"></span>${t}</div>`).join('')}</div>`
    : n('tags', 'tag', 'Etiquetas', `<span class="chev">${svgI('chevR')}</span>`);
  return `<aside class="sb">
    <div class="sb-head"><span class="sb-brand">Finily</span><button class="sb-btn">${svgI('side')}</button></div>
    ${n('docs', 'docs', 'Documentos')}
    ${n('favs', 'star', 'Favoritos')}
    ${n('folders', 'folder', 'Carpetas', `<span class="chev">${svgI(o.folders ? 'chevD' : 'chevR')}</span>`)}${tree}
    ${tags}
    <div class="sb-foot">${o.sbFootExtra || ''}${n('trash', 'trash', 'Papelera', o.trashCount ? `<span class="cnt">${o.trashCount}</span>` : '')}<div class="sb-div"></div>
    <div class="sb-icons"><button class="sb-btn">${svgI(o.dark ? 'sun' : 'moon')}</button><button class="sb-btn${a === 'settings' ? ' on' : ''}">${svgI('gear')}</button></div></div>
  </aside>`;
}
function headerHTML(o = {}) {
  const title = Array.isArray(o.title)
    ? o.title.map((t, i) => i < o.title.length - 1 ? `<span class="crumb">${t}</span>${svgI('chevR')}` : t).join('')
    : (o.title || 'Documentos');
  if (o.custom) return `<div class="hdr">${o.custom}</div>`;
  return `<div class="hdr"><div class="hdr-title">${title}</div><div class="hdr-sp"></div>${o.right ?? ''}
    <button class="icon-btn${o.searchOn ? ' on' : ''}">${svgI('search')}</button>
    <button class="icon-btn${o.filterOn ? ' on' : ''}">${svgI('filter')}${o.filterBadge ? '<span class="badge"></span>' : ''}</button>
    <button class="pill-new">${svgI('plus')}Nuevo</button></div>`;
}
/* Marco completo: o.body reemplaza la grilla; o.overlay va encima de todo */
function frameHTML(o = {}) {
  if (o.dark && o.sb !== undefined) o = { ...o, sb: { ...o.sb, dark: true } };
  const body = o.body ?? `<div class="grid">${(o.items || LIB).map(f => typeof f === 'function' ? f() : f).join('')}</div>`;
  return `<div class="frame-wrap"><div class="frame${o.dark ? ' dark' : ''}">${o.sidebar === false ? '' : sidebarHTML(o.sb)}
    <main class="lib">${headerHTML(o.hdr)}${o.subbar || ''}${body}${o.libOverlay || ''}</main>${o.overlay || ''}</div></div>`;
}
function scene(id, n, title, desc, frame, notes = '') {
  return `<section class="scene" id="${id}"><div class="scene-cap"><h2><span class="n">${n}</span>${title}</h2><p>${desc}</p></div>${frame}${notes ? `<div class="notes">${notes}</div>` : ''}</section>`;
}
/* Menú contextual: filas [icon, label, shortcut, cls] o '-' o ['#', 'TÍTULO'] */
function menuHTML(rows, style) {
  return `<div class="menu" style="${style}">${rows.map(r => {
    if (r === '-') return '<div class="msep"></div>';
    if (typeof r === 'string') return r;
    if (r[0] === '#') return `<div class="mhead">${r[1]}</div>`;
    const [ic, label, k = '', cls = ''] = r;
    return `<div class="mi ${cls}">${ic ? svgI(ic) : '<span style="width:15px"></span>'}${label}<span class="k">${k === '>' ? svgI('chevR') : k}</span></div>`;
  }).join('')}</div>`;
}
function fitFrames() {
  document.querySelectorAll('.frame-wrap').forEach(w => {
    const f = w.firstElementChild, s = Math.min(1, w.clientWidth / 1180);
    f.style.transform = `scale(${s})`; w.style.height = 720 * s + 'px';
  });
}
window.addEventListener('resize', fitFrames);

/* Cabecera en modo búsqueda: el título se convierte en el campo */
function searchHeader(q, o = {}) {
  return { custom: `<div class="sfield">${svgI('search')}<span class="q">${q}</span><span class="caret"></span>
      ${o.scope ? `<span class="scope">${svgI('folder')}${o.scope}<span class="x">${svgI('x')}</span></span>` : ''}<span class="kbd" style="margin-left:auto">Esc</span></div>
    <button class="icon-btn${o.filterOn ? ' on' : ''}">${svgI('filter')}${o.filterBadge ? '<span class="badge"></span>' : ''}</button>
    <button class="pill-new">${svgI('plus')}Nuevo</button>` };
}
