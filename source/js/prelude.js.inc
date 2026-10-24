R"JS(
// BrowseNX browser API, built on the __nx natives in engine.cpp. Nodes are integer ids on the
// C++ side; each gets one JS wrapper object here so identity (a === b) holds.
(function () {
'use strict';
const nx = globalThis.__nx;
const G = globalThis;
const nodes = new Map();          // id -> Node
let constructingId = 0;           // wrap() passes the id to custom element constructors
let currentScript = 0;
let readyState = 'loading';
const customTags = new Map();     // tag -> class
const customNames = new Map();    // class -> tag

// ---------------------------------------------------------------------------
// console
// ---------------------------------------------------------------------------
function fmt(v) {
  if (typeof v === 'string') return v;
  if (v instanceof Error) return v.name + ': ' + v.message + (v.stack ? '\n' + v.stack : '');
  if (v instanceof Node) return '<' + (v.nodeName || '').toLowerCase() + '>';
  try { const s = JSON.stringify(v); return s === undefined ? String(v) : s.length > 400 ? s.slice(0, 400) + '…' : s; }
  catch (e) { return String(v); }
}
const timers = {};
G.console = {};
for (const level of ['log', 'info', 'warn', 'error', 'debug', 'trace', 'dir'])
  G.console[level] = (...a) => nx.log(level, a.map(fmt).join(' '));
G.console.table = G.console.log;
G.console.assert = (c, ...a) => { if (!c) nx.log('assert', a.map(fmt).join(' ')); };
G.console.group = G.console.groupCollapsed = G.console.groupEnd = G.console.count = () => {};
G.console.time = (l = 'default') => { timers[l] = performance.now(); };
G.console.timeEnd = (l = 'default') => nx.log('log', l + ': ' + (performance.now() - (timers[l] || 0)).toFixed(1) + 'ms');

function report(e, where) { nx.log('error', (where ? where + ': ' : '') + fmt(e)); }

// ---------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------
class Event {
  constructor(type, init = {}) {
    this.type = String(type);
    this.bubbles = !!init.bubbles;
    this.cancelable = !!init.cancelable;
    this.composed = !!init.composed;
    this.defaultPrevented = false;
    this.target = null;
    this.currentTarget = null;
    this.eventPhase = 0;
    this.isTrusted = false;
    this.timeStamp = performance.now();
    this._stop = false;
    this._stopNow = false;
  }
  preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
  stopPropagation() { this._stop = true; }
  stopImmediatePropagation() { this._stop = this._stopNow = true; }
  composedPath() { return this._path || []; }
  initEvent(type, bubbles, cancelable) { this.type = type; this.bubbles = !!bubbles; this.cancelable = !!cancelable; }
  get returnValue() { return !this.defaultPrevented; }
  set returnValue(v) { if (!v) this.preventDefault(); }
  get srcElement() { return this.target; }
}
Event.NONE = 0; Event.CAPTURING_PHASE = 1; Event.AT_TARGET = 2; Event.BUBBLING_PHASE = 3;
class CustomEvent extends Event { constructor(t, i = {}) { super(t, i); this.detail = i.detail === undefined ? null : i.detail; } }
class UIEvent extends Event { constructor(t, i = {}) { super(t, i); this.detail = i.detail || 0; this.view = G; } }
class MouseEvent extends UIEvent {
  constructor(t, i = {}) {
    super(t, i);
    for (const k of ['clientX', 'clientY', 'pageX', 'pageY', 'screenX', 'screenY', 'button', 'buttons', 'offsetX', 'offsetY'])
      this[k] = i[k] || 0;
    this.altKey = this.ctrlKey = this.metaKey = this.shiftKey = false;
    this.relatedTarget = i.relatedTarget || null;
  }
}
class PointerEvent extends MouseEvent { constructor(t, i = {}) { super(t, i); this.pointerType = 'touch'; this.pointerId = 1; } }
class KeyboardEvent extends UIEvent { constructor(t, i = {}) { super(t, i); this.key = i.key || ''; this.code = i.code || ''; this.keyCode = i.keyCode || 0; } }
class FocusEvent extends UIEvent {}
class InputEvent extends UIEvent { constructor(t, i = {}) { super(t, i); this.data = i.data || null; this.inputType = i.inputType || ''; } }
class SubmitEvent extends Event { constructor(t, i = {}) { super(t, i); this.submitter = i.submitter || null; } }
class MessageEvent extends Event { constructor(t, i = {}) { super(t, i); this.data = i.data; this.origin = i.origin || ''; this.source = i.source || null; } }
class ProgressEvent extends Event { constructor(t, i = {}) { super(t, i); this.loaded = i.loaded || 0; this.total = i.total || 0; this.lengthComputable = !!i.total; } }

const handlerProps = ['click', 'dblclick', 'mousedown', 'mouseup', 'mouseover', 'mouseout', 'mousemove', 'mouseenter',
  'mouseleave', 'touchstart', 'touchend', 'touchmove', 'pointerdown', 'pointerup', 'keydown', 'keyup', 'keypress',
  'input', 'change', 'submit', 'reset', 'focus', 'blur', 'load', 'error', 'scroll', 'resize', 'readystatechange',
  'animationend', 'transitionend', 'contextmenu', 'wheel', 'message', 'popstate', 'hashchange', 'beforeunload',
  'unload', 'pageshow', 'pagehide', 'visibilitychange', 'abort', 'progress', 'loadend', 'loadstart', 'timeout'];

class EventTarget {
  addEventListener(type, fn, opts) {
    if (!fn) return;
    const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
    const once = !!(opts && typeof opts === 'object' && opts.once);
    const L = this._ls || (this._ls = {});
    const list = L[type] || (L[type] = []);
    if (!list.some(l => l.fn === fn && l.capture === capture)) list.push({ fn, capture, once });
    const sig = opts && typeof opts === 'object' && opts.signal;
    if (sig) sig.addEventListener('abort', () => this.removeEventListener(type, fn, opts));
  }
  removeEventListener(type, fn, opts) {
    const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
    const list = this._ls && this._ls[type];
    if (list) this._ls[type] = list.filter(l => !(l.fn === fn && l.capture === capture));
  }
  _fire(ev, phase) {
    ev.currentTarget = this;
    ev.eventPhase = phase;
    const list = this._ls && this._ls[ev.type];
    if (list) {
      for (const l of list.slice()) {
        if (phase === 1 && !l.capture) continue;
        if (phase === 3 && l.capture) continue;
        if (l.once) this.removeEventListener(ev.type, l.fn, l.capture);
        try {
          if (typeof l.fn === 'function') { if (l.fn.call(this, ev) === false) ev.preventDefault(); }
          else if (l.fn && l.fn.handleEvent) l.fn.handleEvent(ev);
        } catch (e) { report(e, ev.type + ' listener'); }
        if (ev._stopNow) return;
      }
    }
    if (phase !== 1) {
      const h = this._handler(ev.type);
      if (h) {
        try { if (h.call(this, ev) === false) ev.preventDefault(); } catch (e) { report(e, 'on' + ev.type); }
      }
    }
  }
  _handler(type) {
    const own = this._on && this._on[type];
    if (own !== undefined) return own;
    if (this instanceof Element) {
      const code = nx.attr(this._id, 'on' + type);
      if (code) {
        const key = 'attr:' + code;
        if (!this._on) this._on = {};
        if (!this._on[key]) {
          try { this._on[key] = new Function('event', code); } catch (e) { report(e, 'on' + type); this._on[key] = () => {}; }
        }
        return this._on[key];
      }
    }
    return null;
  }
  dispatchEvent(ev) {
    ev.target = this;
    const path = [];
    for (let n = this; n; n = n === G ? null : n === document ? G : (n.parentNode || (n.isConnected ? document : null))) path.push(n);
    ev._path = path;
    for (let i = path.length - 1; i > 0 && !ev._stop; i--) path[i]._fire(ev, 1);
    if (!ev._stop) path[0]._fire(ev, 2);
    if (ev.bubbles) for (let i = 1; i < path.length && !ev._stop; i++) path[i]._fire(ev, 3);
    ev.currentTarget = null;
    ev.eventPhase = 0;
    return !ev.defaultPrevented;
  }
}
for (const t of handlerProps) {
  Object.defineProperty(EventTarget.prototype, 'on' + t, {
    get() { return (this._on && this._on[t]) || null; },
    set(v) { (this._on || (this._on = {}))[t] = typeof v === 'function' ? v : null; },
    configurable: true,
  });
}

// ---------------------------------------------------------------------------
// nodes
// ---------------------------------------------------------------------------
function wrap(id) {
  if (!id) return null;
  let n = nodes.get(id);
  if (n) return n;
  const kind = nx.kind(id);
  if (kind === 3) n = new Text(undefined, id);
  else if (kind === 8) n = new Comment(undefined, id);
  else {
    const tag = nx.tag(id);
    const C = customTags.get(tag) || tagClasses[tag] || HTMLElement;
    constructingId = id;
    try { n = new C(); } catch (e) { report(e, '<' + tag + '> constructor'); n = new HTMLElement(); }
    constructingId = 0;
  }
  return n;
}
const ids = list => Array.from(list, wrap);

class NodeList extends Array {
  item(i) { return this[i] || null; }
}
function nodeList(arr) { const l = new NodeList(); for (const x of arr) l.push(x); return l; }

class Node extends EventTarget {
  constructor(id) {
    super();
    this._id = id;
    if (id) nodes.set(id, this);
  }
  get nodeType() { return 1; }
  get ownerDocument() { return document; }
  get parentNode() { const p = nx.parent(this._id); return p ? wrap(p) : (this._id === nx.root() ? document : null); }
  get parentElement() { const p = nx.parent(this._id); return p ? wrap(p) : null; }
  get childNodes() { return nodeList(ids(nx.kids(this._id))); }
  get firstChild() { const k = nx.kids(this._id); return k.length ? wrap(k[0]) : null; }
  get lastChild() { const k = nx.kids(this._id); return k.length ? wrap(k[k.length - 1]) : null; }
  _sibling(step, elementsOnly) {
    const p = nx.parent(this._id);
    if (!p) return null;
    const k = nx.kids(p);
    for (let i = k.indexOf(this._id) + step; i >= 0 && i < k.length; i += step)
      if (!elementsOnly || nx.kind(k[i]) === 1) return wrap(k[i]);
    return null;
  }
  get nextSibling() { return this._sibling(1, false); }
  get previousSibling() { return this._sibling(-1, false); }
  get isConnected() { let id = this._id, r = nx.root(); while (id && id !== r) id = nx.parent(id); return id === r; }
  get textContent() { return nx.text(this._id); }
  set textContent(v) { nx.setText(this._id, v == null ? '' : String(v)); }
  get nodeValue() { return null; }
  set nodeValue(v) {}
  hasChildNodes() { return nx.kids(this._id).length > 0; }
  contains(o) { for (let n = o; n; n = n.parentNode) if (n === this) return true; return false; }
  getRootNode() { return this.isConnected ? document : this; }
  isSameNode(o) { return o === this; }
  isEqualNode(o) { return !!o && o.outerHTML === this.outerHTML; }
  compareDocumentPosition(o) { return this.contains(o) ? 20 : o && o.contains(this) ? 10 : 4; }
  normalize() {}
  appendChild(c) { return this.insertBefore(c, null); }
  insertBefore(c, ref) {
    if (c instanceof DocumentFragment) { for (const k of c.childNodes) this.insertBefore(k, ref); return c; }
    if (!(c instanceof Node)) throw new TypeError('insertBefore: not a Node');
    nx.insert(this._id, c._id, ref ? ref._id : 0);
    connected(c);
    return c;
  }
  removeChild(c) { nx.remove(this._id, c._id); return c; }
  replaceChild(n, old) { this.insertBefore(n, old); this.removeChild(old); return old; }
  cloneNode(deep) { return wrap(cloneId(this._id, deep)); }
}
Object.assign(Node, { ELEMENT_NODE: 1, ATTRIBUTE_NODE: 2, TEXT_NODE: 3, COMMENT_NODE: 8, DOCUMENT_NODE: 9,
  DOCUMENT_FRAGMENT_NODE: 11, DOCUMENT_POSITION_CONTAINED_BY: 16, DOCUMENT_POSITION_CONTAINS: 8,
  DOCUMENT_POSITION_FOLLOWING: 4, DOCUMENT_POSITION_PRECEDING: 2 });

function cloneId(id, deep) {
  if (nx.kind(id) === 3) return nx.createText(nx.text(id));
  const c = nx.create(nx.tag(id) || 'div');
  for (const a of nx.attrNames(id)) nx.setAttr(c, a, nx.attr(id, a));
  if (deep) for (const k of nx.kids(id)) nx.insert(c, cloneId(k, true), 0);
  return c;
}

function connected(n) {
  if (n.connectedCallback && n.isConnected) { try { n.connectedCallback(); } catch (e) { report(e, 'connectedCallback'); } }
}

function toNode(x) { return x instanceof Node ? x : document.createTextNode(String(x)); }
const childMixin = {
  append(...xs) { for (const x of xs) this.appendChild(toNode(x)); },
  prepend(...xs) { const f = this.firstChild; for (const x of xs) this.insertBefore(toNode(x), f); },
  replaceChildren(...xs) { nx.setHTML(this._id, ''); this.append(...xs); },
  before(...xs) { const p = this.parentNode; if (p) for (const x of xs) p.insertBefore(toNode(x), this); },
  after(...xs) { const p = this.parentNode, n = this.nextSibling; if (p) for (const x of xs) p.insertBefore(toNode(x), n); },
  replaceWith(...xs) { const p = this.parentNode; if (p) { this.before(...xs); p.removeChild(this); } },
  remove() { const p = this.parentNode; if (p && p !== document) p.removeChild(this); },
};

class CharacterData extends Node {
  get data() { return nx.text(this._id); }
  set data(v) { nx.setText(this._id, String(v)); }
  get nodeValue() { return this.data; }
  set nodeValue(v) { this.data = v; }
  get length() { return this.data.length; }
  get nextElementSibling() { return this._sibling(1, true); }
  get previousElementSibling() { return this._sibling(-1, true); }
}
Object.assign(CharacterData.prototype, childMixin);
class Text extends CharacterData {
  constructor(data, id) { super(id || nx.createText(data == null ? '' : String(data))); }
  get nodeType() { return 3; }
  get nodeName() { return '#text'; }
  get wholeText() { return this.data; }
}
class Comment extends CharacterData {
  constructor(data, id) { super(id || nx.createText('')); }
  get nodeType() { return 8; }
  get nodeName() { return '#comment'; }
}

// ---------------------------------------------------------------------------
// elements
// ---------------------------------------------------------------------------
class DOMTokenList {
  constructor(el, attr) { this._el = el; this._attr = attr; }
  _get() { return (nx.attr(this._el._id, this._attr) || '').split(/\s+/).filter(Boolean); }
  _set(l) { nx.setAttr(this._el._id, this._attr, l.join(' ')); }
  get length() { return this._get().length; }
  get value() { return this._get().join(' '); }
  set value(v) { nx.setAttr(this._el._id, this._attr, String(v)); }
  item(i) { return this._get()[i] || null; }
  contains(t) { return this._get().includes(t); }
  add(...ts) { const l = this._get(); for (const t of ts) if (!l.includes(t)) l.push(t); this._set(l); }
  remove(...ts) { this._set(this._get().filter(x => !ts.includes(x))); }
  toggle(t, force) {
    const has = this.contains(t);
    const want = force === undefined ? !has : !!force;
    if (want && !has) this.add(t); else if (!want && has) this.remove(t);
    return want;
  }
  replace(a, b) { const l = this._get(), i = l.indexOf(a); if (i < 0) return false; l[i] = b; this._set(l); return true; }
  supports() { return true; }
  forEach(fn, self) { this._get().forEach(fn, self); }
  entries() { return this._get().entries(); }
  keys() { return this._get().keys(); }
  values() { return this._get().values(); }
  [Symbol.iterator]() { return this._get()[Symbol.iterator](); }
  toString() { return this.value; }
}

const camelToKebab = s => s.startsWith('--') ? s : s.replace(/[A-Z]/g, m => '-' + m.toLowerCase()).replace(/^(webkit|moz|ms)-/, '-$1-');
function parseStyle(text) {
  const m = new Map();
  for (const part of (text || '').split(';')) {
    const i = part.indexOf(':');
    if (i > 0) m.set(part.slice(0, i).trim().toLowerCase(), part.slice(i + 1).trim());
  }
  return m;
}
function styleObject(el) {
  const read = () => parseStyle(nx.attr(el._id, 'style'));
  const write = m => nx.setAttr(el._id, 'style', Array.from(m, ([k, v]) => k + ': ' + v).join('; '));
  const api = {
    getPropertyValue: p => read().get(p.toLowerCase()) || '',
    setProperty: (p, v, prio) => { const m = read(); if (v == null || v === '') m.delete(p.toLowerCase()); else m.set(p.toLowerCase(), String(v) + (prio ? ' !important' : '')); write(m); },
    removeProperty: p => { const m = read(), old = m.get(p.toLowerCase()) || ''; m.delete(p.toLowerCase()); write(m); return old; },
    item: i => Array.from(read().keys())[i] || '',
  };
  return new Proxy(api, {
    get(t, k) {
      if (typeof k !== 'string') return undefined;
      if (k in api) return api[k];
      if (k === 'cssText') return nx.attr(el._id, 'style') || '';
      if (k === 'length') return read().size;
      return read().get(camelToKebab(k)) || '';
    },
    set(t, k, v) {
      if (k === 'cssText') { nx.setAttr(el._id, 'style', String(v)); return true; }
      api.setProperty(camelToKebab(String(k)), v);
      return true;
    },
  });
}

function rectOf(el) {
  const r = nx.rect(el._id), v = nx.viewport();
  const top = r[1] - v[2];
  return { x: r[0], y: top, left: r[0], top, right: r[0] + r[2], bottom: top + r[3], width: r[2], height: r[3],
    toJSON() { return this; } };
}

class Element extends Node {
  constructor(id) {
    let real = id || constructingId;
    constructingId = 0;   // only the wrapper being built gets it, not elements its constructor makes
    if (!real) real = nx.create(customNames.get(new.target) || 'div');
    super(real);
  }
  get nodeType() { return 1; }
  get tagName() { return nx.tag(this._id).toUpperCase(); }
  get nodeName() { return this.tagName; }
  get localName() { return nx.tag(this._id); }
  get namespaceURI() { return 'http://www.w3.org/1999/xhtml'; }
  get id() { return nx.attr(this._id, 'id') || ''; }
  set id(v) { nx.setAttr(this._id, 'id', String(v)); }
  get className() { return nx.attr(this._id, 'class') || ''; }
  set className(v) { nx.setAttr(this._id, 'class', String(v)); }
  get classList() { return new DOMTokenList(this, 'class'); }
  set classList(v) { this.className = v; }
  get style() { return styleObject(this); }
  set style(v) { nx.setAttr(this._id, 'style', String(v)); }
  get dataset() {
    const el = this;
    const toAttr = k => 'data-' + camelToKebab(k);
    return new Proxy({}, {
      get: (t, k) => typeof k === 'string' ? (nx.attr(el._id, toAttr(k)) ?? undefined) : undefined,
      set: (t, k, v) => { nx.setAttr(el._id, toAttr(k), String(v)); return true; },
      deleteProperty: (t, k) => { nx.delAttr(el._id, toAttr(k)); return true; },
      has: (t, k) => nx.attr(el._id, toAttr(k)) !== null,
      ownKeys: () => nx.attrNames(el._id).filter(a => a.startsWith('data-')).map(a => a.slice(5).replace(/-([a-z])/g, (m, c) => c.toUpperCase())),
      getOwnPropertyDescriptor: (t, k) => ({ enumerable: true, configurable: true, value: nx.attr(el._id, toAttr(k)) }),
    });
  }
  getAttribute(n) { return nx.attr(this._id, String(n).toLowerCase()); }
  getAttributeNS(ns, n) { return this.getAttribute(n); }
  setAttribute(n, v) {
    n = String(n).toLowerCase();
    nx.setAttr(this._id, n, String(v));
    if (n === 'src' && this.localName === 'img') setTimeout(() => this.dispatchEvent(new Event('load')), 0);
  }
  setAttributeNS(ns, n, v) { this.setAttribute(n, v); }
  removeAttribute(n) { nx.delAttr(this._id, String(n).toLowerCase()); }
  hasAttribute(n) { return nx.attr(this._id, String(n).toLowerCase()) !== null; }
  hasAttributes() { return nx.attrNames(this._id).length > 0; }
  toggleAttribute(n, force) {
    const has = this.hasAttribute(n), want = force === undefined ? !has : !!force;
    if (want && !has) this.setAttribute(n, ''); else if (!want && has) this.removeAttribute(n);
    return want;
  }
  getAttributeNames() { return nx.attrNames(this._id); }
  get attributes() {
    const list = nx.attrNames(this._id).map(name => ({ name, localName: name, value: nx.attr(this._id, name) }));
    list.getNamedItem = n => list.find(a => a.name === n) || null;
    list.item = i => list[i] || null;
    return list;
  }
  get innerHTML() { return nx.html(this._id); }
  set innerHTML(v) { nx.setHTML(this._id, v == null ? '' : String(v)); }
  get outerHTML() { return nx.outer(this._id); }
  set outerHTML(v) {
    const p = this.parentNode;
    if (!p) return;
    const t = document.createElement('div');
    t.innerHTML = v;
    for (const k of t.childNodes) p.insertBefore(k, this);
    p.removeChild(this);
  }
  get innerText() { return this.textContent; }
  set innerText(v) { this.textContent = v; }
  get outerText() { return this.textContent; }
  insertAdjacentHTML(pos, html) {
    const t = document.createElement('div');
    t.innerHTML = html;
    this._insertAdjacent(pos, t.childNodes);
  }
  insertAdjacentElement(pos, el) { this._insertAdjacent(pos, [el]); return el; }
  insertAdjacentText(pos, text) { this._insertAdjacent(pos, [document.createTextNode(text)]); }
  _insertAdjacent(pos, list) {
    pos = String(pos).toLowerCase();
    for (const n of list) {
      if (pos === 'beforebegin') this.parentNode && this.parentNode.insertBefore(n, this);
      else if (pos === 'afterbegin') this.insertBefore(n, this.firstChild);
      else if (pos === 'beforeend') this.appendChild(n);
      else if (pos === 'afterend') this.parentNode && this.parentNode.insertBefore(n, this.nextSibling);
    }
  }
  get children() { return nodeList(ids(nx.kids(this._id).filter(k => nx.kind(k) === 1))); }
  get childElementCount() { return this.children.length; }
  get firstElementChild() { return this.children[0] || null; }
  get lastElementChild() { const c = this.children; return c[c.length - 1] || null; }
  get nextElementSibling() { return this._sibling(1, true); }
  get previousElementSibling() { return this._sibling(-1, true); }
  querySelectorAll(sel) { return nodeList(ids(nx.select(this._id, fixSelector(sel)))); }
  querySelector(sel) { const r = nx.select(this._id, fixSelector(sel)); return r.length ? wrap(r[0]) : null; }
  getElementsByTagName(t) { return this.querySelectorAll(t === '*' ? '*' : t); }
  getElementsByClassName(c) { return this.querySelectorAll(String(c).trim().split(/\s+/).map(x => '.' + x).join('')); }
  matches(sel) { return nx.matches(this._id, fixSelector(sel)); }
  webkitMatchesSelector(sel) { return this.matches(sel); }
  closest(sel) { for (let n = this; n && n instanceof Element; n = n.parentElement) if (n.matches(sel)) return n; return null; }
  getBoundingClientRect() { return rectOf(this); }
  getClientRects() { return [rectOf(this)]; }
  get offsetWidth() { return nx.rect(this._id)[2]; }
  get offsetHeight() { return nx.rect(this._id)[3]; }
  get offsetLeft() { return nx.rect(this._id)[0]; }
  get offsetTop() { return nx.rect(this._id)[1]; }
  get offsetParent() { return this.parentElement; }
  get clientWidth() { return this.offsetWidth; }
  get clientHeight() { return this.offsetHeight; }
  get clientTop() { return 0; }
  get clientLeft() { return 0; }
  get scrollWidth() { return this.offsetWidth; }
  get scrollHeight() { return this.offsetHeight; }
  get scrollTop() { return this === document.documentElement || this === document.body ? G.scrollY : 0; }
  set scrollTop(v) { if (this === document.documentElement || this === document.body) nx.scrollTo(v); }
  get scrollLeft() { return 0; }
  set scrollLeft(v) {}
  scrollIntoView() { nx.scrollTo(Math.max(0, nx.rect(this._id)[1] - 16)); }
  scrollTo(x, y) { if (this === document.documentElement || this === document.body) G.scrollTo(x, y); }
  scrollBy() {}
  focus() { this.dispatchEvent(new FocusEvent('focus')); }
  blur() { this.dispatchEvent(new FocusEvent('blur')); }
  animate() { return { finished: Promise.resolve(), cancel() {}, play() {}, pause() {}, onfinish: null }; }
  getAnimations() { return []; }
  attachShadow() { return this; }   // ponytail: no shadow DOM; the "shadow" content renders as children
  get shadowRoot() { return null; }
  click() {
    const ev = new MouseEvent('click', { bubbles: true, cancelable: true });
    if (this.dispatchEvent(ev)) activate(this);
  }
}
Object.assign(Element.prototype, childMixin);

// What a click does when no handler prevented it (for clicks made by scripts with el.click()).
function activate(el) {
  const a = el.closest('a[href]');
  if (a) { const h = a.getAttribute('href'); if (!/^javascript:/i.test(h)) nx.navigate(h); return; }
  if (el.localName === 'input' && (el.type === 'checkbox' || el.type === 'radio')) { el.checked = el.type === 'radio' ? true : !el.checked; el.dispatchEvent(new Event('change', { bubbles: true })); return; }
  if ((el.localName === 'button' || el.localName === 'input') && el.form && /^(submit|image)$/.test(el.type)) el.form.requestSubmit(el);
}

const reflect = (C, props, num) => {
  for (const p of props) Object.defineProperty(C.prototype, p, {
    get() { const v = nx.attr(this._id, p.toLowerCase()); return num ? Number(v || 0) : v || ''; },
    set(v) { nx.setAttr(this._id, p.toLowerCase(), String(v)); }, configurable: true,
  });
};
const reflectBool = (C, props) => {
  for (const p of props) Object.defineProperty(C.prototype, p, {
    get() { return nx.attr(this._id, p.toLowerCase()) !== null; },
    set(v) { if (v) nx.setAttr(this._id, p.toLowerCase(), ''); else nx.delAttr(this._id, p.toLowerCase()); }, configurable: true,
  });
};
const urlProp = (C, p) => Object.defineProperty(C.prototype, p, {
  get() { const v = nx.attr(this._id, p); return v === null ? '' : nx.resolve(nx.url(), v); },
  set(v) { this.setAttribute(p, String(v)); }, configurable: true,
});

class HTMLElement extends Element {}
reflect(HTMLElement, ['title', 'lang', 'dir', 'accessKey']);
reflectBool(HTMLElement, ['hidden', 'inert']);
Object.defineProperty(HTMLElement.prototype, 'tabIndex', { get() { return Number(nx.attr(this._id, 'tabindex') || -1); }, set(v) { nx.setAttr(this._id, 'tabindex', String(v)); } });
Object.defineProperty(HTMLElement.prototype, 'isContentEditable', { get() { return false; } });

function checkedOf(id) { const v = nx.attr(id, 'nx-checked'); return v !== null ? v === '1' : nx.attr(id, 'checked') !== null; }
class HTMLInputElement extends HTMLElement {
  get value() { return nx.attr(this._id, 'value') || ''; }
  set value(v) { nx.setAttr(this._id, 'value', v == null ? '' : String(v)); }
  get defaultValue() { return this.value; }
  set defaultValue(v) { this.value = v; }
  get checked() { return checkedOf(this._id); }
  set checked(v) {
    if (v && this.type === 'radio' && this.name) {
      const scope = this.form || document;
      for (const r of scope.querySelectorAll('input[type="radio"]')) if (r.name === this.name) nx.setAttr(r._id, 'nx-checked', '0');
    }
    nx.setAttr(this._id, 'nx-checked', v ? '1' : '0');
  }
  get defaultChecked() { return this.hasAttribute('checked'); }
  get type() { return (nx.attr(this._id, 'type') || 'text').toLowerCase(); }
  set type(v) { nx.setAttr(this._id, 'type', String(v)); }
  get form() { return this.closest('form'); }
  get files() { return []; }
  get validity() { return { valid: true }; }
  checkValidity() { return true; }
  reportValidity() { return true; }
  setCustomValidity() {}
  select() {}
  setSelectionRange() {}
  get labels() { return []; }
}
reflect(HTMLInputElement, ['name', 'placeholder', 'autocomplete', 'pattern', 'min', 'max', 'step', 'accept', 'inputMode']);
reflectBool(HTMLInputElement, ['disabled', 'readOnly', 'required', 'multiple', 'autofocus']);
reflect(HTMLInputElement, ['maxLength', 'size'], true);
class HTMLTextAreaElement extends HTMLElement {
  get value() { const v = nx.attr(this._id, 'value'); return v !== null ? v : nx.text(this._id); }
  set value(v) { nx.setAttr(this._id, 'value', String(v)); }
  get type() { return 'textarea'; }
  get form() { return this.closest('form'); }
  select() {}
}
reflect(HTMLTextAreaElement, ['name', 'placeholder']);
reflectBool(HTMLTextAreaElement, ['disabled', 'readOnly', 'required']);
class HTMLOptionElement extends HTMLElement {
  get value() { const v = nx.attr(this._id, 'value'); return v !== null ? v : this.text; }
  set value(v) { nx.setAttr(this._id, 'value', String(v)); }
  get text() { return this.textContent.trim(); }
  set text(v) { this.textContent = v; }
  get label() { return nx.attr(this._id, 'label') || this.text; }
  get index() { const s = this.closest('select'); return s ? Array.prototype.indexOf.call(s.options, this) : 0; }
  get selected() { const s = this.closest('select'); return !!s && s.selectedIndex === this.index; }
  set selected(v) { const s = this.closest('select'); if (s && v) s.selectedIndex = this.index; }
}
reflectBool(HTMLOptionElement, ['disabled', 'defaultSelected']);
class HTMLSelectElement extends HTMLElement {
  get options() { const o = this.querySelectorAll('option'); o.selectedIndex = this.selectedIndex; return o; }
  get selectedIndex() {
    const v = nx.attr(this._id, 'nx-selected');
    if (v !== null) return Number(v);
    const o = this.querySelectorAll('option');
    const i = o.findIndex(x => x.hasAttribute('selected'));
    return i >= 0 ? i : o.length ? 0 : -1;
  }
  set selectedIndex(i) { nx.setAttr(this._id, 'nx-selected', String(i)); }
  get value() { const o = this.options[this.selectedIndex]; return o ? o.value : ''; }
  set value(v) { const i = this.options.findIndex(o => o.value === String(v)); if (i >= 0) this.selectedIndex = i; }
  get selectedOptions() { const o = this.options[this.selectedIndex]; return o ? [o] : []; }
  get length() { return this.options.length; }
  get type() { return this.multiple ? 'select-multiple' : 'select-one'; }
  get form() { return this.closest('form'); }
  add(o) { this.appendChild(o); }
  item(i) { return this.options[i] || null; }
}
reflect(HTMLSelectElement, ['name']);
reflectBool(HTMLSelectElement, ['disabled', 'multiple', 'required']);
class HTMLButtonElement extends HTMLElement {
  get type() { return (nx.attr(this._id, 'type') || 'submit').toLowerCase(); }
  set type(v) { nx.setAttr(this._id, 'type', String(v)); }
  get form() { return this.closest('form'); }
  get value() { return nx.attr(this._id, 'value') || ''; }
  set value(v) { nx.setAttr(this._id, 'value', String(v)); }
}
reflect(HTMLButtonElement, ['name']);
reflectBool(HTMLButtonElement, ['disabled']);
class HTMLFormElement extends HTMLElement {
  constructor(id) {
    super(id);
    // form.q / form['email']: named controls read as properties, like in real browsers.
    const proxy = new Proxy(this, {
      get(t, k) {
        if (typeof k === 'string' && !(k in t)) {
          const hit = t.querySelector('[name="' + k.replace(/"/g, '\\"') + '"], [id="' + k.replace(/"/g, '\\"') + '"]');
          if (hit) return hit;
        }
        return Reflect.get(t, k, t);
      },
      set(t, k, v) { t[k] = v; return true; },
    });
    nodes.set(this._id, proxy);
    return proxy;
  }
  get elements() { return this.querySelectorAll('input, select, textarea, button'); }
  get length() { return this.elements.length; }
  submit() { nx.submit(this._id, 0); }
  requestSubmit(btn) {
    if (this.dispatchEvent(new SubmitEvent('submit', { bubbles: true, cancelable: true, submitter: btn || null })))
      nx.submit(this._id, btn ? btn._id : 0);
  }
  reset() { for (const e of this.elements) { nx.delAttr(e._id, 'nx-checked'); nx.delAttr(e._id, 'nx-selected'); } }
  checkValidity() { return true; }
  reportValidity() { return true; }
  get action() { return nx.resolve(nx.url(), nx.attr(this._id, 'action') || ''); }
  set action(v) { nx.setAttr(this._id, 'action', String(v)); }
  get method() { return (nx.attr(this._id, 'method') || 'get').toLowerCase(); }
  set method(v) { nx.setAttr(this._id, 'method', String(v)); }
}
reflect(HTMLFormElement, ['name', 'target', 'enctype']);
class HTMLAnchorElement extends HTMLElement {
  _url() { return new URL(this.href || 'about:blank'); }
  get protocol() { return this._url().protocol; }
  get host() { return this._url().host; }
  get hostname() { return this._url().hostname; }
  get port() { return this._url().port; }
  get pathname() { return this._url().pathname; }
  get search() { return this._url().search; }
  get hash() { return this._url().hash; }
  get origin() { return this._url().origin; }
  get text() { return this.textContent; }
  toString() { return this.href; }
}
urlProp(HTMLAnchorElement, 'href');
reflect(HTMLAnchorElement, ['target', 'rel', 'download', 'type']);
class HTMLImageElement extends HTMLElement {
  get complete() { return true; }
  get naturalWidth() { return this.offsetWidth; }
  get naturalHeight() { return this.offsetHeight; }
  get currentSrc() { return this.src; }
  decode() { return Promise.resolve(); }
}
urlProp(HTMLImageElement, 'src');
reflect(HTMLImageElement, ['alt', 'srcset', 'sizes', 'loading', 'decoding', 'crossOrigin']);
reflect(HTMLImageElement, ['width', 'height'], true);
class HTMLScriptElement extends HTMLElement {
  get text() { return nx.text(this._id); }
  set text(v) { nx.setText(this._id, String(v)); }
}
urlProp(HTMLScriptElement, 'src');
reflect(HTMLScriptElement, ['type', 'charset', 'crossOrigin', 'integrity', 'nonce']);
reflectBool(HTMLScriptElement, ['async', 'defer', 'noModule']);
class HTMLLinkElement extends HTMLElement {}
urlProp(HTMLLinkElement, 'href');
reflect(HTMLLinkElement, ['rel', 'type', 'media', 'as', 'crossOrigin']);
class HTMLIFrameElement extends HTMLElement { get contentWindow() { return null; } get contentDocument() { return null; } }
urlProp(HTMLIFrameElement, 'src');
class HTMLCanvasElement extends HTMLElement {
  getContext() { return null; }   // no canvas drawing
  toDataURL() { return 'data:,'; }
}
reflect(HTMLCanvasElement, ['width', 'height'], true);
class HTMLMediaElement extends HTMLElement {
  play() { return Promise.reject(new DOMException('Media playback is not supported', 'NotSupportedError')); }
  pause() {}
  load() {}
  canPlayType() { return ''; }
  get paused() { return true; }
}
urlProp(HTMLMediaElement, 'src');
class HTMLTemplateElement extends HTMLElement {
  get content() { const f = document.createDocumentFragment(); for (const k of this.childNodes) f.appendChild(k.cloneNode(true)); return f; }
}
class HTMLLabelElement extends HTMLElement {
  get htmlFor() { return nx.attr(this._id, 'for') || ''; }
  get control() { const f = this.htmlFor; return f ? document.getElementById(f) : this.querySelector('input, select, textarea'); }
}
class HTMLDialogElement extends HTMLElement {
  get open() { return this.hasAttribute('open'); }
  show() { this.setAttribute('open', ''); }
  showModal() { this.setAttribute('open', ''); }
  close(v) { this.removeAttribute('open'); this.returnValue = v || ''; this.dispatchEvent(new Event('close')); }
}
class HTMLDetailsElement extends HTMLElement {
  get open() { return this.hasAttribute('open'); }
  set open(v) { this.toggleAttribute('open', !!v); }
}
class SVGElement extends Element {}
const tagClasses = {
  input: HTMLInputElement, textarea: HTMLTextAreaElement, select: HTMLSelectElement, option: HTMLOptionElement,
  button: HTMLButtonElement, form: HTMLFormElement, a: HTMLAnchorElement, img: HTMLImageElement,
  script: HTMLScriptElement, link: HTMLLinkElement, iframe: HTMLIFrameElement, canvas: HTMLCanvasElement,
  video: HTMLMediaElement, audio: HTMLMediaElement, template: HTMLTemplateElement, label: HTMLLabelElement,
  dialog: HTMLDialogElement, details: HTMLDetailsElement, svg: SVGElement, path: SVGElement,
};

class DocumentFragment extends Node {
  constructor() { super(nx.create('div')); }
  get nodeType() { return 11; }
  get nodeName() { return '#document-fragment'; }
  get children() { return nodeList(ids(nx.kids(this._id).filter(k => nx.kind(k) === 1))); }
  get firstElementChild() { return this.children[0] || null; }
  querySelector(s) { return Element.prototype.querySelector.call(this, s); }
  querySelectorAll(s) { return Element.prototype.querySelectorAll.call(this, s); }
  getElementById(id) { return this.querySelector('[id="' + String(id).replace(/"/g, '\\"') + '"]'); }
  get innerHTML() { return nx.html(this._id); }
}
Object.assign(DocumentFragment.prototype, { append: childMixin.append, prepend: childMixin.prepend, replaceChildren: childMixin.replaceChildren });

// litehtml's selector parser chokes on a few modern forms: drop :scope, keep the rest.
function fixSelector(sel) { return String(sel).replace(/:scope\s*>?\s*/g, ''); }

// ---------------------------------------------------------------------------
// document
// ---------------------------------------------------------------------------
class Document extends Node {
  constructor() { super(0); }
  get nodeType() { return 9; }
  get nodeName() { return '#document'; }
  get documentElement() { return wrap(nx.root()); }
  get parentNode() { return null; }
  get childNodes() { return nodeList([this.documentElement]); }
  get firstChild() { return this.documentElement; }
  get children() { return nodeList([this.documentElement]); }
  get head() { return this.querySelector('head'); }
  get body() { return this.querySelector('body'); }
  get title() { const t = this.querySelector('title'); return t ? t.textContent.trim() : ''; }
  set title(v) { let t = this.querySelector('title'); if (!t && this.head) { t = this.createElement('title'); this.head.appendChild(t); } if (t) t.textContent = v; }
  get readyState() { return readyState; }
  get currentScript() { return currentScript ? wrap(currentScript) : null; }
  get location() { return G.location; }
  set location(v) { G.location.href = v; }
  get URL() { return nx.url(); }
  get documentURI() { return nx.url(); }
  get baseURI() { return nx.url(); }
  get domain() { return G.location.hostname; }
  get referrer() { return ''; }
  get cookie() { return nx.cookie(); }
  set cookie(v) { nx.cookie(String(v)); }
  get defaultView() { return G; }
  get activeElement() { return this.body; }
  get visibilityState() { return 'visible'; }
  get hidden() { return false; }
  get characterSet() { return 'UTF-8'; }
  get charset() { return 'UTF-8'; }
  get compatMode() { return 'CSS1Compat'; }
  get contentType() { return 'text/html'; }
  get scrollingElement() { return this.documentElement; }
  get forms() { return this.querySelectorAll('form'); }
  get images() { return this.querySelectorAll('img'); }
  get links() { return this.querySelectorAll('a[href]'); }
  get scripts() { return this.querySelectorAll('script'); }
  get fonts() { return { ready: Promise.resolve(), load: () => Promise.resolve([]), check: () => true, add() {}, addEventListener() {} }; }
  get implementation() { return { hasFeature: () => true, createHTMLDocument: () => new DOMParser().parseFromString('', 'text/html') }; }
  hasFocus() { return true; }
  querySelector(s) { return this.documentElement.matches(s) ? this.documentElement : this.documentElement.querySelector(s); }
  querySelectorAll(s) { return this.documentElement.querySelectorAll(s); }
  getElementById(id) { return this.querySelector('[id="' + String(id).replace(/"/g, '\\"') + '"]'); }
  getElementsByTagName(t) { return this.querySelectorAll(t); }
  getElementsByClassName(c) { return this.documentElement.getElementsByClassName(c); }
  getElementsByName(n) { return this.querySelectorAll('[name="' + String(n).replace(/"/g, '\\"') + '"]'); }
  createElement(tag) { return wrap(nx.create(String(tag).toLowerCase())); }
  createElementNS(ns, tag) { return this.createElement(String(tag).replace(/^.*:/, '')); }
  createTextNode(t) { return new Text(t); }
  createComment(t) { return new Comment(t); }
  createDocumentFragment() { return new DocumentFragment(); }
  createEvent(type) { return /mouse/i.test(type) ? new MouseEvent('') : /custom/i.test(type) ? new CustomEvent('') : new Event(''); }
  createRange() { return { setStart() {}, setEnd() {}, selectNodeContents() {}, collapse() {}, getBoundingClientRect: () => rectOf(this.body), createContextualFragment: h => { const f = this.createDocumentFragment(); const d = this.createElement('div'); d.innerHTML = h; for (const k of d.childNodes) f.appendChild(k); return f; } }; }
  createTreeWalker(root) { const all = [root, ...root.querySelectorAll('*')]; let i = 0; return { currentNode: root, nextNode() { return (this.currentNode = all[++i] || null); } }; }
  elementFromPoint() { return this.body; }
  getSelection() { return G.getSelection(); }
  write(...h) { nx.write(h.join('')); }
  writeln(...h) { nx.write(h.join('') + '\n'); }
  open() {}
  close() {}
  execCommand() { return false; }
  importNode(n, deep) { return n.cloneNode(deep); }
  adoptNode(n) { return n; }
  contains(n) { return !!n && (n === this || n.isConnected); }
}
const document = new Document();
G.document = document;

// ---------------------------------------------------------------------------
// URL, timers, storage, fetch
// ---------------------------------------------------------------------------
const URL_RE = /^([a-zA-Z][a-zA-Z0-9+.-]*:)(?:\/\/(?:([^:@\/]*)(?::([^@\/]*))?@)?([^\/?#:]*)(?::(\d*))?)?([^?#]*)(\?[^#]*)?(#.*)?$/;
class URLSearchParams {
  constructor(init) {
    this._l = [];
    if (init instanceof URLSearchParams) this._l = init._l.map(p => p.slice());
    else if (Array.isArray(init)) for (const [k, v] of init) this.append(k, v);
    else if (init && typeof init === 'object') for (const k of Object.keys(init)) this.append(k, init[k]);
    else if (init) {
      for (const part of String(init).replace(/^\?/, '').split('&')) {
        if (!part) continue;
        const i = part.indexOf('=');
        const dec = s => { try { return decodeURIComponent(s.replace(/\+/g, ' ')); } catch (e) { return s; } };
        this._l.push(i < 0 ? [dec(part), ''] : [dec(part.slice(0, i)), dec(part.slice(i + 1))]);
      }
    }
  }
  _changed() { if (this._url) this._url._setSearch(this.toString()); }
  append(k, v) { this._l.push([String(k), String(v)]); this._changed(); }
  set(k, v) { const i = this._l.findIndex(p => p[0] === k); if (i < 0) this._l.push([String(k), String(v)]); else { this._l[i][1] = String(v); this._l = this._l.filter((p, j) => p[0] !== k || j === i); } this._changed(); }
  get(k) { const p = this._l.find(p => p[0] === k); return p ? p[1] : null; }
  getAll(k) { return this._l.filter(p => p[0] === k).map(p => p[1]); }
  has(k) { return this._l.some(p => p[0] === k); }
  delete(k) { this._l = this._l.filter(p => p[0] !== k); this._changed(); }
  sort() { this._l.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0); this._changed(); }
  forEach(fn, self) { for (const [k, v] of this._l) fn.call(self, v, k, this); }
  get size() { return this._l.length; }
  keys() { return this._l.map(p => p[0])[Symbol.iterator](); }
  values() { return this._l.map(p => p[1])[Symbol.iterator](); }
  entries() { return this._l.map(p => p.slice())[Symbol.iterator](); }
  [Symbol.iterator]() { return this.entries(); }
  toString() {
    const enc = s => encodeURIComponent(s).replace(/%20/g, '+').replace(/[!'()~]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase());
    return this._l.map(([k, v]) => enc(k) + '=' + enc(v)).join('&');
  }
}
class URL {
  constructor(u, base) {
    let s = String(u);
    if (!/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(s)) {
      if (base === undefined) throw new TypeError("Invalid URL: " + s);
      s = nx.resolve(String(base), s);
    }
    this._parse(s);
  }
  _parse(s) {
    const m = URL_RE.exec(s);
    if (!m) throw new TypeError('Invalid URL: ' + s);
    this.protocol = m[1].toLowerCase();
    this.username = m[2] || '';
    this.password = m[3] || '';
    this.hostname = (m[4] || '').toLowerCase();
    this.port = m[5] || '';
    this.pathname = m[6] || (m[4] !== undefined ? '/' : '');
    this._search = m[7] && m[7] !== '?' ? m[7] : '';
    this.hash = m[8] && m[8] !== '#' ? m[8] : '';
    this._sp = null;
  }
  get host() { return this.hostname + (this.port ? ':' + this.port : ''); }
  set host(v) { const [h, p] = String(v).split(':'); this.hostname = h; this.port = p || ''; }
  get origin() { return this.hostname ? this.protocol + '//' + this.host : 'null'; }
  get search() { return this._search; }
  set search(v) { v = String(v); this._search = v && v !== '?' ? (v[0] === '?' ? v : '?' + v) : ''; this._sp = null; }
  _setSearch(q) { this._search = q ? '?' + q : ''; }
  get searchParams() { if (!this._sp) { this._sp = new URLSearchParams(this._search); this._sp._url = this; } return this._sp; }
  get href() {
    const auth = this.username ? this.username + (this.password ? ':' + this.password : '') + '@' : '';
    return this.protocol + (this.hostname || this.protocol === 'file:' ? '//' + auth + this.host : '') + this.pathname + this._search + this.hash;
  }
  set href(v) { this._parse(String(v)); }
  toString() { return this.href; }
  toJSON() { return this.href; }
  static canParse(u, b) { try { new URL(u, b); return true; } catch (e) { return false; } }
  static createObjectURL() { return 'blob:browsenx'; }
  static revokeObjectURL() {}
}

class Location {
  get href() { return nx.url(); }
  set href(v) { nx.navigate(String(v)); }
  _u() { return new URL(nx.url()); }
  get protocol() { return this._u().protocol; }
  get host() { return this._u().host; }
  get hostname() { return this._u().hostname; }
  get port() { return this._u().port; }
  get pathname() { return this._u().pathname; }
  set pathname(v) { const u = this._u(); u.pathname = v; nx.navigate(u.href); }
  get search() { return this._u().search; }
  set search(v) { const u = this._u(); u.search = v; nx.navigate(u.href); }
  get hash() { return this._u().hash; }
  set hash(v) { const u = this._u(); u.hash = v ? (String(v)[0] === '#' ? v : '#' + v) : ''; nx.navigate(u.href); }
  get origin() { return this._u().origin; }
  assign(u) { nx.navigate(String(u)); }
  replace(u) { nx.navigate(String(u), 'GET', '', true); }
  reload() { nx.navigate(nx.url(), 'GET', '', true); }
  toString() { return nx.url(); }
}

// Timers and animation frames run from __nxTick, which the browser calls every frame.
let timerSeq = 0, now = 0;
const timerList = new Map();
let rafList = [];
function addTimer(fn, ms, args, repeat) {
  const id = ++timerSeq;
  if (typeof fn === 'string') { const code = fn; fn = () => (0, eval)(code); }
  if (typeof fn !== 'function') return id;
  const delay = Math.max(repeat ? 10 : 0, Number(ms) || 0);
  timerList.set(id, { fn, args, at: nx.now() + delay, every: repeat ? delay : 0 });
  return id;
}
G.setTimeout = (fn, ms, ...a) => addTimer(fn, ms, a, false);
G.setInterval = (fn, ms, ...a) => addTimer(fn, ms, a, true);
G.clearTimeout = G.clearInterval = id => { timerList.delete(id); };
G.requestAnimationFrame = fn => { const id = ++timerSeq; rafList.push({ id, fn }); return id; };
G.cancelAnimationFrame = id => { rafList = rafList.filter(r => r.id !== id); };
G.requestIdleCallback = fn => G.setTimeout(() => fn({ didTimeout: false, timeRemaining: () => 10 }), 1);
G.cancelIdleCallback = G.clearTimeout;
G.queueMicrotask = fn => { Promise.resolve().then(fn); };

class Storage {
  constructor(local) { this._local = local; this._m = new Map(); }
  getItem(k) { return this._local ? nx.storage('get', String(k)) : (this._m.has(String(k)) ? this._m.get(String(k)) : null); }
  setItem(k, v) { if (this._local) nx.storage('set', String(k), String(v)); else this._m.set(String(k), String(v)); }
  removeItem(k) { if (this._local) nx.storage('del', String(k)); else this._m.delete(String(k)); }
  clear() { if (this._local) nx.storage('clear'); else this._m.clear(); }
  key(i) { const k = this._local ? nx.storage('keys') : Array.from(this._m.keys()); return k[i] ?? null; }
  get length() { return this._local ? nx.storage('keys').length : this._m.size; }
}
const storageProxy = s => new Proxy(s, {
  get: (t, k) => k in t || typeof k !== 'string' ? (typeof t[k] === 'function' ? t[k].bind(t) : t[k]) : t.getItem(k) ?? undefined,
  set: (t, k, v) => { t.setItem(k, v); return true; },
  deleteProperty: (t, k) => { t.removeItem(k); return true; },
});

class Headers {
  constructor(init) {
    this._m = new Map();
    if (init instanceof Headers) init.forEach((v, k) => this.append(k, v));
    else if (Array.isArray(init)) for (const [k, v] of init) this.append(k, v);
    else if (init) for (const k of Object.keys(init)) this.append(k, init[k]);
  }
  append(k, v) { k = String(k).toLowerCase(); this._m.set(k, this._m.has(k) ? this._m.get(k) + ', ' + v : String(v)); }
  set(k, v) { this._m.set(String(k).toLowerCase(), String(v)); }
  get(k) { const v = this._m.get(String(k).toLowerCase()); return v === undefined ? null : v; }
  has(k) { return this._m.has(String(k).toLowerCase()); }
  delete(k) { this._m.delete(String(k).toLowerCase()); }
  forEach(fn, self) { for (const [k, v] of this._m) fn.call(self, v, k, this); }
  entries() { return this._m.entries(); }
  keys() { return this._m.keys(); }
  values() { return this._m.values(); }
  [Symbol.iterator]() { return this._m.entries(); }
}
function parseHeaders(raw) {
  const h = new Headers();
  for (const line of String(raw).split(/\r?\n/)) { const i = line.indexOf(':'); if (i > 0) h.append(line.slice(0, i).trim(), line.slice(i + 1).trim()); }
  return h;
}
class Blob {
  constructor(parts = [], opts = {}) { this._t = parts.map(p => typeof p === 'string' ? p : p && p._t !== undefined ? p._t : String(p)).join(''); this.type = opts.type || ''; }
  get size() { return this._t.length; }
  text() { return Promise.resolve(this._t); }
  arrayBuffer() { return Promise.resolve(new TextEncoder().encode(this._t).buffer); }
  slice(a, b) { return new Blob([this._t.slice(a, b)], { type: this.type }); }
}
class File extends Blob { constructor(parts, name, opts) { super(parts, opts); this.name = name; this.lastModified = Date.now(); } }
class FormData {
  constructor(form) {
    this._l = [];
    if (form instanceof HTMLFormElement) {
      for (const e of form.elements) {
        if (!e.name || e.disabled) continue;
        if ((e.type === 'checkbox' || e.type === 'radio') && !e.checked) continue;
        if (/^(submit|button|reset|image|file)$/.test(e.type)) continue;
        this._l.push([e.name, e.value]);
      }
    }
  }
  append(k, v) { this._l.push([String(k), v instanceof Blob ? v : String(v)]); }
  set(k, v) { this.delete(k); this.append(k, v); }
  get(k) { const p = this._l.find(p => p[0] === k); return p ? p[1] : null; }
  getAll(k) { return this._l.filter(p => p[0] === k).map(p => p[1]); }
  has(k) { return this._l.some(p => p[0] === k); }
  delete(k) { this._l = this._l.filter(p => p[0] !== k); }
  forEach(fn, self) { for (const [k, v] of this._l) fn.call(self, v, k, this); }
  entries() { return this._l[Symbol.iterator](); }
  [Symbol.iterator]() { return this.entries(); }
}
class Response {
  constructor(body, init = {}) {
    this._body = body == null ? '' : String(body);
    this.status = init.status === undefined ? 200 : init.status;
    this.statusText = init.statusText || '';
    this.headers = init.headers instanceof Headers ? init.headers : new Headers(init.headers);
    this.url = init.url || '';
    this.redirected = false;
    this.type = 'basic';
    this.bodyUsed = false;
  }
  get ok() { return this.status >= 200 && this.status < 300; }
  text() { this.bodyUsed = true; return Promise.resolve(this._body); }
  json() { return this.text().then(t => JSON.parse(t)); }
  blob() { return this.text().then(t => new Blob([t], { type: this.headers.get('content-type') || '' })); }
  arrayBuffer() { return this.text().then(t => new TextEncoder().encode(t).buffer); }
  formData() { return this.text().then(t => { const f = new FormData(); for (const [k, v] of new URLSearchParams(t)) f.append(k, v); return f; }); }
  clone() { return new Response(this._body, this); }
  static json(d, init) { return new Response(JSON.stringify(d), init); }
  static error() { return new Response('', { status: 0 }); }
}
class Request {
  constructor(input, init = {}) {
    this.url = input instanceof Request ? input.url : String(input);
    this.method = (init.method || (input instanceof Request ? input.method : 'GET')).toUpperCase();
    this.headers = new Headers(init.headers || (input instanceof Request ? input.headers : undefined));
    this.body = init.body !== undefined ? init.body : input instanceof Request ? input.body : null;
    this.signal = init.signal || null;
  }
  clone() { return new Request(this); }
}
let fetchSeq = 0;
const fetches = new Map();
function bodyString(b, headers) {
  if (b == null) return '';
  if (typeof b === 'string') return b;
  if (b instanceof URLSearchParams) { if (!headers.has('content-type')) headers.set('content-type', 'application/x-www-form-urlencoded;charset=UTF-8'); return b.toString(); }
  if (b instanceof FormData) { if (!headers.has('content-type')) headers.set('content-type', 'application/x-www-form-urlencoded'); return new URLSearchParams(b._l.filter(p => typeof p[1] === 'string')).toString(); }
  if (b instanceof Blob) return b._t;
  if (b instanceof ArrayBuffer || ArrayBuffer.isView(b)) return new TextDecoder().decode(b);
  return String(b);
}
G.fetch = function (input, init = {}) {
  const req = new Request(input, init);
  const body = bodyString(req.body, req.headers);
  return new Promise((resolve, reject) => {
    const id = ++fetchSeq;
    fetches.set(id, { resolve, reject });
    if (req.signal) req.signal.addEventListener('abort', () => { if (fetches.delete(id)) reject(new DOMException('The operation was aborted.', 'AbortError')); });
    const hs = [];
    req.headers.forEach((v, k) => hs.push(k + ': ' + v));
    nx.fetch(id, req.method, req.url, hs, body);
  });
};
G.__nxFetchDone = (id, status, body, headers, url, error) => {
  const f = fetches.get(id);
  if (!f) return;
  fetches.delete(id);
  if (!status) f.reject(new TypeError('Failed to fetch' + (error ? ' (' + error + ')' : '')));
  else f.resolve(new Response(body, { status, headers: parseHeaders(headers), url }));
};

class XMLHttpRequest extends EventTarget {
  constructor() {
    super();
    this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.responseURL = '';
    this.responseType = ''; this.timeout = 0; this.withCredentials = false;
    this.upload = new EventTarget();
    this._h = new Headers();
  }
  open(method, url) { this._m = method; this._u = String(url); this.readyState = 1; this._abort = false; this._rs(); }
  setRequestHeader(k, v) { this._h.append(k, v); }
  getResponseHeader(k) { return this._rh ? this._rh.get(k) : null; }
  getAllResponseHeaders() { let s = ''; if (this._rh) this._rh.forEach((v, k) => { s += k + ': ' + v + '\r\n'; }); return s; }
  overrideMimeType() {}
  abort() { this._abort = true; this.readyState = 0; this.dispatchEvent(new ProgressEvent('abort')); }
  _rs() { this.dispatchEvent(new Event('readystatechange')); }
  get response() {
    if (this.responseType === 'json') { try { return JSON.parse(this.responseText); } catch (e) { return null; } }
    if (this.responseType === 'document') return new DOMParser().parseFromString(this.responseText, 'text/html');
    return this.responseText;
  }
  get responseXML() { return null; }
  send(body) {
    this.dispatchEvent(new ProgressEvent('loadstart'));
    fetch(this._u, { method: this._m, headers: this._h, body }).then(r => {
      if (this._abort) return;
      this.status = r.status; this.statusText = r.statusText; this._rh = r.headers; this.responseURL = r.url;
      this.readyState = 2; this._rs();
      return r.text().then(t => {
        this.responseText = t;
        this.readyState = 3; this._rs();
        this.readyState = 4; this._rs();
        this.dispatchEvent(new ProgressEvent('load', { loaded: t.length, total: t.length }));
        this.dispatchEvent(new ProgressEvent('loadend'));
      });
    }, () => {
      if (this._abort) return;
      this.readyState = 4; this._rs();
      this.dispatchEvent(new ProgressEvent('error'));
      this.dispatchEvent(new ProgressEvent('loadend'));
    });
  }
}
XMLHttpRequest.UNSENT = 0; XMLHttpRequest.OPENED = 1; XMLHttpRequest.HEADERS_RECEIVED = 2; XMLHttpRequest.LOADING = 3; XMLHttpRequest.DONE = 4;

// ---------------------------------------------------------------------------
// small web APIs
// ---------------------------------------------------------------------------
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
G.btoa = s => {
  s = String(s);
  let out = '';
  for (let i = 0; i < s.length; i += 3) {
    const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2);
    if (a > 255 || b > 255 || c > 255) throw new DOMException('Invalid character', 'InvalidCharacterError');
    const n = (a << 16) | ((b || 0) << 8) | (c || 0);
    out += B64[n >> 18] + B64[(n >> 12) & 63] + (i + 1 < s.length ? B64[(n >> 6) & 63] : '=') + (i + 2 < s.length ? B64[n & 63] : '=');
  }
  return out;
};
G.atob = s => {
  s = String(s).replace(/[\s=]/g, '').replace(/-/g, '+').replace(/_/g, '/');
  let out = '', bits = 0, acc = 0;
  for (const ch of s) {
    const v = B64.indexOf(ch);
    if (v < 0) throw new DOMException('Invalid character', 'InvalidCharacterError');
    acc = (acc << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; out += String.fromCharCode((acc >> bits) & 255); }
  }
  return out;
};
class TextEncoder {
  get encoding() { return 'utf-8'; }
  encode(s = '') {
    const bin = unescape(encodeURIComponent(String(s)));
    const u = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
    return u;
  }
}
class TextDecoder {
  constructor(label = 'utf-8') { this.encoding = label; }
  decode(buf) {
    if (!buf) return '';
    const u = buf instanceof ArrayBuffer ? new Uint8Array(buf) : new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);
    let bin = '';
    for (let i = 0; i < u.length; i++) bin += String.fromCharCode(u[i]);
    try { return decodeURIComponent(escape(bin)); } catch (e) { return bin; }
  }
}
class DOMException extends Error {
  constructor(message = '', name = 'Error') { super(message); this.name = name; }
}
class AbortSignal extends EventTarget {
  constructor() { super(); this.aborted = false; this.reason = undefined; }
  throwIfAborted() { if (this.aborted) throw this.reason; }
  static timeout(ms) { const c = new AbortController(); setTimeout(() => c.abort(new DOMException('timeout', 'TimeoutError')), ms); return c.signal; }
  static abort(r) { const c = new AbortController(); c.abort(r); return c.signal; }
}
class AbortController {
  constructor() { this.signal = new AbortSignal(); }
  abort(reason) {
    if (this.signal.aborted) return;
    this.signal.aborted = true;
    this.signal.reason = reason || new DOMException('The operation was aborted.', 'AbortError');
    this.signal.dispatchEvent(new Event('abort'));
  }
}
class MutationObserver {   // ponytail: never reports; frameworks fall back to promises
  constructor(cb) { this._cb = cb; }
  observe() {}
  disconnect() {}
  takeRecords() { return []; }
}
const intersectionObservers = new Set();
class IntersectionObserver {
  constructor(cb, opts = {}) { this._cb = cb; this._t = new Map(); this.root = null; this.rootMargin = opts.rootMargin || '0px'; this.thresholds = [0]; }
  observe(el) { this._t.set(el, undefined); intersectionObservers.add(this); }
  unobserve(el) { this._t.delete(el); }
  disconnect() { this._t.clear(); intersectionObservers.delete(this); }
  takeRecords() { return []; }
  _check() {
    const v = nx.viewport(), entries = [];
    for (const [el, was] of this._t) {
      const r = rectOf(el);
      const visible = r.bottom >= -200 && r.top <= v[1] + 200 && (r.width > 0 || r.height > 0);
      if (visible !== was) {
        this._t.set(el, visible);
        entries.push({ target: el, isIntersecting: visible, intersectionRatio: visible ? 1 : 0, boundingClientRect: r,
          intersectionRect: visible ? r : rectOf(document.body), rootBounds: { top: 0, left: 0, width: v[0], height: v[1] }, time: now });
      }
    }
    if (entries.length) { try { this._cb(entries, this); } catch (e) { report(e, 'IntersectionObserver'); } }
  }
}
class ResizeObserver {
  constructor(cb) { this._cb = cb; }
  observe(el) {
    setTimeout(() => {
      const r = rectOf(el);
      try { this._cb([{ target: el, contentRect: r, borderBoxSize: [{ inlineSize: r.width, blockSize: r.height }], contentBoxSize: [{ inlineSize: r.width, blockSize: r.height }] }], this); }
      catch (e) { report(e, 'ResizeObserver'); }
    }, 0);
  }
  unobserve() {}
  disconnect() {}
}
class DOMParser {
  parseFromString(html) {
    const holder = document.createElement('div');
    holder.innerHTML = String(html);
    const q = s => holder.querySelector(s);
    return { body: q('body') || holder, head: q('head'), documentElement: holder, querySelector: q,
      querySelectorAll: s => holder.querySelectorAll(s), getElementById: id => holder.querySelector('[id="' + id + '"]'),
      title: (q('title') || { textContent: '' }).textContent };
  }
}
class CustomElementRegistry {
  define(name, cls) {
    name = String(name).toLowerCase();
    customTags.set(name, cls);
    customNames.set(cls, name);
    for (const id of nx.select(nx.root(), name)) {   // upgrade elements already in the page
      nodes.delete(id);
      connected(wrap(id));
    }
    const w = this._w && this._w[name];
    if (w) w.forEach(r => r(cls));
  }
  get(name) { return customTags.get(String(name).toLowerCase()); }
  whenDefined(name) {
    name = String(name).toLowerCase();
    if (customTags.has(name)) return Promise.resolve(customTags.get(name));
    return new Promise(r => { (this._w || (this._w = {}))[name] = ((this._w || {})[name] || []).concat(r); });
  }
  upgrade() {}
}
function mediaMatches(q) {
  q = String(q).toLowerCase();
  const w = nx.viewport()[0];
  return q.split(',').some(part => part.split(/\band\b/).every(c => {
    c = c.trim().replace(/^only\s+/, '');
    let m;
    if (c === 'screen' || c === 'all' || c === '') return true;
    if (c === 'print') return false;
    if ((m = /\(min-width:\s*([\d.]+)(px|em|rem)?\)/.exec(c))) return w >= parseFloat(m[1]) * (m[2] && m[2] !== 'px' ? 16 : 1);
    if ((m = /\(max-width:\s*([\d.]+)(px|em|rem)?\)/.exec(c))) return w <= parseFloat(m[1]) * (m[2] && m[2] !== 'px' ? 16 : 1);
    if (/prefers-reduced-motion:\s*reduce/.test(c)) return true;     // fewer animations to run
    if (/orientation:\s*landscape/.test(c)) return true;
    if (/hover:\s*hover|pointer:\s*fine/.test(c)) return false;
    return false;
  }));
}

// ---------------------------------------------------------------------------
// window
// ---------------------------------------------------------------------------
const location = new Location();
const historyStack = { state: null };
const winTarget = new EventTarget();
Object.assign(G, {
  window: G, self: G, top: G, parent: G, frames: G, globalThis: G, opener: null, frameElement: null, name: '',
  closed: false, length: 0, isSecureContext: true, crossOriginIsolated: false, devicePixelRatio: 1,
  Node, Element, HTMLElement, Text, Comment, CharacterData, Document, DocumentFragment, EventTarget, Event, CustomEvent,
  UIEvent, MouseEvent, PointerEvent, KeyboardEvent, FocusEvent, InputEvent, SubmitEvent, MessageEvent, ProgressEvent,
  HTMLInputElement, HTMLTextAreaElement, HTMLSelectElement, HTMLOptionElement, HTMLButtonElement, HTMLFormElement,
  HTMLAnchorElement, HTMLImageElement, HTMLScriptElement, HTMLLinkElement, HTMLIFrameElement, HTMLCanvasElement,
  HTMLMediaElement, HTMLVideoElement: HTMLMediaElement, HTMLAudioElement: HTMLMediaElement, HTMLTemplateElement,
  HTMLLabelElement, HTMLDialogElement, HTMLDetailsElement, HTMLDivElement: HTMLElement, HTMLSpanElement: HTMLElement,
  HTMLBodyElement: HTMLElement, HTMLHeadElement: HTMLElement, HTMLHtmlElement: HTMLElement, HTMLStyleElement: HTMLElement,
  HTMLParagraphElement: HTMLElement, HTMLUListElement: HTMLElement, HTMLLIElement: HTMLElement, HTMLTableElement: HTMLElement,
  HTMLUnknownElement: HTMLElement, SVGElement, NodeList, HTMLCollection: NodeList, DOMTokenList,
  URL, URLSearchParams, Location, Headers, Request, Response, Blob, File, FormData, XMLHttpRequest, TextEncoder,
  TextDecoder, DOMException, AbortController, AbortSignal, MutationObserver, IntersectionObserver, ResizeObserver,
  DOMParser, Storage,
  customElements: new CustomElementRegistry(),
  localStorage: storageProxy(new Storage(true)),
  sessionStorage: storageProxy(new Storage(false)),
  navigator: {
    userAgent: 'Mozilla/5.0 (X11; Linux aarch64; rv:128.0) Gecko/20100101 Firefox/128.0 BrowseNX/1.0',
    appName: 'Netscape', appVersion: '5.0', product: 'Gecko', vendor: '', platform: 'Linux aarch64',
    language: 'en-US', languages: ['en-US', 'en'], onLine: true, cookieEnabled: true, doNotTrack: '1',
    hardwareConcurrency: 4, maxTouchPoints: 10, webdriver: false, pdfViewerEnabled: false,
    clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve('') },
    sendBeacon: () => true, vibrate: () => false, javaEnabled: () => false,
    permissions: { query: () => Promise.resolve({ state: 'denied', addEventListener() {} }) },
    userAgentData: { brands: [], mobile: false, platform: 'Linux', getHighEntropyValues: () => Promise.resolve({}) },
    storage: { estimate: () => Promise.resolve({ quota: 0, usage: 0 }), persist: () => Promise.resolve(false) },
  },
  screen: { width: 1280, height: 720, availWidth: 1280, availHeight: 720, colorDepth: 24, pixelDepth: 24,
    orientation: { type: 'landscape-primary', angle: 0, addEventListener() {} } },
  history: {
    get length() { return 1; },
    get state() { return historyStack.state; },
    scrollRestoration: 'auto',
    pushState(s, t, u) { historyStack.state = s; if (u != null) nx.setUrl(String(u)); },
    replaceState(s, t, u) { historyStack.state = s; if (u != null) nx.setUrl(String(u)); },
    back() { nx.navigate('nx:back'); },
    forward() { nx.navigate('nx:forward'); },
    go(n) { if (n < 0) this.back(); else if (n > 0) this.forward(); else location.reload(); },
  },
  performance: {
    now: () => nx.now(), timeOrigin: Date.now(), timing: { navigationStart: Date.now() },
    mark() {}, measure() {}, clearMarks() {}, clearMeasures() {}, getEntries: () => [], getEntriesByName: () => [], getEntriesByType: () => [],
  },
  crypto: {
    getRandomValues(a) { for (let i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 4294967296); return a; },
    randomUUID() { return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => { const r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 3 | 8)).toString(16); }); },
    subtle: {},
  },
  CSS: { supports: () => false, escape: s => String(s).replace(/([^a-zA-Z0-9_-])/g, '\\$1') },
  Image: function (w, h) { const i = document.createElement('img'); if (w) i.width = w; if (h) i.height = h; return i; },
  Option: function (text, value) { const o = document.createElement('option'); if (text) o.textContent = text; if (value !== undefined) o.value = value; return o; },
  scrollTo(x, y) { const top = typeof x === 'object' && x ? x.top : y; if (typeof top === 'number') nx.scrollTo(top); },
  scrollBy(x, y) { const dy = typeof x === 'object' && x ? x.top : y; if (typeof dy === 'number') nx.scrollTo(G.scrollY + dy); },
  alert(m) { nx.alert(m === undefined ? '' : String(m)); },
  confirm() { return true; },
  prompt(m, d) { return nx.prompt(m === undefined ? '' : String(m), d === undefined ? '' : String(d)); },
  print() {},
  stop() {},
  focus() {},
  blur() {},
  close() {},
  open(u) { if (u) nx.navigate(String(u)); return null; },
  postMessage(data) { setTimeout(() => winTarget.dispatchEvent.call(G, new MessageEvent('message', { data, origin: location.origin, source: G })), 0); },
  getComputedStyle(el) {
    const inline = el && el.style;
    return new Proxy({}, {
      get(t, k) {
        if (k === 'getPropertyValue') return p => p === 'display' ? nx.display(el._id) : inline ? inline.getPropertyValue(p) : '';
        if (k === 'display') return el ? nx.display(el._id) : 'block';
        if (k === 'visibility') return 'visible';
        if (k === 'opacity') return '1';
        if (typeof k !== 'string' || !inline) return '';
        return inline[k] || '';
      },
    });
  },
  matchMedia(q) {
    return { matches: mediaMatches(q), media: String(q), onchange: null, addListener() {}, removeListener() {},
      addEventListener() {}, removeEventListener() {}, dispatchEvent() { return true; } };
  },
  getSelection() { return { rangeCount: 0, isCollapsed: true, toString: () => '', removeAllRanges() {}, addRange() {}, getRangeAt() { return document.createRange(); } }; },
  structuredClone(v) { return v === undefined ? v : JSON.parse(JSON.stringify(v)); },
  reportError(e) { report(e); },
});
// Live values: Object.assign above would have copied them once.
for (const [k, get] of Object.entries({
  innerWidth: () => nx.viewport()[0], innerHeight: () => nx.viewport()[1], outerWidth: () => 1280, outerHeight: () => 720,
  scrollX: () => 0, scrollY: () => nx.viewport()[2], pageXOffset: () => 0, pageYOffset: () => nx.viewport()[2],
  screenX: () => 0, screenY: () => 0, screenLeft: () => 0, screenTop: () => 0, origin: () => location.origin,
})) Object.defineProperty(G, k, { get, configurable: true });
Object.defineProperty(G, 'location', { get: () => location, set: v => { location.href = v; }, configurable: true });
// window's own event listeners (load, scroll, resize, message, popstate ...)
for (const k of ['addEventListener', 'removeEventListener', '_fire', '_handler'])
  G[k] = EventTarget.prototype[k];
G.dispatchEvent = function (ev) { ev.target = G; ev._path = [G]; G._fire(ev, 2); return !ev.defaultPrevented; };
for (const t of handlerProps) {
  Object.defineProperty(G, 'on' + t, {
    get() { return (G._on && G._on[t]) || null; },
    set(v) { (G._on || (G._on = {}))[t] = typeof v === 'function' ? v : null; },
    configurable: true,
  });
}
delete G.__nx;

// ---------------------------------------------------------------------------
// entry points called by engine.cpp
// ---------------------------------------------------------------------------
let lastObserverCheck = 0;
G.__nxInit = () => {};
G.__nxSetCurrentScript = id => { currentScript = id; };
G.__nxLoaded = () => {
  readyState = 'interactive';
  document.dispatchEvent(new Event('readystatechange'));
  document.dispatchEvent(new Event('DOMContentLoaded', { bubbles: true }));
  readyState = 'complete';
  document.dispatchEvent(new Event('readystatechange'));
  G.dispatchEvent(new Event('load'));
  G.dispatchEvent(new Event('pageshow'));
};
G.__nxDispatch = (id, type) => {
  const el = wrap(id);
  if (!el) return false;
  let ev;
  if (type === 'click') {
    const r = nx.rect(id), v = nx.viewport();
    ev = new MouseEvent('click', { bubbles: true, cancelable: true, clientX: r[0] + 1, clientY: r[1] - v[2] + 1, pageX: r[0] + 1, pageY: r[1] + 1 });
    ev.isTrusted = true;
    for (const t of ['pointerdown', 'mousedown', 'pointerup', 'mouseup'])
      el.dispatchEvent(new MouseEvent(t, { bubbles: true, cancelable: true }));
  } else if (type === 'submit') ev = new SubmitEvent('submit', { bubbles: true, cancelable: true });
  else if (type === 'input') ev = new InputEvent('input', { bubbles: true });
  else ev = new Event(type, { bubbles: type === 'change' || type === 'focus', cancelable: false });
  return !el.dispatchEvent(ev);
};
G.__nxTick = (t, scrolled) => {
  now = nx.now();
  for (const [id, tm] of Array.from(timerList)) {
    if (tm.at > now || !timerList.has(id)) continue;
    if (tm.every) tm.at = now + tm.every; else timerList.delete(id);
    try { tm.fn.apply(G, tm.args); } catch (e) { report(e, 'timer'); }
  }
  if (rafList.length) {
    const list = rafList;
    rafList = [];
    for (const r of list) { try { r.fn(now); } catch (e) { report(e, 'requestAnimationFrame'); } }
  }
  if (scrolled) { G.dispatchEvent(new Event('scroll')); document.dispatchEvent(new Event('scroll', { bubbles: false })); }
  if (intersectionObservers.size && (scrolled || now - lastObserverCheck > 250)) {
    lastObserverCheck = now;
    for (const o of intersectionObservers) o._check();
  }
};
})();
)JS"
