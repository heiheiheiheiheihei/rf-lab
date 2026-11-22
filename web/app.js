/* RF-LAB FIELD TERMINAL — transport-agnostic UI.
 * HTTP + SSE (ESP32 web server) or BLE (Android WebView bridge).
 * No external requests, no build-time framework.
 */
(() => {
  'use strict';

  const $ = (id) => document.getElementById(id);
  const hasBridge = typeof window.RFBridge !== 'undefined';
  const UI_HASH = ((document.querySelector('meta[name="rflab-ui"]') || {}).content || '--').slice(0, 8);
  const S = {
    transport: hasBridge ? 'ble' : 'http',
    connected: false,
    authed: false,
    codes: [],
    selected: 0,
    codebook: 0,
    filterGroup: '',
    search: '',
    editing: null,
    pendingNew: null,
    selectAfterId: 0,
    state: null,
    stateAt: 0,
    armed: false,
    txId: 0,
    es: null,
    logCount: 0,
    lastScan: [],
    fw: '',
    build: ''
  };

  const el = {
    clock: $('clock'), date: $('date'), uptime: $('uptime'),
    linkDot: $('linkDot'), linkLabel: $('linkLabel'),
    fw: $('fw'), net: $('net'), ble: $('ble'), modBaud: $('modBaud'), oled: $('oled'),
    armChip: $('armChip'), armDot: $('armDot'), armLabel: $('armLabel'), armBtn: $('armBtn'),
    stopBtn: $('stopBtn'), lastCode: $('lastCode'), lastBand: $('lastBand'), lastId: $('lastId'),
    listenTag: $('listenTag'), storeTag: $('storeTag'), codeList: $('codeList'), emptyState: $('emptyState'),
    newCode: $('newCode'), codeSearch: $('codeSearch'), groupChips: $('groupChips'), codebookGate: $('codebookGate'),
    codeModal: $('codeModal'), codeForm: $('codeForm'), codeName: $('codeName'), codeSym: $('codeSym'),
    codeGroup: $('codeGroup'), codeHex: $('codeHex'), codeTitle: $('codeModalTitle'),
    framePreview: $('framePreview'), codeError: $('codeError'), codeNote: $('codeNote'),
    codeDelete: $('codeDelete'), codeReset: $('codeReset'), codeSave: $('codeSave'),
    nameCount: $('nameCount'), symCount: $('symCount'), groupCount: $('groupCount'),
    symPalette: $('symPalette'), groupOptions: $('groupOptions'),
    log: $('log'), tStore: $('tStore'), tFrames: $('tFrames'), tAcks: $('tAcks'), tStray: $('tStray'),
    tUplink: $('tUplink'), tRssi: $('tRssi'), tUptime: $('tUptime'),
    helpModal: $('helpModal'), pinModal: $('pinModal'), scanModal: $('scanModal'), hwModal: $('hwModal'),
    otaModal: $('otaModal'), otaFile: $('otaFile'), otaToken: $('otaToken'), otaStart: $('otaStart'),
    otaBar: $('otaBar'), otaStatus: $('otaStatus'), buildId: $('buildId'), uiId: $('uiId'),
    pinInput: $('pinInput'), pinMsg: $('pinMsg'), scanList: $('scanList'),
    toast: $('toast'), footTail: $('footTail'), wifiForm: $('wifiForm'),
    wifiNote: $('wifiNote'), wifiClear: $('wifiClear')
  };

  const text = (node, value) => { if (node) node.textContent = value == null ? '--' : String(value); };

  function toast(message, warn) {
    if (!el.toast) return;
    el.toast.textContent = message;
    el.toast.classList.toggle('warn', !!warn);
    el.toast.hidden = false;
    clearTimeout(toast._t);
    toast._t = setTimeout(() => { el.toast.hidden = true; }, 2600);
  }

  function log(kind, message) {
    if (!el.log || message == null) return;
    const lines = String(message).replace(/\r/g, '').split('\n');
    for (const line of lines) {
      if (!line && lines.length > 1) continue;
      const row = document.createElement('div');
      row.className = 'log-line ' + (kind || 'sys');
      const t = document.createElement('time');
      const d = new Date();
      t.textContent = [d.getHours(), d.getMinutes(), d.getSeconds()].map(n => String(n).padStart(2, '0')).join(':');
      const s = document.createElement('span');
      s.textContent = line || ' ';
      row.append(t, s);
      el.log.append(row);
      S.logCount++;
    }
    while (S.logCount > 160 && el.log.firstChild) {
      el.log.removeChild(el.log.firstChild);
      S.logCount--;
    }
    const near = el.log.scrollHeight - el.log.scrollTop - el.log.clientHeight < 80;
    if (near) el.log.scrollTop = el.log.scrollHeight;
  }

  function setLink(label, dot) {
    text(el.linkLabel, label);
    if (el.linkDot) el.linkDot.className = 'dot ' + (dot || '');
  }

  function fmtUptime(ms) {
    let sec = Math.floor((ms || 0) / 1000);
    const d = Math.floor(sec / 86400); sec -= d * 86400;
    const h = Math.floor(sec / 3600); sec -= h * 3600;
    const m = Math.floor(sec / 60); sec -= m * 60;
    const hh = String(h).padStart(2, '0'), mm = String(m).padStart(2, '0'), ss = String(sec).padStart(2, '0');
    return (d ? d + 'D ' : '') + hh + ':' + mm + ':' + ss;
  }

  function tickClock() {
    const d = new Date();
    text(el.clock, d.toLocaleTimeString('zh-CN', { hour12: false }));
    text(el.date, d.toISOString().slice(0, 10) + ' // ' + ['SUN','MON','TUE','WED','THU','FRI','SAT'][d.getDay()]);
    if (S.state) {
      const extra = S.stateAt ? (performance.now() - S.stateAt) : 0;
      text(el.uptime, 'UPTIME ' + fmtUptime((S.state.uptime_ms || 0) + extra));
    }
  }
  setInterval(tickClock, 1000);
  tickClock();

  /* ---------------- transport: HTTP + SSE ---------------- */

  async function httpJSON(path, opts) {
    const r = await fetch(path, Object.assign({ cache: 'no-store' }, opts || {}));
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return r.json();
  }

  async function httpRefresh() {
    try {
      const [st, codes] = await Promise.all([httpJSON('/api/state'), httpJSON('/api/codes')]);
      applyState(st);
      setCodes(codes);
      return true;
    } catch (e) {
      setLink('OFFLINE', 'bad');
      S.connected = false;
      log('warn', 'HTTP ' + e.message);
      return false;
    }
  }

  async function httpConnect() {
    try {
      const hello = await httpJSON('/api/hello');
      onHello(hello);
      await httpRefresh();
      setLink('ONLINE', 'ok');
      S.connected = true;
      openSSE();
    } catch (e) {
      setLink('OFFLINE', 'bad');
      log('warn', '无法连接 ' + location.host + ' — ' + e.message);
      setTimeout(httpConnect, 3000);
    }
  }

  function openSSE() {
    if (S.es) S.es.close();
    const es = new EventSource('/api/events');
    S.es = es;
    es.onopen = () => { setLink('ONLINE', 'ok'); S.connected = true; };
    es.onerror = () => { setLink('RECONNECT', 'warn'); S.connected = false; };
    es.addEventListener('hello', (e) => safeParse(e.data, onHello));
    es.addEventListener('state', (e) => safeParse(e.data, (m) => applyState(m.data)));
    es.addEventListener('codes', (e) => safeParse(e.data, (m) => setCodes(m.items)));
    es.addEventListener('event', (e) => safeParse(e.data, onEvent));
  }

  /* ---------------- transport: BLE bridge ---------------- */

  const ble = {
    post(obj) {
      if (!hasBridge) return;
      try { window.RFBridge.postMessage(JSON.stringify(obj)); }
      catch (e) { log('warn', 'BLE bridge: ' + e.message); }
    },
    scan() {
      if (!hasBridge) return;
      S.connected = false;
      setLink('BLE SCAN', 'warn');
      el.scanList.innerHTML = '<div class="empty">SCANNING…</div>';
      show(el.scanModal);
      ble.post({ op: 'scan' });
    },
    connect(address) {
      setLink('CONNECTING', 'warn');
      ble.post({ op: 'connect', address });
      hide(el.scanModal);
    },
    send(line) {
      if (!S.connected) { toast('BLE 未连接', true); return; }
      ble.post({ op: 'send', line });
    }
  };

  window.__rflabNative = (raw) => {
    let msg = raw;
    if (typeof raw === 'string') {
      try { msg = JSON.parse(raw); } catch (e) { log('warn', 'native parse: ' + e.message); return; }
    }
    if (!msg || typeof msg !== 'object') return;
    switch (msg.op) {
      case 'line':
        if (msg.line) handleLine(msg.line);
        break;
      case 'scan':
        S.lastScan = Array.isArray(msg.devices) ? msg.devices : [];
        renderScan();
        break;
      case 'ble':
        if (msg.state === 'connected') {
          S.connected = true;
          setLink('BLE LINK', 'ok');
          log('sys', 'BLE 已连接 ' + (msg.name || msg.address || ''));
          showPin();
        } else if (msg.state === 'disconnected') {
          S.connected = false;
          S.authed = false;
          setLink('DISCONNECTED', 'bad');
          log('warn', 'BLE 已断开');
          ble.scan();
        }
        break;
      case 'error':
        toast(msg.message || 'BLE error', true);
        log('warn', msg.message || 'BLE error');
        break;
      default:
        break;
    }
  };

  function renderScan() {
    if (!el.scanList) return;
    el.scanList.textContent = '';
    if (!S.lastScan.length) {
      const d = document.createElement('div');
      d.className = 'empty';
      d.textContent = 'NO DEVICE // 确认设备已上电并靠近';
      el.scanList.append(d);
      return;
    }
    S.lastScan.forEach((d) => {
      const row = document.createElement('div');
      row.className = 'scan-item';
      const name = document.createElement('b');
      name.textContent = d.name || '(no name)';
      const meta = document.createElement('span');
      meta.textContent = (d.address || '') + (typeof d.rssi === 'number' ? '  ' + d.rssi + ' dBm' : '');
      row.append(name, meta);
      row.addEventListener('click', () => ble.connect(d.address));
      el.scanList.append(row);
    });
  }

  /* ---------------- protocol dispatch ---------------- */

  function safeParse(raw, fn) {
    try { fn(JSON.parse(raw)); }
    catch (e) { log('warn', 'JSON: ' + e.message); }
  }

  function handleLine(line) {
    const trimmed = String(line).trim();
    if (!trimmed) return;
    safeParse(trimmed, (m) => {
      switch (m.t) {
        case 'hello': onHello(m); break;
        case 'auth':
          S.authed = !!m.ok;
          if (m.ok) { hide(el.pinModal); text(el.pinMsg, ''); log('ok', 'BLE AUTH OK'); toast('BLE 已鉴权'); }
          else { text(el.pinMsg, 'PIN 错误，剩余 ' + (m.left == null ? '?' : m.left) + ' 次'); el.pinInput.select(); }
          break;
        case 'state': applyState(m.data); break;
        case 'codes': setCodes(m.items); break;
        case 'event': onEvent(m); break;
        case 'log':
          maybeFinishNew(m.line);
          log(m.line && /ERR|WARN/.test(m.line) ? 'warn' : 'sys', m.line);
          break;
        case 'error': toast(m.line || m.error || 'error', true); log('warn', m.line || m.error || 'error'); break;
        default: break;
      }
    });
  }

  function updateCodebookGate() {
    const ok = S.codebook >= 2;
    if (el.newCode) {
      el.newCode.disabled = !ok;
      el.newCode.title = ok ? '新建自定义码' : '设备固件需 0.7.1 才支持码库编辑';
    }
    if (el.codebookGate) el.codebookGate.hidden = ok;
  }

  function onHello(m) {
    S.fw = m.fw || '';
    S.build = m.build || '';
    S.codebook = Number(m.codebook) || 0;
    updateCodebookGate();
    text(el.fw, m.fw || '--');
    text(el.buildId, S.build || '--');
    text(el.uiId, UI_HASH);
    if (m.ap_ssid) text(el.net, m.ap_ssid);
    if (m.auth === 0) {
      S.authed = false;
      showPin();
    } else {
      S.authed = true;
      hide(el.pinModal);
    }
    text(el.footTail, 'RF-LAB ' + (m.fw || '') + (S.build ? '+' + S.build : '') +
      ' // ' + S.transport.toUpperCase() + ' // ' + (m.proto || 1));
  }

  function onEvent(ev) {
    if (!ev) return;
    if (ev.t === 'log' || ev.kind === 'log') {
      maybeFinishNew(ev.line);
      log(/ERR|WARN/.test(ev.line || '') ? 'warn' : 'sys', ev.line);
      return;
    }
    switch (ev.kind) {
      case 'learn':
        updateLast(Object.assign({}, ev, { kind: 'learn' }));
        log('evt', 'LEARN #' + ev.id + '  ' + ev.band + '  ' + ev.code);
        toast('学到 #' + ev.id + '  ' + ev.code);
        break;
      case 'rx':
        updateLast(Object.assign({}, ev, { kind: 'rx' }));
        log('evt', 'RX #' + ev.id + '  ' + ev.band + '  ' + ev.code);
        break;
      case 'full':
        log('warn', 'STORE FULL  ' + ev.band + '  ' + ev.code + ' (未保存)');
        toast('码库已满，未保存', true);
        break;
      case 'tx': {
        const active = ev.tx === 'long' && ev.ok === 1;
        setTxActive(active, ev.tx, ev.id);
        const label = String(ev.tx || 'tx').toUpperCase();
        log(ev.ok ? 'ok' : 'warn', label + ' #' + ev.id + '  ' + (ev.ok ? 'OK' : 'WARN') + '  ' + ev.ms + 'ms');
        toast(label + ' #' + ev.id + ' ' + (ev.ok ? 'OK' : 'WARN'));
        break;
      }
      case 'net':
        log('sys', ev.line || 'net');
        break;
      default:
        if (ev.line) log('sys', ev.line);
        break;
    }
  }

  /* ---------------- firmware OTA (HTTP only) ---------------- */

  function otaStatus(msg, warn) {
    text(el.otaStatus, msg);
    if (el.otaStatus) el.otaStatus.classList.toggle('warn', !!warn);
  }

  function otaPct(pct) {
    if (el.otaBar) el.otaBar.style.width = Math.max(0, Math.min(100, pct)) + '%';
  }

  function openOta() {
    show(el.otaModal);
    otaPct(0);
    const bleOnly = S.transport !== 'http';
    if (el.otaStart) el.otaStart.disabled = bleOnly;
    if (el.otaFile) el.otaFile.disabled = bleOnly;
    if (el.otaToken) el.otaToken.disabled = bleOnly;
    text(el.buildId, S.build || '--');
    text(el.uiId, UI_HASH);
    otaStatus(bleOnly
      ? 'APP // BLE 不支持上传;请用浏览器打开设备 IP,或 USB 烧录'
      : 'IDLE // 串口执行 OTA 取 token', bleOnly);
  }

  function startOta() {
    if (S.transport !== 'http') return;
    const file = el.otaFile && el.otaFile.files && el.otaFile.files[0];
    if (!file) { otaStatus('ERR // 先选择 firmware.bin', true); return; }
    const token = (el.otaToken.value || '').trim().toUpperCase();
    if (!/^[0-9A-F]{8}$/.test(token)) {
      otaStatus('ERR // token 是 8 位十六进制(串口 OTA)', true);
      return;
    }
    const before = S.build || '';
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota');
    xhr.timeout = 180000;
    xhr.setRequestHeader('X-RFLAB-OTA', token);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = (e) => {
      if (!e.lengthComputable) return;
      const pct = Math.round((e.loaded * 100) / e.total);
      otaPct(pct);
      otaStatus('UPLOAD ' + pct + '% // ' + Math.round(e.loaded / 1024) + ' KiB');
    };
    xhr.onload = () => {
      if (xhr.status === 200) {
        otaPct(100);
        otaStatus('OK // 设备重启中,等待新镜像…');
        log('ok', 'OTA 上传完成,等待重启');
        pollReboot(before);
      } else {
        let why = xhr.responseText || ('HTTP ' + xhr.status);
        try { why = JSON.parse(xhr.responseText).error || why; } catch (e) { /* text */ }
        otaStatus('ERR ' + why, true);
        log('warn', 'OTA ' + why);
      }
    };
    xhr.onerror = () => otaStatus('ERR // 网络中断(设备仍在旧镜像)', true);
    xhr.ontimeout = () => otaStatus('ERR // 超时(设备仍在旧镜像)', true);
    otaPct(0);
    otaStatus('UPLOAD 0% // ' + Math.round(file.size / 1024) + ' KiB');
    xhr.send(file);
  }

  function pollReboot(before) {
    let tries = 0;
    const timer = setInterval(async () => {
      tries++;
      try {
        const h = await httpJSON('/api/hello');
        const now = h.build || '';
        if (now && now !== before) {
          clearInterval(timer);
          otaStatus('OK // ' + h.fw + '+' + now + ' · 刷新页面');
          setTimeout(() => location.reload(), 900);
          return;
        }
      } catch (e) {
        otaStatus('REBOOT… // 等待设备 ' + tries + 's');
      }
      if (tries >= 30) {
        clearInterval(timer);
        otaStatus('WARN // 未检测到新 build,检查串口后手动刷新', true);
      }
    }, 1000);
  }

  /* ---------------- state ---------------- */

  function applyState(st) {
    if (!st || typeof st !== 'object') return;
    S.state = st;
    S.stateAt = performance.now();

    text(el.fw, st.fw);
    text(el.modBaud, st.module ? st.module.baud : '--');
    text(el.ble, st.ble ? (st.ble.on ? (st.ble.conn ? 'LINK' : 'ON') : 'OFF') : '--');
    text(el.oled, st.oled ? (st.oled.present ? (st.oled.on ? 'OK' : 'OFF') : 'N/A') : '--');

    if (st.net) {
      const mode = st.net.mode || 'ap';
      if (st.net.sta_connected) {
        text(el.net, 'LAN ' + st.net.sta_ip);
      } else if (mode === 'connecting') {
        text(el.net, 'WIFI…');
      } else {
        text(el.net, 'AP ' + st.net.ap_ip);
      }
      text(el.tUplink, st.net.sta_connected ? st.net.sta_ip : st.net.ap_ip);
      text(el.tRssi, st.net.sta_connected
        ? ('RSSI ' + st.net.rssi + ' dBm')
        : ('AP ' + (st.net.ap_clients || 0) + ' client'));

      let note = 'AP MODE // 输入 SSID 后接入局域网';
      let hot = false;
      if (mode === 'connecting') {
        note = 'WIFI CONNECTING // 最长 15s,失败自动回 AP';
        hot = true;
      } else if (mode === 'sta') {
        note = 'LAN MODE // AP 已关闭;用局域网 IP 访问';
      } else if (mode === 'apsta' && st.net.sta_connected) {
        note = 'LAN + AP // 局域网与 AP 都可访问';
      } else if (mode === 'apsta') {
        note = 'AP FALLBACK // WiFi 连接失败;AP 可直接用,也可重新配网';
        hot = true;
      }
      if (el.wifiNote) { el.wifiNote.textContent = note; el.wifiNote.classList.toggle('hot', hot); }
    }

    const store = st.store || {};
    text(el.tStore, (store.count == null ? '--' : store.count) + '/' + (store.max == null ? '--' : store.max));
    text(el.storeTag, (store.count == null ? 0 : store.count) + '/' + (store.max == null ? 32 : store.max));

    const mod = st.module || {};
    text(el.tFrames, mod.frames == null ? '--' : mod.frames);
    text(el.tAcks, mod.acks == null ? '--' : mod.acks);
    text(el.tStray, mod.stray == null ? '--' : mod.stray);
    text(el.tUptime, fmtUptime(st.uptime_ms));

    if (st.last && st.last.code) updateLast(st.last);
    if (st.tx) setTxActive(!!st.tx.active, st.tx.kind, st.tx.id);
    text(el.footTail, 'RF-LAB ' + (st.fw || '') + (S.build ? '+' + S.build : '') +
      ' // ' + S.transport.toUpperCase() + ' // ' + (st.net ? (st.net.sta_connected ? 'STA' : 'AP') : 'LOCAL'));
  }

  function updateLast(l) {
    if (!l) return;
    text(el.lastCode, l.code || '--------');
    text(el.lastBand, l.band ? (l.band + ' // ' + String(l.kind || '').toUpperCase()) : 'NO SIGNAL');
    text(el.lastId, l.id ? ('ID ' + l.id) : 'ID --');
  }

  function setTxActive(active, kind, id) {
    document.body.classList.toggle('txing', !!active);
    if (active && id) S.txId = id;
    if (el.stopBtn) el.stopBtn.disabled = !active;
    if (el.listenTag) {
      el.listenTag.textContent = active ? (kind === 'long' ? 'LONG TX' : 'TX ACTIVE') : 'LISTENING';
      el.listenTag.classList.toggle('hot', !!active);
    }
  }

  /* ---------------- codebook ---------------- */

  const utf8Len = (value) => {
    const str = String(value == null ? '' : value);
    try { return new TextEncoder().encode(str).length; }
    catch (e) { try { return unescape(encodeURIComponent(str)).length; } catch (e2) { return str.length; } }
  };
  const pad2 = (n) => String(n).padStart(2, '0');
  const codeValid = (hex) => /^[0-9A-F]{8}$/.test(String(hex || '').toUpperCase());
  const bandOf = (band) => (band === '433M' ? '433M' : '315M');
  const rowName = (c) => (c && c.name ? c.name : 'CODE ' + pad2(c ? c.id : 0));

  function visibleEntries() {
    const q = S.search.trim().toLowerCase();
    return S.codes.map((c, idx) => ({ c, idx })).filter(({ c }) => {
      const group = String(c.group || '').trim();
      if (S.filterGroup === '__none__') {
        if (group) return false;
      } else if (S.filterGroup && group !== S.filterGroup) {
        return false;
      }
      if (!q) return true;
      const hay = [c.name, c.code, c.band, c.group, c.id].map((v) => String(v == null ? '' : v).toLowerCase()).join(' ');
      return hay.includes(q);
    });
  }

  function renderGroupOptions() {
    if (!el.groupOptions) return;
    const groups = [...new Set(S.codes.map((c) => String(c.group || '').trim()).filter(Boolean))]
      .sort((a, b) => a.localeCompare(b, 'zh-Hans-CN'));
    el.groupOptions.textContent = '';
    groups.forEach((g) => {
      const o = document.createElement('option');
      o.value = g;
      el.groupOptions.append(o);
    });
  }

  function renderGroupChips() {
    if (!el.groupChips) return;
    const counts = new Map();
    for (const c of S.codes) {
      const g = String(c.group || '').trim();
      if (g) counts.set(g, (counts.get(g) || 0) + 1);
    }
    const groups = [...counts.keys()].sort((a, b) => a.localeCompare(b, 'zh-Hans-CN'));
    const noneCount = S.codes.filter((c) => !String(c.group || '').trim()).length;
    if (S.filterGroup && S.filterGroup !== '__none__' && !counts.has(S.filterGroup)) S.filterGroup = '';
    if (S.filterGroup === '__none__' && !noneCount) S.filterGroup = '';

    el.groupChips.textContent = '';
    const add = (value, label) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'gchip' + (S.filterGroup === value ? ' on' : '');
      b.textContent = label;
      b.setAttribute('aria-pressed', S.filterGroup === value ? 'true' : 'false');
      b.addEventListener('click', () => {
        S.filterGroup = value;
        renderGroupChips();
        renderCodes();
      });
      el.groupChips.append(b);
    };
    add('', `ALL ${S.codes.length}`);
    groups.forEach((g) => add(g, `${g} ${counts.get(g)}`));
    if (noneCount) add('__none__', `未分组 ${noneCount}`);
  }

  function setCodes(items) {
    if (!Array.isArray(items)) return;
    const keepId = S.selectAfterId || (S.codes[S.selected] && S.codes[S.selected].id);
    S.codes = items;
    let idx = keepId ? items.findIndex((c) => c.id === keepId) : -1;
    if (idx >= 0) {
      S.selected = idx;
      if (S.selectAfterId === keepId) S.selectAfterId = 0;
    } else {
      S.selected = Math.min(S.selected, Math.max(0, items.length - 1));
    }
    renderGroupChips();
    renderGroupOptions();
    renderCodes();
  }

  function renderCodes() {
    const list = el.codeList;
    if (!list) return;
    list.textContent = '';
    if (!S.codes.length) {
      const d = document.createElement('div');
      d.className = 'empty';
      d.innerHTML = 'NO CODES<br><span>按一下原遥控器开始学习</span>';
      list.append(d);
      return;
    }
    const entries = visibleEntries();
    if (!entries.length) {
      const d = document.createElement('div');
      d.className = 'empty';
      d.innerHTML = 'NO MATCH<br><span>换个分组或清空搜索</span>';
      list.append(d);
      return;
    }
    if (!entries.some((e) => e.idx === S.selected)) {
      S.selected = entries[0].idx;
    }

    entries.forEach(({ c, idx }) => {
      const row = document.createElement('div');
      row.className = 'code-row' + (idx === S.selected ? ' selected' : '');
      row.setAttribute('role', 'option');
      row.setAttribute('aria-selected', idx === S.selected ? 'true' : 'false');
      row.tabIndex = -1;
      row.title = '双击或按 E 编辑';

      const id = document.createElement('div');
      id.className = 'code-id';
      id.textContent = pad2(c.id);

      const band = document.createElement('div');
      band.className = 'band ' + (c.band === '433M' ? 'b433' : 'b315');
      band.textContent = c.band || '???';

      const main = document.createElement('div');
      main.className = 'code-main';
      const top = document.createElement('div');
      top.className = 'code-top';
      if (c.sym) {
        const sym = document.createElement('span');
        sym.className = 'code-sym';
        sym.textContent = c.sym;
        top.append(sym);
      }
      const name = document.createElement('span');
      name.className = 'code-name' + (c.name ? '' : ' unnamed');
      name.textContent = rowName(c);
      top.append(name);

      const sub = document.createElement('div');
      sub.className = 'code-sub';
      const hex = document.createElement('span');
      hex.className = 'code-hex';
      hex.textContent = c.code || '--------';
      sub.append(hex);
      if (c.group) {
        const group = document.createElement('span');
        group.className = 'code-group';
        group.textContent = c.group;
        sub.append(group);
      }
      main.append(top, sub);

      const acts = document.createElement('div');
      acts.className = 'code-actions';
      const mk = (cls, label, title, fn) => {
        const b = document.createElement('button');
        b.type = 'button';
        b.className = 'act ' + cls;
        b.textContent = label;
        b.title = title;
        b.setAttribute('aria-label', title + ' ' + rowName(c));
        b.addEventListener('click', (e) => { e.stopPropagation(); fn(b); });
        acts.append(b);
      };
      mk('go', '▶', 'SEND', () => tx('send', idx));
      mk('long', '∞', 'LONG', () => tx('long', idx));
      mk('stop', '■', 'STOP', () => stopTx(idx));
      mk('del', '×', 'ERASE', (b) => erase(idx, b));

      row.append(id, band, main, acts);
      row.addEventListener('click', () => select(idx));
      row.addEventListener('dblclick', () => openEditor(idx));
      list.append(row);
    });

    const sel = list.querySelector('.selected');
    if (sel) sel.scrollIntoView({ block: 'nearest' });
  }

  function select(idx) {
    if (idx < 0 || idx >= S.codes.length) return;
    S.selected = idx;
    renderCodes();
  }

  function move(delta) {
    const entries = visibleEntries();
    if (!entries.length) return;
    let pos = entries.findIndex((e) => e.idx === S.selected);
    if (pos < 0) pos = 0;
    pos = (pos + delta + entries.length) % entries.length;
    S.selected = entries[pos].idx;
    renderCodes();
  }

  function tx(kind, idx) {
    const c = S.codes[idx == null ? S.selected : idx];
    if (!c) { toast('码库为空', true); return; }
    if (!S.armed) { toast('先 ARM TRANSMIT', true); flashArm(); return; }
    sendCmd(kind.toUpperCase() + ' ' + c.id);
  }

  function stopTx(idx) {
    const c = S.codes[idx == null ? S.selected : idx];
    const id = S.txId || (c && c.id);
    if (!id) { toast('没有正在发射的码', true); return; }
    sendCmd('STOP ' + id);
  }

  function erase(idx, btn) {
    const c = S.codes[idx];
    if (!c) return;
    if (btn.dataset.confirm !== '1') {
      btn.dataset.confirm = '1';
      btn.textContent = 'SURE';
      btn.title = '再按一次删除';
      setTimeout(() => {
        btn.dataset.confirm = '';
        btn.textContent = '×';
        btn.title = 'ERASE';
      }, 2600);
      return;
    }
    sendCmd('ERASE ' + c.id);
  }

  /* ---------------- code editor ---------------- */

  const SYM_PRESETS = ['🚪', '🚗', '🔔', '💡', '⚡', '🔒', '🌡', '⌂', '📡', '🎛'];

  function renderSymPalette() {
    if (!el.symPalette) return;
    el.symPalette.textContent = '';
    SYM_PRESETS.forEach((sym) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'sym-btn';
      b.textContent = sym;
      b.title = '使用 ' + sym;
      b.addEventListener('click', () => {
        el.codeSym.value = sym;
        updateEditorMeta();
      });
      el.symPalette.append(b);
    });
  }

  function readDraft() {
    return {
      band: (S.editing && S.editing.draftBand) || '315M',
      code: String(el.codeHex.value || '').trim().toUpperCase(),
      name: String(el.codeName.value || '').trim(),
      group: String(el.codeGroup.value || '').trim(),
      sym: String(el.codeSym.value || '').trim(),
    };
  }

  function setDraftBand(band) {
    if (!S.editing) return;
    S.editing.draftBand = bandOf(band);
    document.querySelectorAll('#codeModal .band-btn').forEach((b) => {
      b.classList.toggle('on', b.dataset.band === S.editing.draftBand);
      b.setAttribute('aria-pressed', b.dataset.band === S.editing.draftBand ? 'true' : 'false');
    });
    updateEditorMeta();
  }

  function updateEditorMeta() {
    const d = readDraft();
    const counts = [
      [el.nameCount, utf8Len(d.name), 24],
      [el.symCount, utf8Len(d.sym), 8],
      [el.groupCount, utf8Len(d.group), 16],
    ];
    counts.forEach(([node, n, max]) => {
      if (!node) return;
      node.textContent = n + '/' + max + 'B';
      node.classList.toggle('over', n > max);
    });
    const hex = codeValid(d.code) ? d.code : '';
    const bytes = hex ? hex.match(/../g).join(' ') : '-- -- -- --';
    if (el.framePreview) {
      el.framePreview.textContent = 'FD ' + (d.band === '433M' ? 'F4' : 'F3') + ' 03 ' + bytes + ' DF';
    }
  }

  function validateDraft(d) {
    if (utf8Len(d.name) > 24) return 'NAME 超过 24 字节';
    if (utf8Len(d.sym) > 8) return 'SYMBOL 超过 8 字节';
    if (utf8Len(d.group) > 16) return 'GROUP 超过 16 字节';
    if (/[\u0000-\u001f\u007f]/.test(d.name + d.sym + d.group)) return '名称/分组/符号不能包含控制字符';
    if (!codeValid(d.code)) return 'RAW CODE 必须是 8 位十六进制';
    const self = S.editing && S.editing.idx;
    const dup = S.codes.find((c, i) => i !== self && c.band === d.band && String(c.code).toUpperCase() === d.code);
    if (dup) return '重复码:与 #' + dup.id + ' 的 ' + d.band + ' ' + d.code + ' 相同';
    return '';
  }

  function fillEditor(rec, isNew) {
    el.codeName.value = rec.name || '';
    el.codeSym.value = rec.sym || '';
    el.codeGroup.value = rec.group || '';
    el.codeHex.value = (rec.code || '').toUpperCase();
    S.editing.draftBand = bandOf(rec.band);
    document.querySelectorAll('#codeModal .band-btn').forEach((b) => {
      b.classList.toggle('on', b.dataset.band === S.editing.draftBand);
      b.setAttribute('aria-pressed', b.dataset.band === S.editing.draftBand ? 'true' : 'false');
    });
    el.codeDelete.hidden = !!isNew;
    el.codeTitle.textContent = isNew ? 'CODE // 新建' : 'CODE // 编辑 #' + rec.id;
    el.codeNote.textContent = isNew
      ? '手工建码:只对自有接收器测试;固定码可被复制,不要用于未知设备。'
      : (rec.manual ? '手工码:修改 band/code 会改变发射内容,保存后不可撤销。'
                    : '学习码:修改 band/code 会改变原始参数,保存后不可撤销;重复码会被拒绝。');
    if (el.codeError) el.codeError.textContent = '';
    updateEditorMeta();
  }

  function openEditor(idx) {
    if (S.codebook < 2) {
      toast('设备固件不支持码库编辑;先 OTA 0.7.1', true);
      return;
    }
    const isNew = idx == null;
    const storeMax = (S.state && S.state.store && S.state.store.max) || 128;
    if (isNew && S.codes.length >= storeMax) {
      toast('码库已满 ' + storeMax + ' 条', true);
      return;
    }
    const rec = isNew
      ? { band: '315M', code: '', name: '', group: (S.filterGroup && S.filterGroup !== '__none__' ? S.filterGroup : ''), sym: '' }
      : S.codes[idx];
    if (!rec) return;
    S.editing = {
      idx: isNew ? -1 : idx,
      isNew,
      original: {
        band: bandOf(rec.band), code: String(rec.code || '').toUpperCase(),
        name: rec.name || '', group: rec.group || '', sym: rec.sym || '',
      },
      draftBand: bandOf(rec.band),
    };
    fillEditor(rec, isNew);
    show(el.codeModal);
    setTimeout(() => { try { el.codeName.focus(); } catch (e) {} }, 40);
  }

  function closeEditor() {
    hide(el.codeModal);
    S.editing = null;
  }

  function resetEditor() {
    if (!S.editing) return;
    const src = S.editing.isNew
      ? { band: '315M', code: '', name: '', group: '', sym: '' }
      : S.editing.original;
    fillEditor(src, S.editing.isNew);
  }

  function saveEditor(event) {
    event.preventDefault();
    if (!S.editing) return;
    const d = readDraft();
    const err = validateDraft(d);
    if (err) {
      if (el.codeError) el.codeError.textContent = err;
      return;
    }
    if (S.editing.isNew) {
      S.pendingNew = { band: d.band, code: d.code, name: d.name, group: d.group, sym: d.sym };
      sendCmd('NEW ' + d.band + ' ' + d.code + (d.name ? ' ' + d.name : ''));
      closeEditor();
      toast('正在新建 #…');
      return;
    }
    const id = S.codes[S.editing.idx].id;
    const o = S.editing.original;
    if ((d.band !== o.band || d.code !== o.code) && !S.codes[S.editing.idx].manual) {
      const ok = window.confirm('这是学习码：修改 band/code 会改变原始参数，保存后不可撤销。继续？');
      if (!ok) return;
    }
    if (d.band !== o.band) sendCmd('EDIT ' + id + ' band ' + d.band);
    if (d.code !== o.code) sendCmd('EDIT ' + id + ' code ' + d.code);
    if (d.name !== o.name) sendCmd('NAME ' + id + (d.name ? ' ' + d.name : ''));
    if (d.group !== o.group) sendCmd('GROUP ' + id + (d.group ? ' ' + d.group : ''));
    if (d.sym !== o.sym) sendCmd('SYM ' + id + (d.sym ? ' ' + d.sym : ''));
    closeEditor();
    toast('已提交 #' + id);
  }

  function maybeFinishNew(line) {
    if (!S.pendingNew) return;
    const text = String(line || '');
    const ok = /^OK new id=(\d+)/.exec(text);
    if (ok) {
      const id = Number(ok[1]);
      const pending = S.pendingNew;
      S.pendingNew = null;
      S.selectAfterId = id;
      const now = S.codes.findIndex((c) => c.id === id);
      if (now >= 0) { S.selected = now; S.selectAfterId = 0; renderCodes(); }
      if (pending.group) sendCmd('GROUP ' + id + ' ' + pending.group);
      if (pending.sym) sendCmd('SYM ' + id + ' ' + pending.sym);
      return;
    }
    if (/^ERR new\b/.test(text)) {
      S.pendingNew = null;
      toast(text, true);
    }
  }

  function deleteEditing() {
    if (!S.editing || S.editing.isNew) return;
    const id = S.codes[S.editing.idx].id;
    if (!window.confirm('删除 #' + id + '? 后续 ID 会前移。')) return;
    closeEditor();
    sendCmd('ERASE ' + id);
  }

  /* ---------------- commands ---------------- */

  /* 单命令出口:HTTP 逐条等 202,避免编辑器一次发 5 条时把 5 个 socket 打满;
   * BLE 逐条节流,固件命令队列只有 8 格。503 自动重试一次。 */
  const outbox = [];
  let outboxBusy = false;

  function pumpOutbox() {
    if (outboxBusy || !outbox.length) return;
    const item = outbox.shift();
    outboxBusy = true;
    const line = item.line;
    log('cmd', '> ' + line);

    const finish = () => {
      outboxBusy = false;
      setTimeout(pumpOutbox, 60);
    };

    if (S.transport === 'ble') {
      if (!S.connected) {
        toast('BLE 未连接', true);
        finish();
        return;
      }
      ble.send(line);
      setTimeout(finish, 70);
      return;
    }

    const post = (tries) => {
      httpJSON('/api/cmd', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd: line })
      }).then(finish).catch((e) => {
        if (tries > 0 && /HTTP 503/.test(e.message)) {
          setTimeout(() => post(tries - 1), 260);
          return;
        }
        toast('命令失败: ' + e.message, true);
        log('warn', 'ERR ' + e.message);
        finish();
      });
    };
    post(1);
  }

  function sendCmd(line) {
    outbox.push({ line });
    pumpOutbox();
  }

  function setArmed(on) {
    S.armed = !!on;
    document.body.classList.toggle('armed', S.armed);
    text(el.armLabel, S.armed ? 'ARMED' : 'DISARMED');
    if (el.armChip) el.armChip.setAttribute('aria-pressed', S.armed ? 'true' : 'false');
    if (el.armChip) el.armChip.classList.toggle('armed', S.armed);
    if (el.armDot) el.armDot.className = 'dot ' + (S.armed ? 'warn' : '');
    text(el.armBtn, S.armed ? 'DISARM' : 'ARM TRANSMIT');
    text(el.heroHint, S.armed ? '已 ARM：Enter 发射 / L 长发 / S 停止。仅防误触。' : '按一下原遥控器 → 自动入库。发射前需 ARM。');
  }

  function toggleArm() {
    if (S.armed && S.state && S.state.tx && S.state.tx.active) stopTx();
    setArmed(!S.armed);
    if (S.armed) toast('ARMED — 发射已解锁');
  }

  function flashArm() {
    if (!el.armBtn) return;
    el.armBtn.animate(
      [{ transform: 'translateX(0)' }, { transform: 'translateX(-4px)' }, { transform: 'translateX(4px)' }, { transform: 'translateX(0)' }],
      { duration: 220 }
    );
  }

  function refresh() {
    if (S.transport === 'http') {
      httpRefresh().then((ok) => { if (ok) toast('状态已刷新'); });
    } else {
      toast('BLE 为推送模式，无需刷新');
    }
  }

  /* ---------------- modals ---------------- */

  function show(m) { if (m) m.hidden = false; }
  function hide(m) { if (m) m.hidden = true; }
  function closeAll() {
    hide(el.helpModal); hide(el.scanModal); hide(el.hwModal); hide(el.otaModal); hide(el.codeModal);
    S.editing = null;
    const hb = el.hwModal && el.hwModal.querySelector('.hw-modal-body');
    if (hb) hb.classList.remove('is-zoom');
    const hz = $('hwZoom');
    if (hz) { hz.textContent = 'ZOOM'; hz.setAttribute('aria-pressed', 'false'); }
    if (S.authed) hide(el.pinModal);
  }
  function showPin() {
    if (!hasBridge) return;
    show(el.pinModal);
    setTimeout(() => { try { el.pinInput.focus(); } catch (e) {} }, 40);
  }

  document.addEventListener('click', (e) => {
    const t = e.target;
    if (t && t.matches('[data-close]')) closeAll();
    if (t && t.classList && t.classList.contains('modal')) {
      if (t.id === 'pinModal' && !S.authed) return;
      closeAll();
    }
  });

  const helpClose = el.helpModal.querySelector('[data-close]');
  if (helpClose) helpClose.addEventListener('click', closeAll);
  const hwChip = $('hwChip');
  if (hwChip) hwChip.addEventListener('click', () => show(el.hwModal));
  const hwZoom = $('hwZoom');
  if (hwZoom) hwZoom.addEventListener('click', () => {
    const body = el.hwModal && el.hwModal.querySelector('.hw-modal-body');
    if (!body) return;
    const on = body.classList.toggle('is-zoom');
    hwZoom.textContent = on ? 'FIT' : 'ZOOM';
    hwZoom.setAttribute('aria-pressed', String(on));
  });
  const rescan = el.scanModal.querySelector('#rescan');
  if (rescan) rescan.addEventListener('click', () => ble.scan());

  const otaOpen = $('otaOpen');
  if (otaOpen) otaOpen.addEventListener('click', openOta);
  if (el.otaStart) el.otaStart.addEventListener('click', startOta);
  if (el.otaFile) el.otaFile.addEventListener('change', () => {
    const f = el.otaFile.files && el.otaFile.files[0];
    if (f) otaStatus('READY // ' + f.name + ' · ' + Math.round(f.size / 1024) + ' KiB');
  });

  $('pinForm').addEventListener('submit', (e) => {
    e.preventDefault();
    const pin = (el.pinInput.value || '').trim();
    if (!/^\d{4}$/.test(pin)) { text(el.pinMsg, 'PIN 必须是 4 位数字'); return; }
    text(el.pinMsg, 'AUTH…');
    ble.post({ op: 'send', line: 'AUTH ' + pin });
  });

  /* ---------------- UI events ---------------- */

  el.armBtn.addEventListener('click', toggleArm);
  el.armChip.addEventListener('click', toggleArm);
  el.stopBtn.addEventListener('click', () => stopTx());
  $('clearLog').addEventListener('click', () => { el.log.textContent = ''; S.logCount = 0; });

  document.querySelectorAll('[data-cmd]').forEach((b) => {
    b.addEventListener('click', () => sendCmd(b.dataset.cmd));
  });

  if (el.newCode) el.newCode.addEventListener('click', () => openEditor(null));
  if (el.codeSearch) el.codeSearch.addEventListener('input', () => {
    S.search = el.codeSearch.value || '';
    renderCodes();
  });
  if (el.codeForm) el.codeForm.addEventListener('submit', saveEditor);
  if (el.codeDelete) el.codeDelete.addEventListener('click', deleteEditing);
  if (el.codeReset) el.codeReset.addEventListener('click', resetEditor);
  document.querySelectorAll('#codeModal .band-btn').forEach((b) => {
    b.addEventListener('click', () => setDraftBand(b.dataset.band));
  });
  if (el.codeHex) el.codeHex.addEventListener('input', () => {
    const clean = el.codeHex.value.replace(/[^0-9a-fA-F]/g, '').slice(0, 8).toUpperCase();
    if (clean !== el.codeHex.value) el.codeHex.value = clean;
    updateEditorMeta();
  });
  [el.codeName, el.codeSym, el.codeGroup].forEach((input) => {
    if (input) input.addEventListener('input', updateEditorMeta);
  });
  if (el.codeModal) el.codeModal.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') { e.stopPropagation(); closeAll(); }
  });
  renderSymPalette();
  renderGroupChips();

  el.codeList.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowDown' || e.key === 'j') { e.preventDefault(); e.stopPropagation(); move(1); }
    else if (e.key === 'ArrowUp' || e.key === 'k') { e.preventDefault(); e.stopPropagation(); move(-1); }
    else if (e.key === 'Enter') { e.preventDefault(); e.stopPropagation(); tx('send'); }
  });

  el.wifiClear.addEventListener('click', () => {
    if (S.transport === 'ble') {
      sendCmd('WIFI CLEAR');
    } else {
      httpJSON('/api/wifi/clear', { method: 'POST' })
        .then(() => { toast('已清除 STA,回到 AP 模式'); log('sys', 'WIFI CLEAR'); })
        .catch((err) => toast('清除失败: ' + err.message, true));
    }
  });

  el.wifiForm.addEventListener('submit', (e) => {
    e.preventDefault();
    const ssid = $('wifiSsid').value.trim();
    const pass = $('wifiPass').value;
    if (!ssid) { toast('SSID 不能为空', true); return; }
    if (S.transport === 'ble') {
      if (/\s/.test(ssid)) { toast('BLE 配网暂不支持含空格的 SSID', true); return; }
      sendCmd('WIFI ' + ssid + ' ' + pass);
    } else {
      httpJSON('/api/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid, pass })
      }).then(() => { toast('正在连接 ' + ssid + '（AP 保持）'); log('sys', 'WIFI ' + ssid + ' connecting'); })
        .catch((err) => toast('配网失败: ' + err.message, true));
    }
    $('wifiPass').value = '';
  });

  document.addEventListener('keydown', (e) => {
    const tag = (e.target && e.target.tagName) || '';
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;
    if (!el.helpModal.hidden || !el.pinModal.hidden || !el.scanModal.hidden ||
        !el.hwModal.hidden || !el.otaModal.hidden || !el.codeModal.hidden) {
      if (e.key === 'Escape') closeAll();
      return;
    }
    switch (e.key) {
      case 'ArrowDown': case 'j': e.preventDefault(); move(1); break;
      case 'ArrowUp': case 'k': e.preventDefault(); move(-1); break;
      case 'Enter': e.preventDefault(); tx('send'); break;
      case 'l': case 'L': tx('long'); break;
      case 's': case 'S': stopTx(); break;
      case 'd': case 'D': {
        const row = el.codeList.querySelector('.selected');
        const btn = row && row.querySelector('.act.del');
        if (btn) erase(S.selected, btn);
        break;
      }
      case 'r': case 'R': refresh(); break;
      case 't': case 'T': sendCmd('DISP TEST 30'); break;
      case 'a': case 'A': toggleArm(); break;
      case 'h': case 'H': show(el.hwModal); break;
      case 'u': case 'U': openOta(); break;
      case 'e': case 'E': openEditor(S.codes.length ? S.selected : null); break;
      case 'n': case 'N': openEditor(null); break;
      case '/': e.preventDefault(); if (el.codeSearch) el.codeSearch.focus(); break;
      case '?': show(el.helpModal); break;
      case 'Escape': closeAll(); break;
      default:
        if (/^[1-9]$/.test(e.key)) select(Math.min(Number(e.key) - 1, S.codes.length - 1));
        break;
    }
  });

  window.addEventListener('beforeunload', () => { if (S.es) S.es.close(); });

  /* ---------------- boot ---------------- */

  // Decorative RF spectrum rail: generated locally, no data path.
  (() => {
    const rail = $('railBars');
    if (!rail) return;
    for (let i = 0; i < 96; i++) {
      const bar = document.createElement('i');
      bar.style.setProperty('--h', (18 + ((i * 37 + 11) % 74)) + '%');
      bar.style.setProperty('--d', ((i % 11) * -0.19).toFixed(2) + 's');
      rail.append(bar);
    }
  })();

  setArmed(false);
  text(el.uiId, UI_HASH);
  log('sys', 'RF-LAB FIELD TERMINAL // ' + S.transport.toUpperCase() + ' // UI ' + UI_HASH);
  if (S.transport === 'ble') {
    setLink('BLE SCAN', 'warn');
    ble.scan();
  } else {
    setLink('CONNECTING', 'warn');
    httpConnect();
  }
})();
