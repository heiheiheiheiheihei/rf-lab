/* RF-LAB terminal demo transport.
 *
 * Loaded by site/assets/terminal.html BEFORE the real single-page app script.
 * It replaces window.fetch / window.EventSource with an in-page state machine,
 * so the real UI runs unchanged with zero network requests. Nothing here is
 * embedded in the firmware; the command timings below are replay fixtures, not
 * a second RF implementation.
 */
(() => {
  "use strict";

  const PRIMARY = [
    { id: 1, band: "315M", code: "5555034C" },
    { id: 2, band: "315M", code: "5555033C" },
    { id: 3, band: "315M", code: "5555034B" },
    { id: 4, band: "433M", code: "A1B2C3D4" },
    { id: 5, band: "433M", code: "5555033C" },
  ];
  const LEARN_POOL = [
    ["315M", "5555031A"],
    ["433M", "B4C5D6E7"],
    ["315M", "5555034D"],
    ["433M", "0F1E2D3C"],
  ];
  const HELLO = { t: "hello", fw: "0.6.0", proto: 1, name: "RF-Lab-1B59", auth: 1 };

  const INITIAL_STATE = {
    fw: "0.6.0", proto: 1, uptime_ms: 3725000,
    net: {
      mode: "sta", ap_on: 0, has_creds: 1,
      ap_ssid: "RF-Lab-1B59", ap_ip: "192.168.4.1",
      sta_connected: true, sta_ssid: "SSID-A",
      sta_ip: "192.168.5.117", rssi: -57,
    },
    ble: { on: 1, conn: 0, name: "RF-Lab-1B59", pin_set: 1 },
    module: { baud: 9600, rx_bytes: 296, frames: 37, acks: 15, dups: 3, stray: 0, resync: 0 },
    store: { count: 5, max: 32 },
    oled: { present: 1, on: 1, flush: 43, err: 0, max_us: 8747 },
    tx: { active: 0, kind: "idle", id: 0, ok: 0, ms: 0 },
    last: { kind: "rx", id: 2, band: "315M", code: "5555033C", ms: 123 },
  };

  const clone = (value) => JSON.parse(JSON.stringify(value));
  const state = clone(INITIAL_STATE);
  let codes = clone(PRIMARY);
  let nextId = 6;
  let seq = 1;
  let activeSSE = null;
  let booted = false;
  let epoch = 0;
  const queued = [];

  const parentWindow = () => {
    try { return window.parent && window.parent !== window ? window.parent : null; } catch { return null; }
  };

  function postParent(message) {
    const parent = parentWindow();
    if (!parent) return;
    try {
      parent.postMessage(Object.assign({ source: "rflab-demo" }, message), "*");
    } catch {
      /* a blocked parent must not break the terminal */
    }
  }

  function syncStore() {
    state.store.count = codes.length;
    state.last = state.last || null;
  }

  function dispatch(name, payload) {
    if (!activeSSE) return;
    activeSSE.dispatch(name, payload);
  }

  function emitEvent(kind, extra) {
    const event = Object.assign({ t: "event", seq: seq++ , kind }, extra);
    dispatch("event", event);
    postParent({
      type: "event",
      kind,
      ok: extra.ok,
      line: extra.line || "",
    });
  }

  function emitLog(line) {
    emitEvent("log", { line });
  }

  function emitSnapshot(which) {
    if (which === "codes") {
      dispatch("codes", { t: "codes", seq: seq++, items: clone(codes) });
    } else {
      dispatch("state", { t: "state", seq: seq++, data: clone(state) });
    }
    postParent({ type: "state", store: codes.length, online: true });
  }

  function codeOf(id) {
    return codes.find((item) => item.id === id) || { id, band: "315M", code: "--------" };
  }

  function txEvent(kind, id, ms, ok = 1) {
    const code = codeOf(id);
    const line = kind === "stop"
      ? `OK tx id=${id} kind=stop band=${code.band} code=${code.code} ok=1 ms=${ms}`
      : `OK tx id=${id} kind=${kind} band=${code.band} code=${code.code} ok=${ok} ms=${ms}`;
    emitEvent("tx", { tx: kind, id, band: code.band, code: code.code, ok, ms, line });
  }

  function learn() {
    if (codes.length >= 32) {
      emitEvent("full", { band: "315M", code: "--------", line: "STORE FULL // 未保存" });
      return;
    }
    const [band, code] = LEARN_POOL[(nextId - 6) % LEARN_POOL.length];
    const id = nextId++;
    codes.push({ id, band, code });
    syncStore();
    state.module.frames += 1;
    state.module.acks += 1;
    state.module.rx_bytes += 8;
    state.last = { kind: "learn", id, band, code, ms: 0 };
    emitEvent("learn", { id, band, code, line: `LEARN id=${id} band=${band} code=${code}` });
    emitSnapshot("codes");
    emitSnapshot("state");
  }

  function erase(id) {
    const at = codes.findIndex((item) => item.id === id);
    if (at < 0) {
      emitLog(`ERR erase id=${id} not found`);
      return;
    }
    codes.splice(at, 1);
    syncStore();
    emitLog(`OK erase id=${id}`);
    emitSnapshot("codes");
    emitSnapshot("state");
  }

  function reset() {
    epoch += 1;
    codes = clone(PRIMARY);
    nextId = 6;
    Object.assign(state, clone(INITIAL_STATE));
    syncStore();
    const clear = document.getElementById("clearLog");
    if (clear) clear.click();
    dispatch("hello", HELLO);
    emitSnapshot("codes");
    emitSnapshot("state");
    emitLog("SIM RESET // 5 codes / 本地模拟");
  }

  function handleCommand(raw) {
    const cmd = String(raw || "").trim();
    if (!cmd) return;
    const parts = cmd.split(/\s+/);
    const verb = parts[0].toUpperCase();
    const id = Number(parts[1]);

    const at = epoch;
    switch (verb) {
      case "SEND": {
        setTimeout(() => {
          if (at !== epoch) return;
          state.module.acks += 1;
          const code = codeOf(id);
          state.last = { kind: "tx", id, band: code.band, code: code.code, ms: 852 };
          txEvent("send", id, 852);
          emitSnapshot("state");
        }, 760);
        break;
      }
      case "LONG":
        setTimeout(() => {
          if (at !== epoch) return;
          state.tx = { active: 1, kind: "long", id, ok: 1, ms: 0 };
          state.module.acks += 1;
          txEvent("long", id, 0);
        }, 160);
        break;
      case "STOP":
        setTimeout(() => {
          if (at !== epoch) return;
          state.tx = { active: 0, kind: "idle", id: 0, ok: 0, ms: 0 };
          txEvent("stop", id, 51);
          emitSnapshot("state");
        }, 140);
        break;
      case "ERASE":
        erase(id);
        break;
      case "MODE":
        emitLog(`OK mode=${(parts[1] || "continuous").toLowerCase()}`);
        break;
      case "BAUD":
        state.module.baud = Number(parts[1]) || 9600;
        emitLog(`OK baud=${state.module.baud}`);
        emitSnapshot("state");
        break;
      case "DISP":
        emitLog(`OK disp test ${(parts[2] || "30")}s // 图案仅在真机 OLED 上显示`);
        break;
      case "WIFI":
        emitLog(`OK wifi ${parts.slice(1).join(" ") || "clear"} // demo 不改变网络`);
        break;
      default:
        emitLog(`OK ${cmd}`);
    }
  }

  const cloneReply = (obj, status = 200) => ({
    ok: status >= 200 && status < 300,
    status,
    json: () => Promise.resolve(clone(obj)),
    text: () => Promise.resolve(JSON.stringify(obj)),
  });

  function demoFetch(input, init) {
    const request = init || {};
    const rawUrl = typeof input === "string" ? input : (input && input.url) || "";
    const path = rawUrl.replace(/^[a-z]+:\/\/[^/]+/i, "").split("?")[0];
    const method = String(request.method || "GET").toUpperCase();
    let body = {};
    if (request.body) {
      try { body = JSON.parse(String(request.body)); } catch { body = { cmd: String(request.body) }; }
    }

    if (method === "POST") {
      if (path === "/api/cmd") {
        const cmd = body.cmd || body.line || "";
        setTimeout(() => handleCommand(cmd), 60);
        return Promise.resolve(cloneReply({ ok: true, queued: true }, 202));
      }
      if (path === "/api/wifi") {
        setTimeout(() => emitLog(`OK wifi ssid=${body.ssid || "?"} // demo 保持 AP/LAN 状态`), 240);
        return Promise.resolve(cloneReply({ ok: true, queued: true }, 202));
      }
      if (path === "/api/wifi/clear") {
        setTimeout(() => emitLog("OK wifi clear // demo 不删除真实凭据"), 200);
        return Promise.resolve(cloneReply({ ok: true, queued: true }, 202));
      }
      return Promise.resolve(cloneReply({ error: "not found" }, 404));
    }

    switch (path) {
      case "/api/hello":
        return Promise.resolve(cloneReply(HELLO));
      case "/api/state":
        return Promise.resolve(cloneReply(state));
      case "/api/codes":
        return Promise.resolve(cloneReply(codes));
      case "/api/net":
        return Promise.resolve(cloneReply(state.net));
      default:
        return Promise.resolve(cloneReply({ error: "not found" }, 404));
    }
  }

  class DemoEventSource {
    constructor(url) {
      this.url = url;
      this.onopen = null;
      this.onerror = null;
      this.handlers = Object.create(null);
      this.closed = false;
      if (activeSSE) activeSSE.close();
      activeSSE = this;
      window.setTimeout(() => this.begin(), 160);
    }

    addEventListener(name, fn) {
      (this.handlers[name] = this.handlers[name] || []).push(fn);
    }

    dispatch(name, payload) {
      if (this.closed) return;
      const event = { type: name, data: JSON.stringify(payload) };
      (this.handlers[name] || []).forEach((fn) => {
        try { fn(event); } catch { /* the real app owns error reporting */ }
      });
    }

    begin() {
      if (this.closed) return;
      if (typeof this.onopen === "function") this.onopen({});
      this.dispatch("hello", HELLO);
      emitSnapshot("state");
      emitSnapshot("codes");
      booted = true;
      postParent({ type: "ready", store: codes.length, online: true });
      while (queued.length) {
        const op = queued.shift();
        if (op === "learn") learn();
        else if (op === "reset") reset();
        else if (op === "hello") postParent({ type: "ready", store: codes.length, online: true });
      }
      // One passive RX to make the first screen feel alive; it does not store a code.
      const bootAt = epoch;
      window.setTimeout(() => {
        if (this.closed || document.hidden || bootAt !== epoch) return;
        state.module.frames += 1;
        state.module.rx_bytes += 8;
        state.last = { kind: "rx", id: 2, band: "315M", code: "5555033C", ms: 123 };
        emitEvent("rx", { id: 2, band: "315M", code: "5555033C", line: "RX id=2 band=315M code=5555033C" });
        emitSnapshot("state");
      }, 2800);
    }

    close() {
      this.closed = true;
      if (activeSSE === this) activeSSE = null;
    }
  }

  try {
    Object.defineProperty(window, "fetch", { value: demoFetch, writable: true, configurable: true });
  } catch {
    window.fetch = demoFetch;
  }
  try {
    Object.defineProperty(window, "EventSource", { value: DemoEventSource, writable: true, configurable: true });
  } catch {
    window.EventSource = DemoEventSource;
  }

  window.addEventListener("message", (event) => {
    const data = event.data;
    if (!data || data.source !== "rflab-site") return;
    if (!booted) { queued.push(data.op); return; }
    if (data.op === "learn") learn();
    else if (data.op === "reset") reset();
    else if (data.op === "hello") {
      postParent({ type: "ready", store: codes.length, online: true });
      emitSnapshot("state");
    }
  });

  /* Standalone (non-iframe) demo gets a small control bar; the embedded one is
     driven by the website so the UI itself stays untouched. */
  function mountStandaloneBar() {
    if (window.top !== window.self) return;
    const style = document.createElement("style");
    style.textContent = [
      ".rflab-demo-bar{position:fixed;left:50%;bottom:0;transform:translateX(-50%);z-index:9999;",
      "display:flex;align-items:center;gap:8px;flex-wrap:wrap;justify-content:center;",
      "padding:8px 12px;background:rgba(7,9,6,.96);border:1px solid #ffb000;border-bottom:0;",
      "font-family:'JetBrains Mono',Consolas,monospace;font-size:9px;letter-spacing:.6px;color:#b4ae96;max-width:100vw}",
      ".rflab-demo-bar b{color:#9bf05c;font-weight:500}",
      ".rflab-demo-bar button{appearance:none;border:1px solid #46522f;background:transparent;color:#ece6d4;",
      "font:inherit;padding:5px 8px;cursor:pointer}",
      ".rflab-demo-bar button:hover{border-color:#ffb000;color:#ffd24a}",
      ".rflab-demo-bar a{color:#ffb000;text-decoration:none;border-bottom:1px solid rgba(255,176,0,.4)}",
    ].join("");
    document.head.appendChild(style);
    const bar = document.createElement("div");
    bar.className = "rflab-demo-bar";
    bar.innerHTML = "<b>SIM</b><span>本地模拟 · 无网络</span>" +
      "<button type=\"button\" data-demo=\"learn\">模拟遥控器 LEARN</button>" +
      "<button type=\"button\" data-demo=\"reset\">RESET</button>" +
      "<a href=\"../index.html#terminal\">返回官网</a>";
    bar.addEventListener("click", (event) => {
      const button = event.target.closest("[data-demo]");
      if (!button) return;
      if (button.dataset.demo === "learn") learn();
      else reset();
    });
    document.body.appendChild(bar);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", mountStandaloneBar);
  } else {
    mountStandaloneBar();
  }
})();
