/* ClaudePad web UI - vanilla ES2020, no build step.
 * Sections:
 *  1. Constants        2. Pure helpers (RGB565, validation)   3. API + mock device
 *  4. Built-in icons   5. State + DOM helpers                 6. Keys view + editor sheet
 *  7. Icons view       8. Settings / LEDs / Backup views      9. Save logic + boot
 */
'use strict';

/* ======================================================================
 * 1. Constants
 * ==================================================================== */
const ICON_SIZE = 128;
const ICON_BYTES = ICON_SIZE * ICON_SIZE * 2;
const ID_RE = /^[a-z0-9_-]{1,24}$/;
const COLOR_RE = /^#[0-9a-fA-F]{6}$/;
const MODS = ['ctrl', 'shift', 'alt', 'gui'];
const MEDIA_KEYS = ['volup', 'voldown', 'mute', 'playpause', 'next', 'prev', 'stop'];
const EFFECTS = ['off', 'solid', 'breathe', 'rainbow', 'chase', 'sparkle'];
const KEY_GROUPS = (() => {
  const letters = 'abcdefghijklmnopqrstuvwxyz'.split('');
  const digits = '0123456789'.split('');
  const fkeys = Array.from({ length: 24 }, (_, i) => 'f' + (i + 1));
  const named = ('enter esc tab space backspace delete insert home end pageup pagedown up down left right ' +
    'printscreen scrolllock pause capslock menu').split(' ');
  const punct = ['-', '=', '[', ']', '\\', ';', "'", ',', '.', '/', '`'];
  return [['Letters', letters], ['Digits', digits], ['Function', fkeys], ['Named', named], ['Punctuation', punct]];
})();
const KEY_SET = new Set(KEY_GROUPS.flatMap(g => g[1]));
const MAX_STEPS = 32;
const MAX_TEXT = 256;
const MS_FOREVER = 65535;
const HUE_NOTICE = "This pad's LED controller ignores hue: colors show as white or pink; brightness works. Pick bright/saturated = pink, pale = white, dark = dim.";
const DEFAULT_ZONES = [3, 24, 192, 1536];

/* ======================================================================
 * 2. Pure helpers
 * ==================================================================== */

/** RGBA bytes (canvas ImageData.data) -> RGB565 big-endian bytes (high byte first). */
function rgbaToRgb565BE(rgba) {
  const n = rgba.length / 4;
  const out = new Uint8Array(n * 2);
  for (let i = 0; i < n; i++) {
    const r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
    const v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    out[i * 2] = v >> 8;
    out[i * 2 + 1] = v & 0xFF;
  }
  return out;
}

/** RGB565 big-endian bytes -> RGBA bytes. */
function rgb565BEToRgba(bytes) {
  const n = bytes.length >> 1;
  const out = new Uint8ClampedArray(n * 4);
  for (let i = 0; i < n; i++) {
    const v = (bytes[i * 2] << 8) | bytes[i * 2 + 1];
    const r5 = v >> 11, g6 = (v >> 5) & 63, b5 = v & 31;
    out[i * 4] = (r5 << 3) | (r5 >> 2);
    out[i * 4 + 1] = (g6 << 2) | (g6 >> 4);
    out[i * 4 + 2] = (b5 << 3) | (b5 >> 2);
    out[i * 4 + 3] = 255;
  }
  return out;
}

function sanitizeId(s) {
  return String(s || '').toLowerCase().replace(/\.[a-z0-9]+$/, '').replace(/[^a-z0-9_-]+/g, '-')
    .replace(/^-+|-+$/g, '').slice(0, 24);
}

function escapeText(s) { return String(s).replace(/\n/g, '\\n').replace(/\t/g, '\\t'); }
function unescapeText(s) { return String(s).replace(/\\n/g, '\n').replace(/\\t/g, '\t'); }

function bytesToBase64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}
function base64ToBytes(b64) {
  const s = atob(b64);
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}

function validateSteps(steps, where, err) {
  if (!Array.isArray(steps)) { err(`${where}: steps must be a list`); return; }
  if (steps.length > MAX_STEPS) err(`${where}: at most ${MAX_STEPS} steps`);
  steps.forEach((s, i) => {
    const w = `${where} step ${i + 1}`;
    if (!s || typeof s !== 'object') return err(`${w}: invalid`);
    if (s.t === 'key') {
      if (!['tap', 'down', 'up'].includes(s.mode)) err(`${w}: bad mode`);
      if (!Array.isArray(s.mods) || s.mods.some(m => !MODS.includes(m))) err(`${w}: bad modifiers`);
      if (s.key !== '' && !KEY_SET.has(s.key)) err(`${w}: unknown key "${s.key}"`);
      if (s.key === '' && (!s.mods || s.mods.length === 0)) err(`${w}: needs a key or a modifier`);
    } else if (s.t === 'text') {
      if (typeof s.text !== 'string' || s.text.length === 0) err(`${w}: text is empty`);
      else if (s.text.length > MAX_TEXT) err(`${w}: text max ${MAX_TEXT} chars`);
      else if (/[^\x00-\x7F]/.test(s.text)) err(`${w}: text must be ASCII`);
    } else if (s.t === 'media') {
      if (!MEDIA_KEYS.includes(s.key)) err(`${w}: pick a media key`);
    } else if (s.t === 'delay') {
      if (!Number.isInteger(s.ms) || s.ms < 0 || s.ms > 5000) err(`${w}: delay must be 0-5000 ms`);
    } else err(`${w}: unknown type`);
  });
}

/** Returns an array of human-readable problems (empty = OK). */
function validateConfig(c) {
  const errs = [];
  const err = m => errs.push(m);
  const int = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
  if (!c || typeof c !== 'object') return ['Config is missing'];
  const s = c.settings || {};
  if (typeof s.apSsid !== 'string' || s.apSsid.length < 1 || s.apSsid.length > 31) err('Wi-Fi name must be 1-31 characters');
  if (typeof s.apPass !== 'string' || s.apPass.length < 8 || s.apPass.length > 63) err('Wi-Fi password must be 8-63 characters');
  if (!int(s.brightness, 5, 100)) err('Screen brightness must be 5-100');
  if (!int(s.ledBrightness, 0, 100)) err('LED brightness must be 0-100');
  if (!int(s.dimMinutes, 0, 600)) err('Dim minutes must be 0-600');
  if (!int(s.doubleMs, 100, 1000)) err('Double-press window must be 100-1000 ms');
  if (!int(s.holdMs, 150, 3000)) err('Hold threshold must be 150-3000 ms');
  if (!int(s.revertMs, 100, 30000)) err('Revert time must be 100-30000 ms');
  if (s.ledHue !== undefined && typeof s.ledHue !== 'boolean') err('ledHue must be true or false');
  if (s.wifiEnabled !== undefined && typeof s.wifiEnabled !== 'boolean') err('wifiEnabled must be true or false');
  if (!Array.isArray(s.ledZones) || s.ledZones.length !== 4 || s.ledZones.some(m => !int(m, 0, 16383))) err('LED zones must be 4 masks (0-16383)');
  if (!Array.isArray(c.pages) || c.pages.length < 1 || c.pages.length > 8) { err('Need 1-8 pages'); return errs; }
  if (!int(s.activePage, 0, Math.min(7, c.pages.length - 1))) err('Active page out of range');
  const iconIds = new Set((c.icons || []).map(i => i.id));
  const checkIcon = (id, w) => { if (id && !iconIds.has(id)) err(`${w}: icon "${id}" does not exist`); };
  const checkColor = (col, w) => { if (col && !COLOR_RE.test(col)) err(`${w}: bad color`); };
  const checkMs = (ms, w) => { if (ms !== undefined && !int(ms, 0, 65535)) err(`${w}: duration must be 0-65535 ms`); };
  c.pages.forEach((p, pi) => {
    const pw = `Page ${pi + 1}`;
    if (typeof p.name !== 'string' || p.name.length < 1 || p.name.length > 15) err(`${pw}: name must be 1-15 characters`);
    const led = p.led || {};
    if (!EFFECTS.includes(led.effect)) err(`${pw}: bad LED effect`);
    if (!COLOR_RE.test(led.color || '')) err(`${pw}: bad LED color`);
    if (!int(led.speed, 1, 100)) err(`${pw}: LED speed must be 1-100`);
    if (!Array.isArray(p.keys) || p.keys.length !== 4) { err(`${pw}: needs exactly 4 keys`); return; }
    const all = p.keys.map((k, i) => [k, `${pw} key ${i + 1}`]).concat([[p.encoder, `${pw} encoder`]]);
    all.forEach(([k, w]) => {
      if (!k || typeof k !== 'object') return err(`${w}: missing`);
      if (k.idle) { checkIcon(k.idle.icon, w + ' idle'); checkColor(k.idle.color, w + ' idle'); }
      for (const g of ['single', 'double']) {
        if (!k[g]) continue;
        validateSteps(k[g].steps, `${w} ${g}`, err);
        checkIcon(k[g].icon, `${w} ${g}`); checkColor(k[g].color, `${w} ${g}`); checkMs(k[g].ms, `${w} ${g}`);
      }
      if (k.hold) {
        const h = k.hold;
        validateSteps(h.start || [], `${w} hold start`, err);
        validateSteps(h.end || [], `${w} hold end`, err);
        checkIcon(h.icon, `${w} hold`); checkColor(h.color, `${w} hold`);
        if (h.after) { checkIcon(h.after.icon, `${w} after`); checkColor(h.after.color, `${w} after`); checkMs(h.after.ms, `${w} after`); }
      }
    });
  });
  (c.icons || []).forEach(i => { if (!ID_RE.test(i.id)) err(`Icon id "${i.id}" is invalid`); });
  return errs;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { rgbaToRgb565BE, rgb565BEToRgba, validateConfig, sanitizeId, escapeText, unescapeText, DEFAULT_ZONES };
}

/* Everything below needs a browser. */
if (typeof document !== 'undefined') {

/* ======================================================================
 * 3. API layer + in-memory mock device
 * ==================================================================== */
async function api(method, path, body) {
  const opt = { method, headers: {} };
  if (body !== undefined) { opt.headers['Content-Type'] = 'application/json'; opt.body = JSON.stringify(body); }
  return parseResponse(await fetch(path, opt));
}
async function apiUpload(path, bytes, filename) {
  const fd = new FormData();
  fd.append('file', new Blob([bytes], { type: 'application/octet-stream' }), filename);
  return parseResponse(await fetch(path, { method: 'POST', body: fd }));
}
async function parseResponse(r) {
  let j = null;
  try { j = await r.json(); } catch (e) { /* not JSON */ }
  if (!r.ok || (j && j.ok === false)) throw new Error((j && j.error) || ('HTTP ' + r.status));
  return j;
}

const sleep = ms => new Promise(r => setTimeout(r, ms));

function installMock() {
  state.mock = true;
  const db = { config: mockSeedConfig(), icons: new Map(), activePage: 0, t0: Date.now() };
  for (const b of BUILTIN_ICONS) db.icons.set(b.id, renderBuiltinBytes(b));
  const json = (o, status = 200) => new Response(JSON.stringify(o), { status, headers: { 'Content-Type': 'application/json' } });

  window.fetch = async (input, opt = {}) => {
    const path = String(input).replace(/^[a-z]+:\/\/[^/]+/i, '').split('?')[0];
    const method = (opt.method || 'GET').toUpperCase();
    await sleep(30);
    const body = typeof opt.body === 'string' ? JSON.parse(opt.body) : null;
    let m;
    if (path === '/api/status') {
      return json({ fw: 'mock-1.0', activePage: db.activePage, pageCount: db.config.pages.length, heapFree: 180000,
        fsTotal: 1441792, fsUsed: 32768 * db.icons.size, uptimeMs: Date.now() - db.t0, ledCount: 14 });
    }
    if (path === '/api/config' && method === 'GET') return json(db.config);
    if (path === '/api/config' && method === 'PUT') {
      const errs = validateConfig(body);
      if (errs.length) return json({ ok: false, error: errs[0] }, 400);
      db.config = body; return json({ ok: true });
    }
    if (path === '/api/page' && method === 'POST') {
      if (!Number.isInteger(body.index) || body.index < 0 || body.index >= db.config.pages.length) return json({ ok: false, error: 'bad index' }, 400);
      db.activePage = body.index; return json({ ok: true });
    }
    if (path === '/api/test' && method === 'POST') { console.log('[mock] test steps', body.steps); return json({ ok: true }); }
    if (path === '/api/leds/probe' && method === 'POST') { console.log('[mock] probe mask', body.mask); return json({ ok: true }); }
    if ((m = path.match(/^\/api\/icons\/([^/]+)$/))) {
      const id = decodeURIComponent(m[1]);
      if (method === 'GET') return db.icons.has(id) ? new Response(db.icons.get(id)) : json({ ok: false, error: 'not found' }, 404);
      if (method === 'POST') {
        if (!ID_RE.test(id)) return json({ ok: false, error: 'bad id' }, 400);
        const file = opt.body && opt.body.get && opt.body.get('file');
        if (!file) return json({ ok: false, error: 'no file field' }, 400);
        const bytes = new Uint8Array(await file.arrayBuffer());
        if (bytes.length !== ICON_BYTES) return json({ ok: false, error: 'icon must be ' + ICON_BYTES + ' bytes' }, 400);
        db.icons.set(id, bytes); return json({ ok: true });
      }
      if (method === 'DELETE') { db.icons.delete(id); return json({ ok: true }); }
    }
    if (path === '/api/factory-reset' && method === 'POST') {
      db.config = mockSeedConfig(); db.icons = new Map();
      for (const b of BUILTIN_ICONS) db.icons.set(b.id, renderBuiltinBytes(b));
      return json({ ok: true });
    }
    return json({ ok: false, error: 'not found' }, 404);
  };
}

function mockSeedConfig() {
  const blank = () => ({ idle: { icon: '', color: '' } });
  const key = (tap, mods) => ({ t: 'key', mode: 'tap', mods: mods || [], key: tap });
  return {
    version: 1,
    settings: { apSsid: 'ClaudePad', apPass: 'claudepad1', brightness: 80, ledBrightness: 40, dimMinutes: 10,
      doubleMs: 250, holdMs: 400, revertMs: 1500, activePage: 0, ledHue: true, wifiEnabled: true, ledZones: DEFAULT_ZONES.slice() },
    icons: BUILTIN_ICONS.map(b => ({ id: b.id, name: b.name })),
    pages: [
      {
        name: 'Claude Code',
        led: { effect: 'breathe', color: '#d97757', speed: 50 },
        keys: [
          { idle: { icon: 'mic-red', color: '#ff3030' },
            hold: { start: [{ t: 'key', mode: 'down', mods: ['alt'], key: 'k' }],
              end: [{ t: 'key', mode: 'up', mods: ['alt'], key: 'k' }],
              icon: 'mic-green', color: '#22c55e', after: { icon: 'mic-red', color: '#ff3030', ms: 0 } } },
          { idle: { icon: 'clear', color: '#f59e0b' }, single: { steps: [{ t: 'text', text: '/clear\n' }] } },
          { idle: { icon: 'esc', color: '#94a3b8' }, single: { steps: [key('esc')] } },
          { idle: { icon: 'enter', color: '#38bdf8' }, single: { steps: [key('enter')] } },
        ],
        encoder: { idle: { icon: '', color: '' }, single: { steps: [{ t: 'media', key: 'mute' }] } },
      },
      {
        name: 'Windows',
        led: { effect: 'solid', color: '#2563eb', speed: 50 },
        keys: [
          { idle: { icon: 'win', color: '#38bdf8' }, single: { steps: [key('d', ['gui'])] } },
          blank(), blank(), blank(),
        ],
        encoder: blank(),
      },
    ],
  };
}

/* ======================================================================
 * 4. Built-in icons (drawn on a 128x128 canvas)
 * ==================================================================== */
function rr(c, x, y, w, h, r) {
  c.beginPath(); c.moveTo(x + r, y); c.arcTo(x + w, y, x + w, y + h, r); c.arcTo(x + w, y + h, x, y + h, r);
  c.arcTo(x, y + h, x, y, r); c.arcTo(x, y, x + w, y, r); c.closePath();
}
function bg(c, col) { c.fillStyle = col; c.fillRect(0, 0, ICON_SIZE, ICON_SIZE); }
function starPath(c, cx, cy, r1, r2, n) {
  c.beginPath();
  for (let i = 0; i < n * 2; i++) {
    const r = i % 2 ? r2 : r1, a = Math.PI * i / n - Math.PI / 2;
    c[i ? 'lineTo' : 'moveTo'](cx + r * Math.cos(a), cy + r * Math.sin(a));
  }
  c.closePath();
}
function bigText(c, text, size, col) {
  c.font = `bold ${size}px sans-serif`; c.textAlign = 'center'; c.textBaseline = 'middle'; c.fillStyle = col;
  c.fillText(text, 64, 66);
}
function drawMic(c, col) {
  bg(c, '#0f172a'); c.fillStyle = col; rr(c, 48, 14, 32, 58, 16); c.fill();
  c.strokeStyle = col; c.lineWidth = 7; c.lineCap = 'round';
  c.beginPath(); c.arc(64, 56, 28, 0, Math.PI); c.stroke();
  c.beginPath(); c.moveTo(64, 84); c.lineTo(64, 108); c.moveTo(46, 108); c.lineTo(82, 108); c.stroke();
}
const BUILTIN_ICONS = [
  { id: 'mic-green', name: 'Mic (green)', draw: c => drawMic(c, '#22c55e') },
  { id: 'mic-red', name: 'Mic (red)', draw: c => drawMic(c, '#ef4444') },
  { id: 'check', name: 'Check', draw: c => {
    bg(c, '#06280f'); c.strokeStyle = '#4ade80'; c.lineWidth = 16; c.lineCap = 'round'; c.lineJoin = 'round';
    c.beginPath(); c.moveTo(30, 66); c.lineTo(54, 92); c.lineTo(98, 38); c.stroke(); } },
  { id: 'stop', name: 'Stop', draw: c => { bg(c, '#2a0a0a'); c.fillStyle = '#ef4444'; rr(c, 34, 34, 60, 60, 8); c.fill(); } },
  { id: 'clear', name: 'Clear', draw: c => {
    bg(c, '#2b1b05'); c.strokeStyle = '#f59e0b'; c.lineWidth = 16; c.lineCap = 'round';
    c.beginPath(); c.moveTo(34, 34); c.lineTo(94, 94); c.moveTo(94, 34); c.lineTo(34, 94); c.stroke(); } },
  { id: 'enter', name: 'Enter', draw: c => {
    bg(c, '#0a2238'); c.strokeStyle = '#38bdf8'; c.fillStyle = '#38bdf8'; c.lineWidth = 12; c.lineCap = 'round'; c.lineJoin = 'round';
    c.beginPath(); c.moveTo(100, 30); c.lineTo(100, 78); c.lineTo(42, 78); c.stroke();
    c.beginPath(); c.moveTo(24, 78); c.lineTo(50, 54); c.lineTo(50, 102); c.closePath(); c.fill(); } },
  { id: 'esc', name: 'Escape', draw: c => {
    bg(c, '#1e2230'); c.strokeStyle = '#94a3b8'; c.lineWidth = 6; rr(c, 14, 34, 100, 60, 12); c.stroke(); bigText(c, 'ESC', 38, '#e2e8f0'); } },
  { id: 'play', name: 'Play', draw: c => {
    bg(c, '#06200f'); c.fillStyle = '#22c55e'; c.beginPath(); c.moveTo(42, 28); c.lineTo(100, 64); c.lineTo(42, 100); c.closePath(); c.fill(); } },
  { id: 'vol', name: 'Volume', draw: c => {
    bg(c, '#1c1030'); c.fillStyle = '#a78bfa'; c.strokeStyle = '#a78bfa'; c.lineWidth = 7; c.lineCap = 'round';
    c.beginPath(); c.moveTo(22, 52); c.lineTo(42, 52); c.lineTo(64, 32); c.lineTo(64, 96); c.lineTo(42, 76); c.lineTo(22, 76); c.closePath(); c.fill();
    c.beginPath(); c.arc(64, 64, 20, -0.7, 0.7); c.stroke(); c.beginPath(); c.arc(64, 64, 36, -0.7, 0.7); c.stroke(); } },
  { id: 'win', name: 'Windows key', draw: c => {
    bg(c, '#06193a'); c.fillStyle = '#38bdf8';
    c.fillRect(30, 30, 31, 31); c.fillRect(67, 30, 31, 31); c.fillRect(30, 67, 31, 31); c.fillRect(67, 67, 31, 31); } },
  { id: 'spark', name: 'Spark', draw: c => { bg(c, '#24120a'); c.fillStyle = '#d97757'; starPath(c, 64, 64, 54, 14, 6); c.fill(); } },
  { id: 'remote', name: 'Remote', draw: c => {
    bg(c, '#12142e'); c.strokeStyle = '#f472b6'; c.fillStyle = '#f472b6'; c.lineWidth = 7; c.lineJoin = 'round'; c.lineCap = 'round';
    rr(c, 18, 26, 92, 62, 8); c.stroke(); c.beginPath(); c.moveTo(64, 88); c.lineTo(64, 104); c.moveTo(44, 104); c.lineTo(84, 104); c.stroke();
    c.beginPath(); c.moveTo(48, 44); c.lineTo(48, 72); c.lineTo(56, 65); c.lineTo(63, 76); c.lineTo(68, 73); c.lineTo(62, 62); c.lineTo(72, 62); c.closePath(); c.fill(); } },
];

function newCanvas() {
  const cv = document.createElement('canvas');
  cv.width = cv.height = ICON_SIZE;
  return cv;
}
function canvasToBytes(cv) {
  const d = cv.getContext('2d', { willReadFrequently: true }).getImageData(0, 0, ICON_SIZE, ICON_SIZE);
  return rgbaToRgb565BE(d.data);
}
function renderBuiltinBytes(b) {
  const cv = newCanvas(); b.draw(cv.getContext('2d')); return canvasToBytes(cv);
}

/* ======================================================================
 * 5. State + DOM helpers
 * ==================================================================== */
const state = { view: 'keys', pageIdx: 0, devPage: 0, editing: null, status: null, mock: false, maker: null, ledSel: null };
let cfg = null;        // working copy
let savedStr = '';     // JSON of last saved/loaded config
const iconCache = new Map();   // id -> Promise<Uint8Array|null>

const $ = sel => document.querySelector(sel);
const PROP_ATTRS = new Set(['value', 'checked', 'disabled', 'selected', 'hidden']);
function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === null || v === undefined || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2).toLowerCase(), v);
    else if (PROP_ATTRS.has(k)) el[k] = v;
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat()) if (kid !== null && kid !== undefined && kid !== false) el.append(kid);
  return el;
}

let toastTimer = 0;
function toast(msg, kind) {
  const t = $('#toast');
  t.textContent = msg; t.className = 'toast show ' + (kind || '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = 'toast'; }, kind === 'err' ? 5000 : 2500);
}
async function guard(fn, okMsg) {
  try { const r = await fn(); if (okMsg) toast(okMsg, 'ok'); return r; }
  catch (e) { toast(e.message || String(e), 'err'); return undefined; }
}

const page = () => cfg.pages[state.pageIdx];
const isDirty = () => JSON.stringify(cfg) !== savedStr;
function markDirty() {
  const d = isDirty();
  $('#saveBar').hidden = !d;
}

function blankKey() { return { idle: { icon: '', color: '' } }; }
function blankPage(n) {
  return { name: ('Page ' + n).slice(0, 15), led: { effect: 'breathe', color: '#3355ff', speed: 50 },
    keys: [blankKey(), blankKey(), blankKey(), blankKey()], encoder: blankKey() };
}
function normalizeConfig(c) {
  c.icons = c.icons || [];
  c.settings = c.settings || {};
  if (typeof c.settings.ledHue !== 'boolean') c.settings.ledHue = true;
  if (!Array.isArray(c.settings.ledZones) || c.settings.ledZones.length !== 4) c.settings.ledZones = DEFAULT_ZONES.slice();
  for (const p of c.pages) {
    p.led = p.led || { effect: 'off', color: '#3355ff', speed: 50 };
    p.keys = p.keys || [];
    while (p.keys.length < 4) p.keys.push(blankKey());
    p.encoder = p.encoder || blankKey();
    [...p.keys, p.encoder].forEach(k => { k.idle = k.idle || { icon: '', color: '' }; });
  }
  return c;
}

/* ---- icon loading / drawing ---- */
function loadIconBytes(id) {
  if (!iconCache.has(id)) {
    iconCache.set(id, fetch('/api/icons/' + encodeURIComponent(id))
      .then(r => (r.ok ? r.arrayBuffer() : null))
      .then(b => (b && b.byteLength === ICON_BYTES ? new Uint8Array(b) : null))
      .catch(() => null));
  }
  return iconCache.get(id);
}
function iconCanvas(id) {
  const cv = newCanvas();
  const ctx = cv.getContext('2d');
  ctx.fillStyle = '#000'; ctx.fillRect(0, 0, ICON_SIZE, ICON_SIZE);
  if (id) {
    loadIconBytes(id).then(bytes => {
      if (bytes) ctx.putImageData(new ImageData(rgb565BEToRgba(bytes), ICON_SIZE, ICON_SIZE), 0, 0);
      else { ctx.fillStyle = '#777'; ctx.font = 'bold 64px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText('?', 64, 66); }
    });
  }
  return cv;
}

/* ---- reusable form widgets ---- */
function hueNotice() { return cfg.settings.ledHue ? null : h('div', { class: 'notice info small', role: 'note', text: HUE_NOTICE }); }
function field(label, control, hint) {
  return h('label', { class: 'field' }, h('span', { class: 'lbl', text: label }), control, hint ? h('div', { class: 'hint', text: hint }) : null);
}
function textField(label, value, set, opts = {}) {
  const inp = h('input', { type: opts.type || 'text', value, maxlength: opts.max, autocomplete: 'off',
    onInput: () => { set(inp.value); markDirty(); if (opts.after) opts.after(); } });
  return field(label, inp, opts.hint);
}
function numField(label, value, min, max, set, hint) {
  const inp = h('input', { type: 'number', inputmode: 'numeric', min, max, step: 1, value,
    onInput: () => { const n = inp.valueAsNumber; if (!Number.isNaN(n)) { set(Math.round(n)); markDirty(); } } });
  return field(label, inp, hint);
}
function selectField(label, options, value, set, hint) {
  return field(label, selectEl(options, value, set, label), hint);
}
function selectEl(options, value, set, aria) {
  const sel = h('select', { 'aria-label': aria || null, onChange: () => set(sel.value) },
    options.map(o => { const [v, t] = Array.isArray(o) ? o : [o, o]; return h('option', { value: v, text: t }); }));
  sel.value = value;
  return sel;
}
/** Color input; optional=true adds an on/off checkbox (value '' = off). */
function colorField(label, value, set, optional, rerender) {
  const inp = h('input', { type: 'color', value: value || '#ffffff', 'aria-label': label, disabled: optional && !value,
    onInput: () => { set(inp.value); markDirty(); } });
  if (!optional) return h('div', { class: 'field' }, h('span', { class: 'lbl', text: label }), inp);
  const cb = h('input', { type: 'checkbox', checked: !!value, onChange: () => { set(cb.checked ? inp.value : ''); markDirty(); rerender(); } });
  return h('div', { class: 'row' }, h('label', { class: 'check grow' }, cb, label), inp);
}
function iconPicker(value, set, noneLabel, rerender) {
  const opts = [{ id: '', name: noneLabel }].concat(cfg.icons);
  return h('div', { class: 'icon-grid icon-pick', role: 'radiogroup', 'aria-label': 'Icon' },
    opts.map(o => h('button', { type: 'button', class: 'icon-btn', role: 'radio', 'aria-checked': String((value || '') === o.id), title: o.name,
      onClick: () => { set(o.id); markDirty(); rerender(); } },
      iconCanvas(o.id), h('span', { text: o.name }))));
}

/* ---- navigation ---- */
const VIEWS = [['keys', 'Keys'], ['icons', 'Icons'], ['settings', 'Global'], ['leds', 'LEDs'], ['backup', 'Backup']];
function renderNav() {
  const nav = $('#nav');
  nav.replaceChildren(...VIEWS.map(([id, label]) =>
    h('button', { type: 'button', 'aria-current': state.view === id ? 'page' : null, onClick: () => go(id), text: label })));
}
function go(view) {
  if (state.view === 'leds' && view !== 'leds') restoreLeds();
  state.view = view; state.editing = null; renderSheet(); render();
  window.scrollTo(0, 0);
}
function render() {
  renderNav();
  const main = $('#view');
  const fn = { keys: viewKeys, icons: viewIcons, settings: viewSettings, leds: viewLeds, backup: viewBackup }[state.view];
  main.replaceChildren(fn());
  markDirty();
}

/* ======================================================================
 * 6. Keys view + editor sheet
 * ==================================================================== */
function missingIcons() {
  const have = new Set(cfg.icons.map(i => i.id)), miss = new Set();
  const scan = o => { if (o && typeof o === 'object') for (const [k, v] of Object.entries(o)) {
    if (k === 'icon' && typeof v === 'string' && v && !have.has(v)) miss.add(v); else scan(v); } };
  scan(cfg.pages);
  return miss;
}
function viewKeys() {
  const pg = page();
  const root = h('div');
  if (!cfg.icons.length || missingIcons().size) {
    root.append(h('div', { class: 'card', role: 'region', 'aria-label': 'Starter icons' },
      h('h2', { text: 'Icons are missing' }),
      h('p', { text: 'Some keys refer to icons that are not on the pad yet.' }),
      h('button', { type: 'button', class: 'btn primary', style: 'width:100%', text: 'Generate starter icons', onClick: generateBuiltins })));
  }
  const tabsBox = h('div', { class: 'tabs', role: 'tablist', 'aria-label': 'Pages' });
  root.append(tabsBox);
  function refreshTabs() {
    tabsBox.replaceChildren(...cfg.pages.map((p, i) => h('button', { type: 'button', class: 'tab', role: 'tab',
      'aria-selected': String(i === state.pageIdx), onClick: () => { state.pageIdx = i; render(); } },
      p.name, i === state.devPage ? h('span', { class: 'dev', title: 'Showing on device', 'aria-label': '(on device)', text: '●' }) : null)));
    if (cfg.pages.length < 8) tabsBox.append(h('button', { type: 'button', class: 'tab', 'aria-label': 'Add page', onClick: addPage, text: '+ Add' }));
  }
  refreshTabs();

  root.append(h('div', { class: 'row' },
    h('button', { type: 'button', class: 'btn primary grow', onClick: showOnDevice, text: 'Show on device' }),
    h('button', { type: 'button', class: 'btn', 'aria-label': 'Move page left', disabled: state.pageIdx === 0, onClick: () => movePage(-1), text: '◀' }),
    h('button', { type: 'button', class: 'btn', 'aria-label': 'Move page right', disabled: state.pageIdx === cfg.pages.length - 1, onClick: () => movePage(1), text: '▶' }),
    h('button', { type: 'button', class: 'btn danger', disabled: cfg.pages.length <= 1, onClick: deletePage, text: 'Delete' })));

  const badges = k => h('span', { class: 'badges' }, [k.single && 'S', k.double && 'D', k.hold && 'H'].filter(Boolean).map(t => h('i', { text: t })));
  root.append(h('div', { class: 'tiles' }, pg.keys.map((k, i) => {
    const led = h('span', { class: 'led' });
    if (k.idle.color) led.style.background = k.idle.color;
    return h('button', { type: 'button', class: 'tile', 'aria-label': `Edit key ${i + 1}`, onClick: () => openSheet(i) },
      iconCanvas(k.idle.icon), led, h('span', { class: 'num', text: String(i + 1) }), badges(k));
  })));
  root.append(h('div', { class: 'enc-wrap' },
    h('button', { type: 'button', class: 'enc-tile', 'aria-label': 'Edit encoder push button', onClick: () => openSheet('enc') },
      h('span', { text: 'Encoder' }), h('span', { class: 'muted small', text: 'push' }), badges(pg.encoder))));
  root.append(h('p', { class: 'muted small', text: 'Tap a key to edit it. S = single, D = double, H = hold.' }));

  // page settings
  const nameInp = textField('Page name (1-15 characters)', pg.name, v => { pg.name = v; }, { max: 15, after: refreshTabs });
  root.append(h('section', { class: 'card', 'aria-label': 'Page settings' },
    h('h2', { text: 'Page settings' }),
    nameInp,
    selectField('LED effect', EFFECTS.map(e => [e, e === 'rainbow' && !cfg.settings.ledHue ? 'wave (white/pink)' : e]), pg.led.effect, v => { pg.led.effect = v; markDirty(); }),
    colorField('LED color', pg.led.color, v => { pg.led.color = v; }, false),
    numField('LED speed (1-100)', pg.led.speed, 1, 100, v => { pg.led.speed = v; }),
    hueNotice()));
  return root;
}

function addPage() {
  if (cfg.pages.length >= 8) return;
  cfg.pages.push(blankPage(cfg.pages.length + 1));
  state.pageIdx = cfg.pages.length - 1; render();
}
function movePage(d) {
  const i = state.pageIdx, j = i + d;
  if (j < 0 || j >= cfg.pages.length) return;
  const act = cfg.settings.activePage;
  [cfg.pages[i], cfg.pages[j]] = [cfg.pages[j], cfg.pages[i]];
  if (act === i) cfg.settings.activePage = j; else if (act === j) cfg.settings.activePage = i;
  state.pageIdx = j; render();
}
function deletePage() {
  if (cfg.pages.length <= 1 || !confirm(`Delete page "${page().name}"?`)) return;
  const i = state.pageIdx;
  cfg.pages.splice(i, 1);
  const a = cfg.settings.activePage;
  if (a === i) cfg.settings.activePage = 0; else if (a > i) cfg.settings.activePage = a - 1;
  state.pageIdx = Math.min(i, cfg.pages.length - 1); render();
}
async function showOnDevice() {
  if (isDirty()) return toast('Save first so page numbers match the device', 'err');
  const r = await guard(() => api('POST', '/api/page', { index: state.pageIdx }), 'Showing "' + page().name + '"');
  if (r) { state.devPage = state.pageIdx; render(); }
}

/* ---- bottom sheet key editor ---- */
const TABS = [['idle', 'Idle'], ['single', 'Single'], ['double', 'Double'], ['hold', 'Hold']];
function openSheet(keyId) {
  state.editing = { key: keyId, tab: keyId === 'enc' ? 'single' : 'idle', fresh: true };
  renderSheet();
}
function closeSheet() { state.editing = null; renderSheet(); render(); }
function editedKey() { return state.editing.key === 'enc' ? page().encoder : page().keys[state.editing.key]; }

function renderSheet() {
  const s = $('#sheet');
  if (!state.editing) { s.hidden = true; s.replaceChildren(); return; }
  const ed = state.editing, enc = ed.key === 'enc';
  const old = s.querySelector('.sheet-body');
  const top = old ? old.scrollTop : 0;
  const rerender = () => renderSheet();
  const key = editedKey();
  const tabs = TABS.filter(t => !(enc && t[0] === 'idle'));
  const body = h('div', { class: 'sheet-body' });
  if (ed.tab === 'idle') body.append(idleEditor(key, rerender));
  else if (ed.tab === 'hold') body.append(holdEditor(key, enc, rerender));
  else body.append(gestureEditor(key, ed.tab, enc, rerender));

  const doneBtn = h('button', { type: 'button', class: 'btn primary', onClick: closeSheet, text: 'Done' });
  const panel = h('div', { class: 'sheet-panel', role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'sheetTitle' },
    h('div', { class: 'sheet-head' }, h('h2', { id: 'sheetTitle', tabindex: '-1', text: enc ? 'Encoder push' : 'Key ' + (ed.key + 1) }), doneBtn),
    h('div', { class: 'sheet-tabs', role: 'tablist' }, tabs.map(([id, label]) =>
      h('button', { type: 'button', role: 'tab', 'aria-selected': String(ed.tab === id),
        text: label + (id !== 'idle' && key[id] ? ' •' : ''), onClick: () => { ed.tab = id; renderSheet(); } }))),
    body);
  s.replaceChildren(h('div', { class: 'sheet-backdrop', onClick: closeSheet }), panel);
  s.hidden = false;
  body.scrollTop = top;
  if (ed.fresh) { ed.fresh = false; panel.querySelector('#sheetTitle').focus(); }
}

function idleEditor(key, rerender) {
  return h('div', null,
    h('h3', { text: 'Idle icon' }),
    iconPicker(key.idle.icon, v => { key.idle.icon = v; }, 'None', rerender),
    h('h3', { text: 'Idle LED color' }),
    colorField('Light the key LED', key.idle.color, v => { key.idle.color = v; }, true, rerender),
    hueNotice(),
    h('p', { class: 'hint', text: 'Shown whenever the key is not being used.' }));
}

/** Visual override block shared by single/double/hold/after. */
function visualBlock(obj, rerender, iconLabel, iconNone, colorLabel) {
  return h('div', null,
    h('h3', { text: iconLabel }),
    iconPicker(obj.icon || '', v => { if (v) obj.icon = v; else delete obj.icon; }, iconNone, rerender),
    colorField(colorLabel, obj.color || '', v => { if (v) obj.color = v; else delete obj.color; }, true, rerender),
    hueNotice());
}

function enableToggle(key, name, label, make, rerender) {
  const cb = h('input', { type: 'checkbox', checked: !!key[name], onChange: () => {
    if (cb.checked) key[name] = make(); else delete key[name];
    markDirty(); rerender();
  } });
  return h('label', { class: 'check' }, cb, label);
}

function gestureEditor(key, name, enc, rerender) {
  const label = name === 'single' ? 'Enable single press' : 'Enable double press';
  const root = h('div', null, enableToggle(key, name, label, () => ({ steps: [] }), rerender));
  if (name === 'single' && key.double) root.append(h('p', { class: 'hint', text: 'Because a double press exists, a single press waits for the double-press window before firing.' }));
  const g = key[name];
  if (!g) return root;
  root.append(h('h3', { text: 'Steps' }), stepList(g.steps, rerender));
  if (!enc) {
    root.append(visualBlock(g, rerender, 'Icon while active', 'Keep idle icon', 'Override LED color'));
  }
  root.append(msControl('Show for', g, rerender));
  return root;
}

/** ms: 0 = default (revertMs), 65535 = until next press, else custom. */
function msControl(label, obj, rerender) {
  const ms = obj.ms || 0;
  const mode = ms === 0 ? 'default' : ms === MS_FOREVER ? 'forever' : 'custom';
  const sel = selectEl([['default', 'default (revert time)'], ['custom', 'custom ms'], ['forever', 'until next press']], mode, v => {
    if (v === 'default') delete obj.ms; else if (v === 'forever') obj.ms = MS_FOREVER; else obj.ms = ms && ms !== MS_FOREVER ? ms : 1000;
    markDirty(); rerender();
  }, label);
  const root = h('div', null, field(label, sel));
  if (mode === 'custom') root.append(numField('Duration (ms, 1-65534)', ms, 1, 65534, v => { obj.ms = Math.min(65534, Math.max(1, v)); }));
  return root;
}

function holdEditor(key, enc, rerender) {
  const root = h('div', null, enableToggle(key, 'hold', 'Enable hold', () => ({ start: [], end: [] }), rerender));
  const hd = key.hold;
  if (!hd) { root.append(h('p', { class: 'hint', text: 'Without hold, a long press counts as an ordinary single press.' })); return root; }
  root.append(h('div', { class: 'notice info' },
    'Start steps run when the hold threshold is crossed; end steps run when the button is released. ',
    h('strong', { text: 'Any key still held down after the end steps is released automatically.' }),
    ' (So a "key down" in Start does not need a matching "key up" in End, but adding one is clearer.)'));
  root.append(h('h3', { text: 'Start steps (on hold)' }), stepList(hd.start, rerender));
  root.append(h('h3', { text: 'End steps (on release)' }), stepList(hd.end, rerender));
  if (!enc) {
    root.append(visualBlock(hd, rerender, 'Icon while held', 'Keep idle icon', 'LED color while held'));
    root.append(enableToggle(hd, 'after', 'Show something after release', () => ({ ms: 0 }), rerender));
    if (hd.after) {
      root.append(visualBlock(hd.after, rerender, 'Icon after release', 'Keep idle icon', 'LED color after release'));
      root.append(msControl('Show for', hd.after, rerender));
    }
  }
  return root;
}

/* ---- step lists ---- */
const STEP_DEFAULTS = {
  key: () => ({ t: 'key', mode: 'tap', mods: [], key: 'a' }),
  text: () => ({ t: 'text', text: '' }),
  media: () => ({ t: 'media', key: 'playpause' }),
  delay: () => ({ t: 'delay', ms: 100 }),
};
function stepList(steps, rerender) {
  const wrap = h('div', { class: 'steps' });
  const change = () => { markDirty(); rerender(); };
  steps.forEach((s, i) => {
    wrap.append(h('div', { class: 'step' },
      h('div', { class: 'step-head' },
        h('strong', { text: `${i + 1}. ${{ key: 'Key', text: 'Text', media: 'Media', delay: 'Delay' }[s.t] || s.t}` }),
        h('button', { type: 'button', class: 'btn', 'aria-label': 'Move step up', disabled: i === 0, text: '↑',
          onClick: () => { [steps[i - 1], steps[i]] = [steps[i], steps[i - 1]]; change(); } }),
        h('button', { type: 'button', class: 'btn', 'aria-label': 'Move step down', disabled: i === steps.length - 1, text: '↓',
          onClick: () => { [steps[i + 1], steps[i]] = [steps[i], steps[i + 1]]; change(); } }),
        h('button', { type: 'button', class: 'btn danger', 'aria-label': 'Remove step', text: '×',
          onClick: () => { steps.splice(i, 1); change(); } })),
      stepBody(s)));
  });
  if (!steps.length) wrap.append(h('p', { class: 'muted small', text: 'No steps yet.' }));
  const addSel = selectEl([['', 'Add step…'], ['key', 'Key press'], ['text', 'Type text'], ['media', 'Media key'], ['delay', 'Delay']], '', v => {
    if (v && steps.length >= MAX_STEPS) toast(`At most ${MAX_STEPS} steps`, 'err');
    else if (v) { steps.push(STEP_DEFAULTS[v]()); change(); }
  }, 'Add step');
  wrap.append(h('div', { class: 'row' },
    h('div', { class: 'grow' }, addSel),
    h('button', { type: 'button', class: 'btn', disabled: !steps.length, text: 'Test',
      onClick: () => guard(() => api('POST', '/api/test', { steps }), 'Test sent') })));
  return wrap;
}

function stepBody(s) {
  if (s.t === 'key') {
    const chips = h('div', { class: 'chips', role: 'group', 'aria-label': 'Modifiers' }, MODS.map(m => {
      const b = h('button', { type: 'button', class: 'chip', 'aria-pressed': String(s.mods.includes(m)), text: m,
        onClick: () => {
          s.mods = s.mods.includes(m) ? s.mods.filter(x => x !== m) : MODS.filter(x => x === m || s.mods.includes(x));
          b.setAttribute('aria-pressed', String(s.mods.includes(m))); markDirty();
        } });
      return b;
    }));
    const keySel = h('select', { 'aria-label': 'Key', onChange: () => { s.key = keySel.value; markDirty(); } },
      h('option', { value: '', text: '(none - modifiers only)' }),
      KEY_GROUPS.map(([g, names]) => h('optgroup', { label: g }, names.map(n => h('option', { value: n, text: n })))));
    keySel.value = s.key;
    return h('div', null,
      field('Mode', selectEl([['tap', 'tap (press + release)'], ['down', 'down (hold until "up")'], ['up', 'up (release)']], s.mode, v => { s.mode = v; markDirty(); })),
      h('div', { class: 'lbl small muted', text: 'Modifiers' }), chips,
      field('Key', keySel));
  }
  if (s.t === 'text') {
    const inp = h('input', { type: 'text', value: escapeText(s.text), maxlength: MAX_TEXT, autocomplete: 'off', autocapitalize: 'off', spellcheck: 'false', 'aria-label': 'Text to type',
      onInput: () => { s.text = unescapeText(inp.value); markDirty(); } });
    return h('div', null, inp,
      h('div', { class: 'hint', text: 'ASCII, US layout. Write \\n for Enter and \\t for Tab.' }),
      h('button', { type: 'button', class: 'btn', text: '+ Enter at end', onClick: () => { inp.value += '\\n'; s.text = unescapeText(inp.value); markDirty(); } }));
  }
  if (s.t === 'media') return field('Media key', selectEl(MEDIA_KEYS, s.key, v => { s.key = v; markDirty(); }));
  if (s.t === 'delay') return numField('Delay (ms, 0-5000)', s.ms, 0, 5000, v => { s.ms = v; });
  return h('p', { text: 'Unknown step' });
}

/* ======================================================================
 * 7. Icons view
 * ==================================================================== */
function viewIcons() {
  const root = h('div');
  const fileInp = h('input', { type: 'file', accept: 'image/*', hidden: true, onChange: () => pickImage(fileInp) });
  const camInp = h('input', { type: 'file', accept: 'image/*', capture: 'environment', hidden: true, onChange: () => pickImage(camInp) });
  root.append(h('h2', { text: 'Icon library' }),
    h('div', { class: 'row' },
      h('button', { type: 'button', class: 'btn primary grow', text: 'Upload image', onClick: () => fileInp.click() }),
      h('button', { type: 'button', class: 'btn grow', text: 'Take photo', onClick: () => camInp.click() }),
      h('button', { type: 'button', class: 'btn grow', text: 'Text-only icon', onClick: () => { startMaker(null, ''); } })),
    fileInp, camInp,
    h('div', { class: 'row', style: 'margin-top:8px' },
      h('button', { type: 'button', class: 'btn grow', text: 'Generate built-in icons', onClick: generateBuiltins })));
  if (state.maker) root.append(makerPanel());
  root.append(h('div', { class: 'icon-grid', style: 'margin-top:12px' }, cfg.icons.map(ic =>
    h('div', { class: 'icon-card' }, iconCanvas(ic.id), h('div', { class: 'nm', text: ic.name }), h('div', { class: 'muted nm', text: ic.id }),
      h('button', { type: 'button', class: 'btn danger', text: 'Delete', 'aria-label': 'Delete icon ' + ic.name, onClick: () => deleteIcon(ic) })))));
  if (!cfg.icons.length) root.append(h('p', { class: 'muted', text: 'No icons yet. Upload one or generate the built-in set.' }));
  return root;
}

function iconsChanged(id) {
  iconCache.delete(id);
}
function registerIcon(id, name) {
  const ex = cfg.icons.find(i => i.id === id);
  if (ex) { if (name) ex.name = name; } else cfg.icons.push({ id, name: name || id });
}
async function uploadIcon(id, bytes, name) {
  await apiUpload('/api/icons/' + encodeURIComponent(id), bytes, id + '.565');
  iconsChanged(id); registerIcon(id, name);
}
async function deleteIcon(ic) {
  const used = JSON.stringify(cfg.pages).split('"' + ic.id + '"').length - 1;
  if (!confirm(`Delete icon "${ic.name}"?` + (used ? `\nIt is used ${used} time(s); those keys will show a "?".` : ''))) return;
  const ok = await guard(() => api('DELETE', '/api/icons/' + encodeURIComponent(ic.id)), 'Deleted');
  if (ok) { iconsChanged(ic.id); cfg.icons = cfg.icons.filter(i => i.id !== ic.id); render(); }
}
async function generateBuiltins() {
  let n = 0;
  for (const b of BUILTIN_ICONS) {
    const ok = await guard(() => uploadIcon(b.id, renderBuiltinBytes(b), b.name));
    if (ok === undefined) { toast(`Stopped after ${n} icon(s)`, 'err'); render(); return; }
    n++; toast(`Uploading ${n}/${BUILTIN_ICONS.length}…`);
  }
  toast(`Generated ${n} icons (remember to Save)`, 'ok'); render();
}

async function loadBitmap(file) {
  if (window.createImageBitmap) { try { return await createImageBitmap(file); } catch (e) { /* fall through */ } }
  return new Promise((res, rej) => {
    const url = URL.createObjectURL(file), img = new Image();
    img.onload = () => { URL.revokeObjectURL(url); res(img); };
    img.onerror = () => { URL.revokeObjectURL(url); rej(new Error('Could not read that image')); };
    img.src = url;
  });
}
async function pickImage(inp) {
  const f = inp.files && inp.files[0];
  inp.value = '';
  if (!f) return;
  const bmp = await guard(() => loadBitmap(f));
  if (bmp) startMaker(bmp, sanitizeId(f.name) || 'icon');
}
function startMaker(bitmap, id) {
  state.maker = { bitmap, id, name: id, label: '', size: 24, lcolor: '#ffffff', bg: '#000000', fit: 'cover' };
  render();
}
function drawMaker(cv) {
  const m = state.maker, c = cv.getContext('2d');
  c.fillStyle = m.bg; c.fillRect(0, 0, ICON_SIZE, ICON_SIZE);
  if (m.bitmap) {
    const w = m.bitmap.width, hh = m.bitmap.height;
    const sc = (m.fit === 'cover' ? Math.max : Math.min)(ICON_SIZE / w, ICON_SIZE / hh);
    c.drawImage(m.bitmap, (ICON_SIZE - w * sc) / 2, (ICON_SIZE - hh * sc) / 2, w * sc, hh * sc);
  }
  if (m.label) {
    c.font = `bold ${m.size}px sans-serif`; c.textAlign = 'center'; c.textBaseline = 'bottom';
    c.lineJoin = 'round'; c.lineWidth = Math.max(2, m.size / 5); c.strokeStyle = 'rgba(0,0,0,0.85)';
    c.strokeText(m.label, 64, 124, 120); c.fillStyle = m.lcolor; c.fillText(m.label, 64, 124, 120);
  }
}
function makerPanel() {
  const m = state.maker;
  const prev = h('canvas', { class: 'maker-prev', width: ICON_SIZE, height: ICON_SIZE, role: 'img', 'aria-label': 'Icon preview' });
  const redraw = () => drawMaker(prev);
  redraw();
  const upd = (k, post) => v => { m[k] = post ? post(v) : v; redraw(); };
  return h('section', { class: 'card', 'aria-label': 'New icon' },
    h('h2', { text: 'New icon' }), prev,
    m.bitmap ? selectField('Fit', [['cover', 'Crop to square (cover)'], ['contain', 'Whole image (letterbox)']], m.fit, upd('fit')) : null,
    textField('Label (bottom of icon, optional)', m.label, upd('label'), { max: 20 }),
    numField('Label size (8-48 px)', m.size, 8, 48, upd('size')),
    colorField('Label color', m.lcolor, upd('lcolor'), false),
    colorField('Background color', m.bg, upd('bg'), false),
    textField('Icon id (a-z, 0-9, _ and -, max 24)', m.id, upd('id', sanitizeId)),
    textField('Display name', m.name, upd('name')),
    h('div', { class: 'row' },
      h('button', { type: 'button', class: 'btn primary grow', text: 'Save icon', onClick: async () => {
        const id = sanitizeId(m.id);
        if (!ID_RE.test(id)) return toast('Icon id must be 1-24 of a-z 0-9 _ -', 'err');
        const cv = newCanvas(); drawMaker(cv);
        const ok = await guard(() => uploadIcon(id, canvasToBytes(cv), m.name || id), 'Icon uploaded (remember to Save)');
        if (ok !== undefined) { state.maker = null; render(); }
      } }),
      h('button', { type: 'button', class: 'btn grow', text: 'Cancel', onClick: () => { state.maker = null; render(); } })));
}

/* ======================================================================
 * 8. Settings / LEDs / Backup views
 * ==================================================================== */
function viewSettings() {
  const s = cfg.settings;
  const pw = textField('Wi-Fi password (8-63)', s.apPass, v => { s.apPass = v; }, { type: 'password', max: 63 });
  const show = h('input', { type: 'checkbox', onChange: () => { pw.querySelector('input').type = show.checked ? 'text' : 'password'; } });
  return h('div', null,
    h('h2', { text: 'Global settings' }),
    h('section', { class: 'card' },
      h('h3', { text: 'Wi-Fi access point' }),
      h('div', { class: 'notice', text: 'Changing the name or password disconnects your phone when you save. Reconnect to the new network afterwards.' }),
      textField('Wi-Fi name (1-31)', s.apSsid, v => { s.apSsid = v; }, { max: 31 }),
      pw, h('label', { class: 'check' }, show, 'Show password'),
      h('p', { class: 'hint', text: 'Wi-Fi can be turned off/on from the pad: hold the knob for 3 seconds to see the network name, password and address, then push the knob to switch Wi-Fi.' })),
    h('section', { class: 'card' },
      h('h3', { text: 'Display' }),
      numField('Screen brightness (5-100 %)', s.brightness, 5, 100, v => { s.brightness = v; }),
      numField('LED brightness (0-100 %)', s.ledBrightness, 0, 100, v => { s.ledBrightness = v; }),
      h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: !!s.ledHue, 'aria-describedby': 'ledHueHelp',
        onChange: e => { s.ledHue = e.target.checked; markDirty(); } }), "LED colors: real hue (needs the fixed STM32 firmware, see docs/STM32_UPDATE.md). Turn off for white/pink only."),
      h('div', { class: 'hint', id: 'ledHueHelp', text: "Hue needs LilyGo's fixed STM32 firmware; without it colors show as white or pink." }),
      numField('Dim after (minutes, 0 = never)', s.dimMinutes, 0, 600, v => { s.dimMinutes = v; })),
    h('section', { class: 'card' },
      h('h3', { text: 'Timing' }),
      numField('Double-press window (ms)', s.doubleMs, 100, 1000, v => { s.doubleMs = v; }),
      numField('Hold threshold (ms)', s.holdMs, 150, 3000, v => { s.holdMs = v; }),
      numField('Default revert time (ms)', s.revertMs, 100, 30000, v => { s.revertMs = v; }, 'How long temporary icons/colors are shown when a gesture sets ms = 0.')));
}

/* ---- LED calibration ---- */
function zoneOf(i) { return cfg.settings.ledZones.findIndex(m => (m >> i) & 1); }
function probe(mask) { return guard(() => api('POST', '/api/leds/probe', { mask })); }
function restoreLeds() { probe(0); }
function viewLeds() {
  const z = cfg.settings.ledZones, sel = state.ledSel;
  const root = h('div', null, h('h2', { text: 'LED calibration' }), hueNotice(),
    h('p', { class: 'muted', text: 'Tap an LED number: that single LED lights up white on the pad. Then say which key it sits above.' }));
  root.append(h('div', { class: 'led-grid' }, Array.from({ length: 14 }, (_, i) => {
    const zi = zoneOf(i);
    return h('button', { type: 'button', class: 'led-btn' + (zi >= 0 ? ' zone-' + (zi + 1) : ''), 'aria-pressed': String(sel === i),
      'aria-label': `LED ${i + 1}` + (zi >= 0 ? `, key ${zi + 1}` : ', unassigned'),
      onClick: () => { state.ledSel = i; probe(1 << i); render(); } },
      String(i + 1), h('small', { text: zi >= 0 ? 'K' + (zi + 1) : '-' }));
  })));
  if (sel !== null) {
    const cur = zoneOf(sel);
    const assign = zi => {
      for (let k = 0; k < 4; k++) z[k] &= ~(1 << sel);
      if (zi >= 0) z[zi] |= 1 << sel;
      render();
    };
    root.append(h('section', { class: 'card' }, h('h3', { text: `LED ${sel + 1} belongs above:` }),
      h('div', { class: 'chips', role: 'group', 'aria-label': 'Assign LED to key' },
        [0, 1, 2, 3].map(k => h('button', { type: 'button', class: 'chip', 'aria-pressed': String(cur === k), text: 'Key ' + (k + 1), onClick: () => assign(k) })),
        h('button', { type: 'button', class: 'chip', 'aria-pressed': String(cur === -1), text: 'None', onClick: () => assign(-1) }))));
  }
  const rows = z.map((m, k) => {
    const leds = Array.from({ length: 14 }, (_, i) => i).filter(i => (m >> i) & 1).map(i => i + 1);
    return h('tr', null, h('td', { text: 'Key ' + (k + 1) }), h('td', { text: leds.length ? leds.join(', ') : '(none)' }), h('td', { class: 'muted', text: String(m) }));
  });
  root.append(h('section', { class: 'card' }, h('h3', { text: 'Current zones' }),
    h('table', { class: 'zones' }, h('tr', null, h('th', { text: 'Key' }), h('th', { text: 'LEDs' }), h('th', { text: 'Mask' })), rows),
    h('div', { class: 'row', style: 'margin-top:8px' },
      h('button', { type: 'button', class: 'btn grow', text: 'Restore LEDs', onClick: () => { state.ledSel = null; restoreLeds(); render(); } }),
      h('button', { type: 'button', class: 'btn grow', text: 'Reset zones', onClick: () => { cfg.settings.ledZones = DEFAULT_ZONES.slice(); render(); } })),
    h('p', { class: 'hint', text: 'Zones are saved with the rest of the config (Save bar).' })));
  return root;
}

/* ---- Backup ---- */
function viewBackup() {
  const imp = h('input', { type: 'file', accept: 'application/json,.json', hidden: true, onChange: () => importFile(imp) });
  const st = state.status;
  return h('div', null, h('h2', { text: 'Backup & reset' }),
    st ? h('p', { class: 'muted small', text: `Firmware ${st.fw} - storage ${Math.round(st.fsUsed / 1024)} / ${Math.round(st.fsTotal / 1024)} KB - free heap ${Math.round(st.heapFree / 1024)} KB` }) : null,
    h('section', { class: 'card' },
      h('button', { type: 'button', class: 'btn primary', style: 'width:100%', text: 'Export backup (.json)', onClick: exportBackup }),
      h('button', { type: 'button', class: 'btn', style: 'width:100%;margin-top:8px', text: 'Import backup…', onClick: () => imp.click() }), imp,
      h('p', { class: 'hint', text: 'Import replaces the whole config and all icons.' })),
    h('section', { class: 'card' },
      h('button', { type: 'button', class: 'btn danger', style: 'width:100%', text: 'Factory reset', onClick: factoryReset }),
      h('p', { class: 'hint', text: 'Wipes config and icons, then reboots the pad.' })));
}
async function exportBackup() {
  if (isDirty() && !confirm('You have unsaved changes; the backup contains the last saved version. Continue?')) return;
  const out = await guard(async () => {
    const config = await api('GET', '/api/config');
    const icons = {};
    let n = 0;
    for (const ic of config.icons || []) {
      toast(`Reading icon ${++n}/${config.icons.length}…`);
      try {
        const r = await fetch('/api/icons/' + encodeURIComponent(ic.id));
        if (!r.ok) continue;
        const b = new Uint8Array(await r.arrayBuffer());
        if (b.length === ICON_BYTES) icons[ic.id] = bytesToBase64(b);
      } catch (e) { /* skip failed icon */ }
    }
    return { config, icons };
  });
  if (!out) return;
  const url = URL.createObjectURL(new Blob([JSON.stringify(out)], { type: 'application/json' }));
  const a = h('a', { href: url, download: 'claudepad-backup.json' });
  document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
  toast(`Backup downloaded (${Object.keys(out.icons).length} icons)`, 'ok');
}
async function importFile(inp) {
  const f = inp.files && inp.files[0]; inp.value = '';
  if (!f) return;
  let data;
  try { data = JSON.parse(await f.text()); } catch (e) { return toast('Not a valid JSON file', 'err'); }
  if (!data || !data.config) return toast('File has no "config"', 'err');
  const errs = validateConfig(normalizeConfig(data.config));
  if (errs.length) return toast('Backup is invalid: ' + errs[0], 'err');
  if (!confirm('Replace the entire config and all icons with this backup?')) return;
  const ok = await guard(async () => {
    await api('PUT', '/api/config', data.config);
    const ids = Object.keys(data.icons || {});
    let n = 0;
    for (const id of ids) {
      toast(`Importing icon ${++n}/${ids.length}…`);
      if (!ID_RE.test(id)) continue;
      await apiUpload('/api/icons/' + encodeURIComponent(id), base64ToBytes(data.icons[id]), id + '.565');
    }
    return true;
  }, 'Imported');
  if (ok) await reload();
}
async function factoryReset() {
  if (!confirm('Factory reset: erase ALL pages, icons and settings? This cannot be undone.')) return;
  const ok = await guard(() => api('POST', '/api/factory-reset'), 'Resetting - the pad is rebooting');
  if (ok) { await sleep(state.mock ? 100 : 6000); await reload(); }
}

/* ======================================================================
 * 9. Save logic + boot
 * ==================================================================== */
async function save() {
  const errs = validateConfig(cfg);
  if (errs.length) return toast(errs[0] + (errs.length > 1 ? ` (+${errs.length - 1} more)` : ''), 'err');
  const was = JSON.parse(savedStr).settings;
  const wifi = was.apSsid !== cfg.settings.apSsid || was.apPass !== cfg.settings.apPass;
  if (wifi && !confirm('Wi-Fi name/password changed. Your phone will be disconnected; reconnect to the new network. Save?')) return;
  const btn = $('#saveBtn'); btn.disabled = true;
  try {
    await api('PUT', '/api/config', cfg);
    savedStr = JSON.stringify(cfg);
    toast(wifi ? 'Saved. Reconnect to the new Wi-Fi.' : 'Saved', 'ok');
  } catch (e) { toast('Save failed: ' + e.message, 'err'); }
  btn.disabled = false; markDirty();
}
function discard() {
  if (!confirm('Discard all unsaved changes?')) return;
  cfg = JSON.parse(savedStr); state.pageIdx = Math.min(state.pageIdx, cfg.pages.length - 1);
  state.editing = null; renderSheet(); render();
}

async function reload() {
  const [c, st] = await Promise.all([api('GET', '/api/config'), api('GET', '/api/status').catch(() => null)]);
  cfg = normalizeConfig(c); savedStr = JSON.stringify(cfg);
  state.status = st; iconCache.clear();
  state.devPage = st ? st.activePage : cfg.settings.activePage;
  state.pageIdx = Math.min(state.pageIdx, cfg.pages.length - 1);
  state.maker = null; state.ledSel = null; state.editing = null;
  renderSheet(); render();
  $('#statusLine').textContent = st ? `fw ${st.fw}` : '';
}

function isDevHost() {
  return location.protocol === 'file:' || ['localhost', '127.0.0.1', '[::1]', ''].includes(location.hostname);
}

async function boot() {
  $('#saveBtn').addEventListener('click', save);
  $('#discardBtn').addEventListener('click', discard);
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && state.editing) closeSheet(); });
  window.addEventListener('beforeunload', e => { if (cfg && isDirty()) { e.preventDefault(); e.returnValue = ''; } });
  if (/[?&]mock\b/.test(location.search)) installMock();
  else {
    try { const r = await fetch('/api/status'); if (!r.ok) throw new Error('status ' + r.status); await r.json(); }
    catch (e) {
      if (isDevHost()) installMock();
      else { $('#view').replaceChildren(h('div', { class: 'card' }, h('h2', { text: 'Cannot reach the pad' }),
        h('p', { text: String(e.message || e) }), h('button', { type: 'button', class: 'btn primary', text: 'Retry', onClick: () => location.reload() }))); return; }
    }
  }
  $('#mockBadge').hidden = !state.mock;
  try { await reload(); }
  catch (e) { $('#view').replaceChildren(h('div', { class: 'card' }, h('h2', { text: 'Could not load config' }), h('p', { text: e.message }),
    h('button', { type: 'button', class: 'btn primary', text: 'Retry', onClick: () => location.reload() }))); }
}

boot();

} // end browser-only section
