/* RF-LAB // 百元射频实验室 — microsite interactions.
 * No dependencies, no network calls. Everything below degrades gracefully:
 * the page remains readable without JS; the configurator is the enhancement.
 */
(() => {
  "use strict";

  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));
  const staticMode = new URLSearchParams(window.location.search).has("static");
  if (staticMode) document.documentElement.classList.add("static");
  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches || staticMode;
  const BASE_PRICE = 79;
  const BASE_NAME = "CORE-C3";

  const PRESETS = {
    scout: {
      name: "侦察兵", en: "SCOUT", budget: 100,
      ids: ["ys"],
    },
    assault: {
      name: "突击手", en: "ASSAULT", budget: 299,
      ids: ["ys", "cc1101", "nrf", "backplane", "antenna"],
    },
    tactical: {
      name: "战术组", en: "TACTICAL", budget: 599,
      ids: ["ys", "cc1101", "nrf", "si4463", "lr1121", "backplane", "antenna", "case"],
    },
    allin: {
      name: "全频段", en: "ALL-IN", budget: 999,
      ids: ["ys", "cc1101", "nrf", "si4463", "lr1121", "backplane", "antenna", "case", "dock"],
    },
  };

  /* ---------------------------------------------------------------- toast */
  const toastEl = $("#toast");
  let toastTimer = 0;
  function toast(message, warn = false) {
    if (!toastEl) return;
    toastEl.textContent = message;
    toastEl.classList.toggle("warn", warn);
    toastEl.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = window.setTimeout(() => {
      toastEl.hidden = true;
    }, 2800);
  }

  /* ------------------------------------------------------------- scroll ui */
  const progress = $("#scrollProgress");
  let scrollTicking = false;
  function paintProgress() {
    scrollTicking = false;
    if (!progress) return;
    const doc = document.documentElement;
    const max = doc.scrollHeight - window.innerHeight;
    const ratio = max > 0 ? Math.min(1, Math.max(0, window.scrollY / max)) : 0;
    progress.style.width = `${(ratio * 100).toFixed(2)}%`;
  }
  window.addEventListener("scroll", () => {
    if (!scrollTicking) {
      scrollTicking = true;
      requestAnimationFrame(paintProgress);
    }
  }, { passive: true });
  paintProgress();

  /* --------------------------------------------------------------- reveal */
  const reveals = $$(".reveal");
  if (reduceMotion || !("IntersectionObserver" in window)) {
    reveals.forEach((el) => el.classList.add("in"));
  } else {
    const revealObserver = new IntersectionObserver((entries, obs) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add("in");
          obs.unobserve(entry.target);
        }
      });
    }, { threshold: 0.12, rootMargin: "0px 0px -6% 0px" });
    reveals.forEach((el) => revealObserver.observe(el));
    // If JS throws after this point, content must not stay invisible.
    window.setTimeout(() => reveals.forEach((el) => el.classList.add("in")), 3200);
  }

  /* --------------------------------------------------------------- nav active */
  const navLinks = $$(".nav-links a");
  const sections = ["budget", "modules", "terminal", "uplink", "status"]
    .map((id) => document.getElementById(id))
    .filter(Boolean);
  if ("IntersectionObserver" in window && navLinks.length) {
    const navObserver = new IntersectionObserver((entries) => {
      entries.forEach((entry) => {
        if (!entry.isIntersecting) return;
        navLinks.forEach((a) => a.classList.toggle("active", a.getAttribute("href") === `#${entry.target.id}`));
      });
    }, { rootMargin: "-32% 0px -58% 0px", threshold: 0 });
    sections.forEach((section) => navObserver.observe(section));
  }

  /* ------------------------------------------------------------ ambient glow */
  const ambient = $("#ambient");
  if (ambient && window.matchMedia("(hover: hover)").matches && !reduceMotion) {
    let mx = 50, my = 40, pointerTicking = false;
    window.addEventListener("pointermove", (event) => {
      mx = (event.clientX / window.innerWidth) * 100;
      my = (event.clientY / window.innerHeight) * 100;
      if (!pointerTicking) {
        pointerTicking = true;
        requestAnimationFrame(() => {
          pointerTicking = false;
          ambient.style.setProperty("--mx", `${mx}%`);
          ambient.style.setProperty("--my", `${my}%`);
        });
      }
    }, { passive: true });
  }

  /* ------------------------------------------------------------- spectrum */
  const spectrum = $("#spectrum");
  if (spectrum && spectrum.getContext) {
    const ctx = spectrum.getContext("2d");
    const bands = [
      { f: 315, label: "315", amp: 0.55, color: "rgba(255,176,0," },
      { f: 433, label: "433", amp: 0.82, color: "rgba(255,176,0," },
      { f: 868, label: "868", amp: 0.33, color: "rgba(155,240,92," },
      { f: 915, label: "915", amp: 0.26, color: "rgba(155,240,92," },
      { f: 2400, label: "2.4G", amp: 0.62, color: "rgba(255,176,0," },
    ];
    let W = 0, H = 0, DPR = 1, raf = 0, last = 0, visible = true, inited = false;

    function resize() {
      if (!spectrum.clientWidth) return;
      DPR = Math.min(window.devicePixelRatio || 1, 2);
      W = spectrum.clientWidth;
      H = spectrum.clientHeight;
      spectrum.width = Math.max(1, Math.round(W * DPR));
      spectrum.height = Math.max(1, Math.round(H * DPR));
      ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
      inited = true;
    }

    const xOf = (f) => Math.log(f / 150) / Math.log(2500 / 150) * W;

    function draw(t) {
      if (!inited || !W || !H) return;
      ctx.clearRect(0, 0, W, H);

      const base = H * 0.79;
      const maxH = Math.min(H * 0.28, 210);
      const gridY = [0.22, 0.42, 0.62, 0.79];

      // horizontal grid + axis labels
      ctx.lineWidth = 1;
      gridY.forEach((gy, i) => {
        ctx.strokeStyle = i === gridY.length - 1 ? "rgba(255,176,0,.20)" : "rgba(255,176,0,.07)";
        ctx.beginPath();
        ctx.moveTo(0, H * gy);
        ctx.lineTo(W, H * gy);
        ctx.stroke();
      });

      // vertical band grid + labels
      ctx.font = "9px 'JetBrains Mono', Consolas, monospace";
      ctx.textAlign = "center";
      bands.forEach((band) => {
        const x = xOf(band.f);
        ctx.strokeStyle = "rgba(255,176,0,.07)";
        ctx.beginPath();
        ctx.moveTo(x, H * 0.14);
        ctx.lineTo(x, base);
        ctx.stroke();
        ctx.fillStyle = "rgba(180,174,150,.42)";
        ctx.fillText(band.label, x, base + 15);
      });

      // spectrum trace
      const points = [];
      const step = Math.max(2, Math.floor(W / 420));
      for (let x = 0; x <= W; x += step) {
        let v = 0.025 + 0.018 * (Math.sin(x * 0.031 + t * 0.9) + Math.sin(x * 0.083 - t * 0.55) + 2) / 2;
        bands.forEach((band, i) => {
          const d = (x - xOf(band.f)) / (W * (i === 4 ? 0.014 : 0.022));
          const shimmer = 0.86 + 0.14 * Math.sin(t * 0.9 + i * 1.7);
          v += band.amp * shimmer * Math.exp(-(d * d));
        });
        points.push([x, base - Math.min(0.95, v) * maxH]);
      }

      const grad = ctx.createLinearGradient(0, base - maxH, 0, base);
      grad.addColorStop(0, "rgba(255,176,0,.20)");
      grad.addColorStop(0.55, "rgba(255,176,0,.07)");
      grad.addColorStop(1, "rgba(255,176,0,0)");

      ctx.beginPath();
      ctx.moveTo(0, base);
      points.forEach(([x, y]) => ctx.lineTo(x, y));
      ctx.lineTo(W, base);
      ctx.closePath();
      ctx.fillStyle = grad;
      ctx.fill();

      ctx.beginPath();
      points.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
      ctx.strokeStyle = "rgba(255,176,0,.78)";
      ctx.lineWidth = 1.2;
      ctx.stroke();

      // peak glows
      bands.forEach((band, i) => {
        const x = xOf(band.f);
        const d = 0;
        const shimmer = 0.86 + 0.14 * Math.sin(t * 0.9 + i * 1.7);
        const y = base - Math.min(0.95, band.amp * shimmer + 0.04) * maxH;
        ctx.beginPath();
        ctx.arc(x, y, i === 4 ? 2.4 : 2, 0, Math.PI * 2);
        ctx.fillStyle = band.color + "0.9)";
        ctx.shadowColor = band.color + "0.55)";
        ctx.shadowBlur = 10;
        ctx.fill();
        ctx.shadowBlur = 0;
      });

      // scan line
      const sx = ((t * 0.035) % 1) * W;
      const scan = ctx.createLinearGradient(sx - 22, 0, sx + 22, 0);
      scan.addColorStop(0, "rgba(255,176,0,0)");
      scan.addColorStop(0.5, "rgba(255,176,0,.15)");
      scan.addColorStop(1, "rgba(255,176,0,0)");
      ctx.fillStyle = scan;
      ctx.fillRect(sx - 22, H * 0.12, 44, base - H * 0.12);
      ctx.strokeStyle = "rgba(255,176,0,.34)";
      ctx.beginPath();
      ctx.moveTo(sx, H * 0.12);
      ctx.lineTo(sx, base);
      ctx.stroke();

      // corner readout
      ctx.textAlign = "left";
      ctx.font = "8px 'JetBrains Mono', Consolas, monospace";
      ctx.fillStyle = "rgba(255,176,0,.46)";
      ctx.fillText("RF SPECTRUM / 150–2500 MHz", 18, H * 0.16);
      ctx.textAlign = "right";
      ctx.fillStyle = "rgba(155,240,92,.55)";
      ctx.fillText("● LIVE", W - 18, H * 0.16);
    }

    function loop(ts) {
      raf = 0;
      if (!visible || document.hidden) return;
      if (ts - last > 34) {
        draw(ts / 1000);
        last = ts;
      }
      raf = requestAnimationFrame(loop);
    }

    function start() {
      if (raf || reduceMotion) return;
      raf = requestAnimationFrame(loop);
    }
    function stop() {
      if (raf) cancelAnimationFrame(raf);
      raf = 0;
    }

    resize();
    if (reduceMotion) {
      draw(0.9);
    } else {
      requestAnimationFrame(() => { resize(); draw(0.6); start(); });
    }

    if ("ResizeObserver" in window) {
      new ResizeObserver(() => {
        resize();
        if (reduceMotion) draw(0.9);
      }).observe(spectrum);
    } else {
      window.addEventListener("resize", resize, { passive: true });
    }

    document.addEventListener("visibilitychange", () => {
      if (document.hidden) stop();
      else start();
    });

    if ("IntersectionObserver" in window) {
      new IntersectionObserver((entries) => {
        entries.forEach((entry) => {
          visible = entry.isIntersecting;
          if (visible) start();
          else stop();
        });
      }, { threshold: 0.02 }).observe(spectrum);
    }
  }

  /* --------------------------------------------------------- configurator */
  const cards = $$(".mod-card");
  const cardById = new Map(cards.map((card) => [card.dataset.id, card]));
  const optionalIds = cards.map((card) => card.dataset.id);
  const budgetRange = $("#budgetRange");
  const totalEl = $("#total");
  const budgetValEl = $("#budgetVal");
  const presetNameEl = $("#presetName");
  const buildCodeEl = $("#buildCode");
  const selectedCountEl = $("#selectedCount");
  const gauge = $(".gauge");
  const gaugeFill = $("#gaugeFill");
  const reserveEl = $("#reserve");
  const budgetStateEl = $("#budgetState");
  const sheetList = $("#sheetList");
  const sheetWarn = $("#sheetWarn");
  const fitBtn = $("#fitBudget");
  const copyBtn = $("#copyBom");
  const randomBtn = $("#randomize");
  const presetButtons = $$(".preset");

  const state = {
    budget: 100,
    selected: new Set(["ys"]),
    preset: "scout",
  };
  let interacted = false;

  const priceOf = (id) => {
    const card = cardById.get(id);
    return card ? Number(card.dataset.price) || 0 : 0;
  };
  const nameOf = (id) => cardById.get(id)?.querySelector(".mod-name")?.textContent.trim() || id;
  const totalOf = () => BASE_PRICE + [...state.selected].reduce((sum, id) => sum + priceOf(id), 0);

  function detectPreset() {
    for (const [key, preset] of Object.entries(PRESETS)) {
      if (preset.budget !== state.budget) continue;
      if (preset.ids.length !== state.selected.size) continue;
      if (preset.ids.every((id) => state.selected.has(id))) return key;
    }
    return "custom";
  }

  function syncChecks() {
    cards.forEach((card) => {
      const id = card.dataset.id;
      const on = state.selected.has(id);
      const input = $(".mod-check", card);
      if (input) input.checked = on;
      card.classList.toggle("is-on", on);
    });
  }

  function applyPreset(key) {
    const preset = PRESETS[key];
    if (!preset) return;
    state.budget = preset.budget;
    state.selected = new Set(preset.ids);
    state.preset = detectPreset();
    syncChecks();
    render();
    updateHash();
  }

  function render() {
    const total = totalOf();
    const over = total > state.budget;
    const reserve = state.budget - total;
    const preset = PRESETS[state.preset];

    if (totalEl) totalEl.textContent = String(total);
    if (budgetValEl) budgetValEl.textContent = String(state.budget);
    if (budgetRange && Number(budgetRange.value) !== state.budget) budgetRange.value = String(state.budget);
    if (presetNameEl) {
      presetNameEl.innerHTML = preset
        ? `${preset.name} <em>${preset.en}</em>`
        : `自定义 <em>CUSTOM</em>`;
    }
    if (buildCodeEl) {
      buildCodeEl.textContent = preset ? `${preset.en}-${state.budget}` : `CUSTOM-${state.budget}`;
    }
    if (selectedCountEl) selectedCountEl.textContent = `${state.selected.size} / ${optionalIds.length} SELECTED`;

    presetButtons.forEach((button) => {
      button.setAttribute("aria-pressed", String(button.dataset.preset === state.preset));
    });

    if (gaugeFill) gaugeFill.style.width = `${Math.min(100, (total / state.budget) * 100)}%`;
    if (gauge) gauge.classList.toggle("budget-over", over);

    if (reserveEl) {
      const first = reserveEl.firstElementChild;
      if (first) first.textContent = over ? `超出 ¥${Math.abs(reserve)}` : `余量 ¥${reserve}`;
    }
    if (budgetStateEl) {
      budgetStateEl.textContent = over ? "OVER BUDGET" : reserve === 0 ? "AT CEILING" : "WITHIN BUDGET";
      budgetStateEl.className = over ? "over" : "ok";
    }

    // capability matrix
    const has = (id) => state.selected.has(id);
    const caps = {
      fixed: has("ys"),
      sub: has("cc1101") || has("si4463") || has("lr1121"),
      g24: has("nrf") || has("lr1121"),
      lora: has("lr1121"),
    };
    $$(".cap").forEach((cap) => {
      const key = cap.dataset.cap;
      if (key === "mqtt") {
        cap.classList.remove("on");
        return;
      }
      const on = Boolean(caps[key]);
      cap.classList.toggle("on", on);
      const value = cap.querySelector("b");
      if (value) value.textContent = on ? "YES" : "NO";
    });

    // build sheet rows
    if (sheetList) {
      const rows = [`<div class="sheet-row"><span>${BASE_NAME}</span><b>¥${BASE_PRICE}</b></div>`];
      optionalIds.forEach((id) => {
        if (!state.selected.has(id)) return;
        rows.push(`<div class="sheet-row"><span>${nameOf(id)}</span><b>¥${priceOf(id)}</b></div>`);
      });
      sheetList.innerHTML = rows.join("");
    }

    // warnings
    const warnings = [];
    if (!has("ys")) warnings.push("没有 YS-RF34DL：当前固件只能当 C3 开发板用，固定码链路为空。");
    if (over) warnings.push(`超出预算 ¥${Math.abs(reserve)}。可手动减配，或点“削到预算内”。`);
    if (sheetWarn) {
      sheetWarn.hidden = warnings.length === 0;
      sheetWarn.textContent = warnings.join(" ");
    }
    if (fitBtn) fitBtn.hidden = !over;
    if (copyBtn) copyBtn.title = over ? "当前方案超出预算，仍可复制为规划单" : "复制配装单";
  }

  function updateHash() {
    if (!interacted) return;
    const value = `#b=${state.budget}&m=${[...state.selected].join(",")}`;
    try {
      history.replaceState(null, "", value);
    } catch {
      /* file:// in some browsers refuses replaceState; the page still works. */
    }
  }

  function readHash() {
    const raw = window.location.hash.replace(/^#/, "");
    if (!raw.startsWith("b=")) return false;
    const params = new URLSearchParams(raw);
    const budget = Number(params.get("b"));
    if (!Number.isFinite(budget) || budget < 100 || budget > 999) return false;
    const ids = (params.get("m") || "").split(",").filter((id) => cardById.has(id));
    state.budget = Math.round(budget);
    state.selected = new Set(ids);
    state.preset = detectPreset();
    return true;
  }

  if (readHash()) syncChecks();
  render();

  window.addEventListener("hashchange", () => {
    if (!window.location.hash.startsWith("#b=")) return;
    if (!readHash()) return;
    syncChecks();
    render();
    goTo("budget");
  });

  if (budgetRange) {
    budgetRange.addEventListener("input", () => {
      interacted = true;
      state.budget = Math.round(Number(budgetRange.value) || 100);
      state.preset = detectPreset();
      render();
      updateHash();
    });
  }

  cards.forEach((card) => {
    const input = $(".mod-check", card);
    if (!input) return;
    input.addEventListener("change", () => {
      interacted = true;
      const id = card.dataset.id;
      if (input.checked) state.selected.add(id);
      else state.selected.delete(id);
      state.preset = detectPreset();
      card.classList.toggle("is-on", input.checked);
      render();
      updateHash();
    });
  });

  presetButtons.forEach((button) => {
    button.addEventListener("click", () => {
      interacted = true;
      applyPreset(button.dataset.preset);
      toast(`${PRESETS[button.dataset.preset].name}方案已装载 / 预算 ¥${state.budget}`);
    });
  });

  if (randomBtn) {
    randomBtn.addEventListener("click", () => {
      interacted = true;
      const pool = optionalIds.filter((id) => id !== "ys");
      for (let i = pool.length - 1; i > 0; i -= 1) {
        const j = Math.floor(Math.random() * (i + 1));
        [pool[i], pool[j]] = [pool[j], pool[i]];
      }
      const chosen = new Set(["ys"]);
      let running = BASE_PRICE + priceOf("ys");
      pool.forEach((id) => {
        const price = priceOf(id);
        if (Math.random() < 0.22) return;
        if (running + price <= state.budget) {
          chosen.add(id);
          running += price;
        }
      });
      state.selected = chosen;
      state.preset = detectPreset();
      syncChecks();
      render();
      updateHash();
      toast(`随机配装完成 / ¥${totalOf()} · 余量 ¥${state.budget - totalOf()}`);
    });
  }

  if (fitBtn) {
    fitBtn.addEventListener("click", () => {
      interacted = true;
      let total = totalOf();
      const removable = [...state.selected]
        .filter((id) => id !== "ys")
        .sort((a, b) => priceOf(b) - priceOf(a));
      while (total > state.budget && removable.length) {
        const id = removable.shift();
        state.selected.delete(id);
        total -= priceOf(id);
      }
      state.preset = detectPreset();
      syncChecks();
      render();
      updateHash();
      toast(`已削到 ¥${state.budget} 预算内 / 当前 ¥${totalOf()}`);
    });
  }

  function buildText() {
    const total = totalOf();
    const reserve = state.budget - total;
    const lines = [
      "RF-LAB // BUILD SHEET",
      `预算: ¥${state.budget}`,
      `方案: ${PRESETS[state.preset] ? `${PRESETS[state.preset].name} / ${PRESETS[state.preset].en}` : "自定义 / CUSTOM"}`,
      "物料:",
      `  1x ${BASE_NAME}  ¥${BASE_PRICE}`,
    ];
    optionalIds.forEach((id) => {
      if (state.selected.has(id)) lines.push(`  1x ${nameOf(id)}  ¥${priceOf(id)}`);
    });
    lines.push(`合计: ¥${total}`);
    lines.push(`余量: ¥${reserve}`);
    lines.push("");
    lines.push("注意: 只有 YS-RF34DL 完成上机验证；CC1101 / NRF24L01 / SI4463 / LR1121 / MQTT / HA 为路线图，不是现货能力。");
    lines.push("参考物料价，不含运费与工具。");
    lines.push(`配置链接: ${window.location.href.split("#")[0]}#b=${state.budget}&m=${[...state.selected].join(",")}`);
    return lines.join("\n");
  }

  async function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
      try {
        await navigator.clipboard.writeText(text);
        return true;
      } catch {
        /* fall through */
      }
    }
    try {
      const area = document.createElement("textarea");
      area.value = text;
      area.setAttribute("readonly", "");
      area.style.position = "fixed";
      area.style.top = "-1000px";
      area.style.opacity = "0";
      document.body.appendChild(area);
      area.select();
      const ok = document.execCommand("copy");
      area.remove();
      return ok;
    } catch {
      return false;
    }
  }

  const copyDialog = $("#copyDialog");
  const copyArea = $("#copyText");
  function openCopyFallback(text) {
    if (!copyDialog || !copyArea) return;
    copyArea.value = text;
    if (!copyDialog.open) copyDialog.showModal();
    window.setTimeout(() => { copyArea.focus(); copyArea.select(); }, 30);
  }
  if ($("#copyClose")) $("#copyClose").addEventListener("click", () => copyDialog?.close());
  if (copyDialog) {
    copyDialog.addEventListener("click", (event) => {
      if (event.target === copyDialog) copyDialog.close();
    });
  }
  if ($("#copySelect")) $("#copySelect").addEventListener("click", () => {
    if (!copyArea) return;
    copyArea.focus();
    copyArea.select();
  });

  if (copyBtn) {
    copyBtn.addEventListener("click", async () => {
      interacted = true;
      const text = buildText();
      const ok = await copyText(text);
      if (ok) toast(`配装单已复制 / ¥${totalOf()} · 预算 ¥${state.budget}`);
      else {
        openCopyFallback(text);
        toast("剪贴板不可用：已打开手动复制。", true);
      }
    });
  }

  /* ------------------------------------------------------- terminal demo */
  const DEMO_VIEWS = {
    web: { w: 1440, h: 900, mode: "WEB // 1440×900", caption: "桌面布局 · 真实单页 UI · 本地模拟传输" },
    app: { w: 390, h: 844, mode: "APP // 390×844", caption: "WebView 布局 · 同一份 UI · 窄屏单列" },
    tui: { w: 1280, h: 720, mode: "UCONSOLE // 1280×720", caption: "uConsole 逻辑分辨率 · 三栏完整" },
  };
  const demoTabs = $$(".tab");
  const demoPanel = $("#demoPanel");
  const demoFrame = $("#demoFrame");
  const demoViewport = $("#demoViewport");
  const demoStage = $("#demoStage");
  const demoLoading = $("#demoLoading");
  const demoModeLabel = $("#demoModeLabel");
  const demoHint = $("#demoHint");
  const demoCaption = $("#demoCaption");
  const demoLive = $("#demoLive");
  const demoLearn = $("#demoLearn");
  const demoReset = $("#demoReset");
  const feedBody = $("#feedBody");
  let demoView = "web";
  let demoReady = false;

  function layoutDemo() {
    if (!demoFrame || !demoStage || !demoViewport) return;
    const view = DEMO_VIEWS[demoView];
    const availW = Math.max(240, demoViewport.clientWidth - 24);
    const availH = Math.min(window.innerHeight * 0.78, 700);
    const scale = Math.min(1, availW / view.w, availH / view.h);
    demoStage.style.width = `${Math.round(view.w * scale)}px`;
    demoStage.style.height = `${Math.round(view.h * scale)}px`;
    demoFrame.style.width = `${view.w}px`;
    demoFrame.style.height = `${view.h}px`;
    demoFrame.style.transform = `scale(${scale})`;
    if (demoPanel) demoPanel.dataset.view = demoView;
    if (demoModeLabel) demoModeLabel.textContent = view.mode;
    if (demoCaption) demoCaption.textContent = view.caption;
    if (demoHint) {
      demoHint.textContent = demoView === "app"
        ? "点码库选中 → ARM → SEND；底部「模拟遥控器」可学习"
        : "点 ARM → SEND；码库行尾按钮可 LONG / STOP / ERASE";
    }
  }

  function selectView(key, focus = false) {
    if (!DEMO_VIEWS[key]) return;
    demoView = key;
    demoTabs.forEach((tab) => {
      const active = tab.dataset.view === key;
      tab.classList.toggle("active", active);
      tab.setAttribute("aria-selected", String(active));
      tab.tabIndex = active ? 0 : -1;
      if (active && focus) tab.focus();
    });
    if (demoPanel) demoPanel.setAttribute("aria-labelledby", `tab-${key}`);
    layoutDemo();
  }

  demoTabs.forEach((tab, index) => {
    tab.addEventListener("click", () => selectView(tab.dataset.view));
    tab.addEventListener("keydown", (event) => {
      let next = -1;
      if (event.key === "ArrowRight") next = (index + 1) % demoTabs.length;
      if (event.key === "ArrowLeft") next = (index - 1 + demoTabs.length) % demoTabs.length;
      if (event.key === "Home") next = 0;
      if (event.key === "End") next = demoTabs.length - 1;
      if (next >= 0) {
        event.preventDefault();
        selectView(demoTabs[next].dataset.view, true);
      }
    });
  });

  const mobileQuery = window.matchMedia("(max-width: 760px)");
  if (mobileQuery.matches) selectView("app");
  else layoutDemo();
  if (typeof mobileQuery.addEventListener === "function") {
    mobileQuery.addEventListener("change", (event) => {
      if (event.matches && demoView === "web") selectView("app");
      else layoutDemo();
    });
  }
  window.addEventListener("resize", layoutDemo, { passive: true });
  if ("ResizeObserver" in window && demoViewport) {
    new ResizeObserver(layoutDemo).observe(demoViewport);
  }
  window.setTimeout(layoutDemo, 900);

  function feedLine(tag, text, cls = "") {
    const time = new Date().toLocaleTimeString("zh-CN", { hour12: false });
    const line = document.createElement("div");
    line.className = `feed-line ${cls}`.trim();
    const timeEl = document.createElement("time");
    timeEl.textContent = time;
    const tagEl = document.createElement("b");
    tagEl.textContent = tag;
    const textEl = document.createElement("span");
    textEl.textContent = text;
    line.append(timeEl, tagEl, textEl);
    return line;
  }

  function pushFeed(tag, text, cls = "") {
    if (!feedBody || !text) return;
    feedBody.appendChild(feedLine(tag, text, cls));
    while (feedBody.children.length > 5) feedBody.firstElementChild?.remove();
  }

  if (feedBody) {
    pushFeed("SYS", "terminal link // 本地模拟 · 无网络");
    pushFeed("SIM", "点 ARM 解锁 → SEND；或按「模拟遥控器」");
  }

  function demoPost(op) {
    try {
      demoFrame?.contentWindow?.postMessage({ source: "rflab-site", op }, "*");
    } catch {
      /* iframe not ready yet */
    }
  }

  window.addEventListener("message", (event) => {
    const data = event.data;
    if (!data || data.source !== "rflab-demo") return;
    if (data.type === "ready") {
      demoLoading?.classList.add("gone");
      demoPanel?.classList.add("is-live");
      if (demoLearn) demoLearn.disabled = false;
      if (demoLive) demoLive.innerHTML = `<i class="dot ok"></i> SIM // ${data.store ?? "?"}/32`;
      if (!demoReady) {
        demoReady = true;
        pushFeed("SYS", `link up // ${data.store ?? "?"} codes · 本地模拟`);
      }
    } else if (data.type === "state") {
      if (demoLive && Number.isFinite(data.store)) {
        demoLive.innerHTML = `<i class="dot ok"></i> SIM // ${data.store}/32`;
      }
    } else if (data.type === "event") {
      const kind = String(data.kind || "log").toUpperCase();
      const cls = data.ok ? "ok" : kind === "TX" ? "cmd" : "";
      pushFeed(kind, data.line || "", cls);
    }
  });

  if (demoFrame) {
    demoFrame.addEventListener("load", () => {
      demoPost("hello");
      window.setTimeout(() => demoLoading?.classList.add("gone"), 900);
    });
  }
  window.setTimeout(() => {
    demoLoading?.classList.add("gone");
    if (demoPanel && !demoPanel.classList.contains("is-live") && demoHint) {
      demoHint.textContent = "内嵌 demo 未就绪；点 OPEN 全屏打开";
    }
  }, 7000);

  if (demoLearn) {
    demoLearn.addEventListener("click", () => {
      demoPost("learn");
      demoLearn.disabled = true;
      window.setTimeout(() => { demoLearn.disabled = false; }, 900);
      toast("SIM // 模拟遥控器按下");
    });
  }
  if (demoReset) {
    demoReset.addEventListener("click", () => {
      demoPost("reset");
      if (feedBody) {
        feedBody.textContent = "";
        pushFeed("SYS", "terminal link // 已重置");
      }
      toast("SIM // 演示已重置");
    });
  }

  /* ------------------------------------------------------------- counters */
  const counters = $$("[data-count]");
  function paintCount(el, value) {
    const decimals = Number(el.dataset.decimals || 0);
    const sep = el.dataset.sep === "1";
    let text = Number(value).toFixed(decimals);
    if (sep) text = text.replace(/\B(?=(\d{3})+(?!\d))/g, ",");
    el.textContent = text;
  }
  function animateCount(el) {
    const target = Number(el.dataset.count);
    if (!Number.isFinite(target)) return;
    if (reduceMotion || target === 0) {
      paintCount(el, target);
      return;
    }
    const started = performance.now();
    const duration = 900;
    function tick(now) {
      const progress = Math.min(1, (now - started) / duration);
      const eased = 1 - Math.pow(1 - progress, 3);
      paintCount(el, target * eased);
      if (progress < 1) requestAnimationFrame(tick);
      else paintCount(el, target);
    }
    requestAnimationFrame(tick);
  }
  if (counters.length) {
    if (reduceMotion || !("IntersectionObserver" in window)) {
      counters.forEach(animateCount);
    } else {
      const counterObserver = new IntersectionObserver((entries, obs) => {
        entries.forEach((entry) => {
          if (!entry.isIntersecting) return;
          animateCount(entry.target);
          obs.unobserve(entry.target);
        });
      }, { threshold: 0.4 });
      counters.forEach((el) => counterObserver.observe(el));
    }
  }

  /* ---------------------------------------------------------------- manual */
  const helpDialog = $("#helpDialog");
  function openHelp() {
    if (!helpDialog || helpDialog.open) return;
    if (typeof helpDialog.showModal === "function") helpDialog.showModal();
  }
  if ($("#helpBtn")) $("#helpBtn").addEventListener("click", openHelp);
  if ($("#helpClose")) $("#helpClose").addEventListener("click", () => helpDialog?.close());
  if (helpDialog) {
    helpDialog.addEventListener("click", (event) => {
      if (event.target === helpDialog) helpDialog.close();
    });
  }

  function goTo(id) {
    const target = document.getElementById(id);
    if (!target) return;
    target.scrollIntoView({ behavior: reduceMotion ? "auto" : "smooth", block: "start" });
  }

  window.addEventListener("keydown", (event) => {
    const target = event.target;
    const editing = target instanceof Element && (
      target.matches("input, textarea, select") ||
      target.isContentEditable
    );
    if (editing || event.metaKey || event.ctrlKey || event.altKey) return;

    const key = event.key.toLowerCase();
    if (document.querySelector("dialog[open]")) return;
    if (key === "?") {
      event.preventDefault();
      openHelp();
      return;
    }

    if (key === "b") { event.preventDefault(); goTo("budget"); }
    else if (key === "t") { event.preventDefault(); goTo("terminal"); }
    else if (key === "u") { event.preventDefault(); goTo("uplink"); }
    else if (key === "r") {
      event.preventDefault();
      randomBtn?.click();
    }
  });
})();
