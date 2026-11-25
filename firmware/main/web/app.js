"use strict";
function sameMetric(a, b) {
  return ["host", "source", "key"].every((key) => a[key] === b[key]);
}
const $ = (s) => document.querySelector(s),
  esc = (v) =>
    String(v ?? "").replace(
      /[&<>"']/g,
      (c) =>
        ({
          "&": "&amp;",
          "<": "&lt;",
          ">": "&gt;",
          '"': "&quot;",
          "'": "&#39;",
        })[c],
    );
let token = sessionStorage.getItem("curio-token") || "",
  configDocument,
  configEtag = "",
  yamlText = "",
  draftSource = "synced",
  certificateHostname = "curio",
  display,
  device,
  network,
  alerts,
  state = { hosts: [] },
  tab = "screen",
  selected = 0,
  selectedWidget = "",
  polling = false,
  pollTimer,
  message = "",
  pollError = "",
  gesture = null;
const layoutHistory = [];
const dirtySections = new Set();
function markDirty(section) {
  dirtySections.add(section);
  draftSource = section === "config" ? "yaml" : "forms";
}
const minimum = (type) => widgetTypes[type]?.minimum;
const clone = (value) => structuredClone(value);
function validRect(w) {
  const min = minimum(w.type);
  return (
    min &&
    [w.at, w.size].every(
      (a) => Array.isArray(a) && a.length === 2 && a.every(Number.isInteger),
    ) &&
    w.at[0] >= 0 &&
    w.at[1] >= 0 &&
    w.size[0] >= min[0] &&
    w.size[1] >= min[1] &&
    w.at[0] + w.size[0] <= 4 &&
    w.at[1] + w.size[1] <= 6
  );
}
function rectMask(w) {
  let mask = 0;
  for (let y = w.at[1]; y < w.at[1] + w.size[1]; y++)
    for (let x = w.at[0]; x < w.at[0] + w.size[0]; x++)
      mask |= 1 << (y * 4 + x);
  return mask;
}
function validateLayout(p) {
  if (!Array.isArray(p.widgets) || p.widgets.length > 8)
    throw Error("每页最多 8 个组件");
  let used = 0;
  for (const w of p.widgets) {
    if (!validRect(w))
      throw Error(`${w.title || w.type}: at / size 超出边界或小于最小尺寸`);
    const mask = rectMask(w);
    if (used & mask) throw Error("组件重叠");
    used |= mask;
  }
}
function pixelRect(w) {
  return {
    x: 12 + w.at[0] * 116,
    y: 60 + Math.floor((w.at[1] * 416) / 6),
    width: w.size[0] * 116 - 8,
    height:
      Math.floor(((w.at[1] + w.size[1]) * 416) / 6) -
      Math.floor((w.at[1] * 416) / 6) -
      8,
  };
}
function rememberLayout() {
  layoutHistory.push({
    display: clone(display),
    selected,
    selectedWidget,
    dirty: dirtySections.has("screen"),
  });
  if (layoutHistory.length > 30) layoutHistory.shift();
  const undo = $("#undo-layout");
  if (undo) undo.disabled = false;
}
function layoutProposal(next) {
  if (!validRect(next)) return null;
  const widgets = page().widgets,
    others = widgets.filter((w) => w.id !== next.id);
  if (
    others.reduce(
      (sum, w) => sum + w.size[0] * w.size[1],
      next.size[0] * next.size[1],
    ) > 24
  )
    return null;
  others.sort((a, b) => b.size[0] * b.size[1] - a.size[0] * a.size[1]);
  const choices = others.map((w) => {
    const result = [];
    for (let y = 0; y <= 6 - w.size[1]; y++)
      for (let x = 0; x <= 4 - w.size[0]; x++) {
        const moved = { ...w, at: [x, y] },
          distance = Math.abs(x - w.at[0]) + Math.abs(y - w.at[1]);
        result.push({
          widget: moved,
          mask: rectMask(moved),
          cost: distance ? 1000 + distance : 0,
        });
      }
    return result.sort((a, b) => a.cost - b.cost);
  });
  let best = null,
    bestCost = Infinity,
    nodes = 0,
    exhausted = false;
  function place(i, used, cost, placed) {
    if (++nodes > 25000) {
      exhausted = true;
      return;
    }
    if (cost >= bestCost) return;
    if (i === others.length) {
      best = [next, ...placed];
      bestCost = cost;
      return;
    }
    let lower = cost;
    for (let j = i; j < choices.length; j++) {
      const first = choices[j].find((option) => !(used & option.mask));
      if (!first) return;
      lower += first.cost;
    }
    if (lower >= bestCost) return;
    for (const option of choices[i]) {
      if (exhausted) return;
      if (!(used & option.mask) && cost + option.cost < bestCost)
        place(i + 1, used | option.mask, cost + option.cost, [
          ...placed,
          option.widget,
        ]);
    }
  }
  place(0, rectMask(next), 0, []);
  return best
    ? widgets.map((w) => best.find((result) => result.id === w.id))
    : null;
}
function commitLayout(result) {
  if (!result) {
    status("无法放置，请调整位置或尺寸");
    render();
    return;
  }
  if (JSON.stringify(result) === JSON.stringify(page().widgets)) {
    render();
    return;
  }
  rememberLayout();
  page().widgets = result;
  markDisplayDirty();
  render();
}
function updateStatus() {
  $("#status").textContent = [
    message,
    pollError,
    dirtySections.has("config") ? "配置未保存" : "",
    dirtySections.has("screen") ? "屏幕未保存" : "",
    dirtySections.has("device") ? "设备未保存" : "",
    dirtySections.has("alerts") ? "提醒未保存" : "",
  ]
    .filter(Boolean)
    .join(" · ");
}
function status(s) {
  message = s;
  updateStatus();
}
function markDisplayDirty() {
  markDirty("screen");
  status("");
}

const page = () => display.pages[selected];
const hosts = () => state.hosts;
const host = (id) => hosts().find((h) => h.host_id === id);
const entries = (o) => Object.entries(o || {});
const source = (h, id) => h?.sources?.[id];
const health = (h, s) =>
  h?.health === "offline"
    ? "Offline"
    : s?.health === "stale"
      ? "Stale"
      : s?.status === "error"
        ? "Collection error"
        : s?.status === "unavailable"
          ? "Unavailable"
          : s?.status === "warming_up"
            ? "Warming up"
            : "";
function formatMetric(value, unit) {
  if (!Number.isFinite(value)) return "--";
  const rounded = (n, scale) =>
    (Math.sign(n) * Math.round(Math.abs(n) * scale)) / scale;
  const decimal = (n) =>
    String(Number((Math.abs(n) < 1e14 ? rounded(n, 10) : n).toFixed(1)));
  const scaled = (base, labels) => {
    let i = 0;
    while (Math.abs(value) >= base && i < labels.length - 1) {
      value /= base;
      i++;
    }
    return `${decimal(value)} ${labels[i]}`;
  };
  if (unit === "bytes" || unit === "bytes_per_second")
    return scaled(
      1024,
      ["B", "KiB", "MiB", "GiB", "TiB", "PiB"].map(
        (u) => u + (unit === "bytes_per_second" ? "/s" : ""),
      ),
    );
  if (unit === "watts") return scaled(1000, ["W", "kW", "MW"]);
  if (unit === "hertz") return scaled(1000, ["Hz", "kHz", "MHz", "GHz"]);
  if (unit === "seconds" || unit === "milliseconds") {
    if (unit === "milliseconds") value /= 1000;
    if (value !== 0 && Math.abs(value) < 1)
      return `${decimal(value * 1000)} ms`;
    let label = "s";
    for (const [base, next] of [
      [60, "min"],
      [60, "h"],
      [24, "d"],
    ]) {
      if (Math.abs(value) < base) break;
      value /= base;
      label = next;
    }
    return `${decimal(value)} ${label}`;
  }
  if (unit === "count")
    return String(Math.abs(value) < 1e14 ? rounded(value, 1) : value);
  return `${decimal(value)}${unit === "percent" ? "%" : unit === "number" || !unit ? "" : ` ${{ celsius: "C" }[unit] || unit}`}`;
}
function resolve(b, unit) {
  const h = host(b.host);
  if (!h) return { text: "Waiting" };
  const s = source(h, b.source),
    problem = health(h, s);
  if (problem) return { text: problem };
  if (!s) return { text: "Not reported" };
  if (s.kind !== "metrics") return { text: "Type mismatch" };
  const m = s.metrics?.[b.key];
  if (!m) return { text: "Not reported" };
  if (unit && m.unit !== unit) return { text: "Type mismatch" };
  if (m.age_ms > (s.max_age_ms ?? 10000)) return { text: "Stale" };
  if (typeof m.value !== "number" || !Number.isFinite(m.value))
    return { text: "Unavailable" };
  return { value: m.value, text: formatMetric(m.value, m.unit) };
}
function validateNtp(value) {
  if (
    typeof value !== "string" ||
    !value ||
    value.length > 127 ||
    !/^[A-Za-z0-9.:-]+$/.test(value)
  )
    throw Error("NTP 服务器须为主机名或 IP，最多 127 字符");
}
const bytes = (value) => new TextEncoder().encode(value).length;
const checkString = (value, name, max, id = false, optional = false) => {
  if (typeof value !== "string" || (!value && !optional))
    throw Error(`${name}不能为空`);
  if (bytes(value) > max) throw Error(`${name}最多 ${max} 字节`);
  if (id && value && !/^[A-Za-z0-9._-]+$/.test(value))
    throw Error(`${name}格式错误`);
};

const checkNumber = (value, label, min, max, integer = true) => {
  if (
    !Number.isFinite(value) ||
    (integer && !Number.isInteger(value)) ||
    value < min ||
    value > max
  )
    throw Error(`${label}须为 ${min}–${max}`);
};
const checkIdentifier = (value, label) => checkString(value, label, 48, true);

function dirty() {
  return dirtySections.size > 0;
}
function parseConfig(source) {
  const value = jsyaml.load(source, { schema: jsyaml.JSON_SCHEMA });
  if (!value || typeof value !== "object" || Array.isArray(value))
    throw Error("配置须为 YAML 对象");
  if (
    !value.access ||
    typeof value.access.admin_token !== "string" ||
    typeof value.access.collector_token !== "string"
  )
    throw Error("缺少 access.admin_token 或 access.collector_token");
  if (!Array.isArray(value.screen?.pages) || !value.screen.pages.length)
    throw Error("screen.pages 不能为空");
  if (value.alerts?.rules !== undefined && !Array.isArray(value.alerts.rules))
    throw Error("alerts.rules 须为列表");
  const keys = (object, allowed, path) => {
    if (!object || typeof object !== "object" || Array.isArray(object))
      throw Error(`${path} 须为对象`);
    for (const key of Object.keys(object))
      if (!allowed.includes(key)) throw Error(`未知字段: ${path}.${key}`);
  };
  keys(
    value,
    ["version", "access", "device", "network", "tls", "screen", "alerts"],
    "config",
  );
  keys(value.access, ["admin_token", "collector_token"], "access");
  if (value.device !== undefined)
    keys(
      value.device,
      ["brightness", "rotation", "auto_rotate", "ntp_server"],
      "device",
    );
  if (value.network !== undefined) {
    keys(value.network, ["wifi", "hostname"], "network");
    if (value.network.wifi !== undefined) {
      validateWifi(value.network.wifi);
      for (const [i, wifi] of value.network.wifi.entries())
        keys(wifi, ["ssid", "password", "ipv4"], `network.wifi[${i}]`);
    }
  }
  if (value.tls !== undefined)
    keys(value.tls, ["certificate", "private_key"], "tls");
  keys(value.screen, ["default_page", "pages"], "screen");
  const pageIds = new Set();
  const checkId = (id, ids, path) => {
    if (typeof id !== "string" || !/^[A-Za-z0-9._-]{1,32}$/.test(id))
      throw Error(`${path} 格式错误`);
    if (ids.has(id)) throw Error(`${path} 重复: ${id}`);
    ids.add(id);
  };
  for (const [i, page] of value.screen.pages.entries()) {
    const path = `screen.pages[${i}]`;
    keys(page, ["id", "title", "widgets"], path);
    checkId(page.id, pageIds, `${path}.id`);
    const widgetIds = new Set();
    if (!Array.isArray(page.widgets)) throw Error(`${path}.widgets 须为列表`);
    for (const [j, widget] of page.widgets.entries()) {
      const module = widgetTypes[widget?.type];
      if (!module) throw Error(`Widget 类型无效: ${widget?.type}`);
      const fields = Object.keys(module.types);
      keys(
        widget,
        ["id", "type", "title", "at", "size", ...fields],
        `${path}.widgets[${j}]`,
      );
      checkId(
        widget.id ?? `${widget.type}-${j + 1}`,
        widgetIds,
        `${path}.widgets[${j}].id`,
      );
      scalarTypes(widget, `${path}.widgets[${j}]`, {
        title: "string",
        ...module.types,
      });
      const normalized = { ...module.defaults(), ...widget };
      checkString(normalized.title ?? "", "Widget 标题", 48, false, true);
      module.validate(normalized, checkString);
    }
    checkString(page.title ?? page.id, "页面名称", 48, false, true);
    validateLayout(page);
  }
  if (value.screen.pages.length > 4) throw Error("最多 4 个页面");
  if (!pageIds.has(value.screen.default_page ?? value.screen.pages[0].id))
    throw Error("screen.default_page 指向不存在的页面");
  if (value.alerts !== undefined)
    keys(
      value.alerts,
      ["volume", "vibration_strength", "flash_enabled", "rules"],
      "alerts",
    );
  const rules = value.alerts?.rules ?? [];
  if (rules.length > 8) throw Error("最多 8 条规则");
  const ruleIds = new Set();
  for (const [i, rule] of rules.entries()) {
    const id = rule.id ?? `rule-${i + 1}`;
    checkIdentifier(id, "规则 ID");
    if (ruleIds.has(id)) throw Error("规则 ID 重复");
    ruleIds.add(id);
    checkNumber(rule.duration ?? 10, "显示时间", 0, 300);
    const module = alertTypes[rule?.type];
    if (!module) throw Error(`提醒类型无效: ${rule?.type}`);
    const fields = Object.keys(module.types);
    keys(
      rule,
      [
        "id",
        "type",
        "enabled",
        "duration",
        "sound",
        "vibrate",
        "flash",
        ...fields,
      ],
      `alerts.rules[${i}]`,
    );
    scalarTypes(rule, `alerts.rules[${i}]`, {
      enabled: "boolean",
      duration: "number",
      sound: "boolean",
      vibrate: "boolean",
      flash: "boolean",
      ...module.types,
    });
    module.validate(
      { ...module.defaults(), ...rule },
      { id: checkIdentifier, number: checkNumber },
    );
  }
  function commonTypes() {
    return {
      version: "number",
      brightness: "number",
      rotation: "number",
      volume: "number",
      vibration_strength: "number",
      duration: "number",
      auto_rotate: "boolean",
      flash_enabled: "boolean",
      enabled: "boolean",
      sound: "boolean",
      vibrate: "boolean",
      flash: "boolean",
      ntp_server: "string",
      ssid: "string",
      password: "string",
      hostname: "string",
      certificate: "string",
      private_key: "string",
    };
  }
  function scalarTypes(object, path, schema = commonTypes()) {
    for (const [key, item] of Object.entries(object)) {
      const name = `${path}.${key}`;
      if (item === null) throw Error(`${name} 不能为 null`);
      if (schema[key] && (Array.isArray(item) ? "array" : typeof item) !== schema[key])
        throw Error(`${name} 类型错误`);
      if (typeof item === "object" && !item.type)
        scalarTypes(item, name, schema);
    }
  }
  scalarTypes(value, "config");
  validateNtp(value.device?.ntp_server ?? "pool.ntp.org");
  const hostname = value.network?.hostname ?? "curio";
  if (!/^[A-Za-z0-9](?:[A-Za-z0-9-]{0,30}[A-Za-z0-9])?$/.test(hostname))
    throw Error("network.hostname 须为 1–32 位主机名");
  if (
    value.tls !== undefined &&
    (!value.tls.certificate ||
      !value.tls.private_key ||
      bytes(value.tls.certificate) > 2047 ||
      bytes(value.tls.private_key) > 2047)
  )
    throw Error(
      "tls.certificate 和 tls.private_key 须同时填写，且各不超过 2047 字节",
    );
  if (value.version !== undefined && value.version !== 1)
    throw Error("version 须为 1");
  if (
    value.device?.rotation !== undefined &&
    ![0, 90, 180, 270].includes(value.device.rotation)
  )
    throw Error("device.rotation 须为 0、90、180 或 270");
  checkNumber(value.device?.brightness ?? 75, "亮度", 0, 100);
  checkNumber(value.alerts?.volume ?? 35, "音量", 0, 100);
  checkNumber(value.alerts?.vibration_strength ?? 60, "alerts.vibration_strength", 0, 100);
  return value;
}
function loadFormConfig(value) {
  display = {
    default_page: value.screen.default_page ?? value.screen.pages[0].id,
    pages: value.screen.pages.map((p) => ({
      id: p.id,
      title: p.title ?? p.id,
      widgets: p.widgets.map((w, i) => ({
        ...widgetTypes[w.type].defaults(),
        ...clone(w),
        id: w.id ?? `${w.type}-${i + 1}`,
      })),
    })),
  };
  device = {
    ntp_server: "pool.ntp.org",
    brightness: 75,
    rotation: 0,
    auto_rotate: false,
    ...value.device,
  };
  alerts = {
    volume: value.alerts?.volume ?? 35,
    vibration_strength: value.alerts?.vibration_strength ?? 60,
    flash_enabled: value.alerts?.flash_enabled ?? false,
    rules: (value.alerts?.rules ?? []).map((r, i) => ({
      enabled: true,
      duration: 10,
      sound: false,
      vibrate: false,
      flash: false,
      ...alertTypes[r.type].defaults(),
      ...clone(r),
      id: r.id ?? `rule-${i + 1}`,
    })),
  };
  network = { hostname: "curio", wifi: [], ...clone(value.network ?? {}) };
  configDocument = { ...value, screen: display, device, network, alerts };
  certificateHostname = network.hostname;
  selected = Math.max(0, Math.min(selected, display.pages.length - 1));
}
function syncFormYaml() {
  if (draftSource !== "forms") return;
  if (network.hostname !== certificateHostname) delete configDocument.tls;
  yamlText = jsyaml.dump({
    ...configDocument,
    device: {
      brightness: device.brightness,
      rotation: device.rotation,
      auto_rotate: device.auto_rotate,
      ...(device.ntp_server !== "pool.ntp.org" ? { ntp_server: device.ntp_server } : {}),
    },
    network: {
      wifi: network.wifi.map(({ ssid, password, ipv4 }) => ({
        ssid,
        ...(password ? { password } : {}),
        ...(ipv4 !== undefined ? { ipv4 } : {}),
      })),
      ...(network.hostname !== "curio" ? { hostname: network.hostname } : {}),
    },
  }, {
    noRefs: true,
    lineWidth: -1,
    sortKeys: false,
  });
  draftSource = "synced";
}
function applyRawDraft() {
  const parsed = parseConfig(yamlText);
  Object.values(widgetTypes).forEach((module) => module.clear?.());
  loadFormConfig(parsed);
}
async function configRequest(method = "GET", path = "config", body) {
  const headers = { Authorization: "Bearer " + token };
  if (body !== undefined)
    headers["Content-Type"] =
      path === "config" ? "application/yaml" : "application/json";
  if (method === "PUT" && path === "config") headers["If-Match"] = configEtag;
  $("main").inert = true;
  try {
    const response = await fetch("/api/v2/" + path, {
      method,
      headers,
      ...(body === undefined
        ? {}
        : { body: path === "config" ? body : JSON.stringify(body) }),
    });
    if (response.status === 401) {
      logout();
      throw Error("管理 Token 无效");
    }
    if (!response.ok) {
      let error;
      try {
        error = (await response.json()).error;
      } catch {}
      throw Error(
        response.status === 409
          ? "配置已变更，请重新加载"
          : error || `请求失败 (${response.status})`,
      );
    }
    const source = await response.text(),
      parsed = parseConfig(source);
    const etag = response.headers.get("ETag");
    if (!etag) throw Error("响应缺少 ETag");
    loadFormConfig(parsed);
    yamlText = source;
    configEtag = etag;
    token = parsed.access.admin_token;
    sessionStorage.setItem("curio-token", token);
    dirtySections.clear();
    draftSource = "synced";
    layoutHistory.length = 0;
    Object.values(widgetTypes).forEach((module) => module.clear?.());
  } finally {
    $("main").inert = false;
  }
}
async function saveConfig() {
  syncFormYaml();
  applyRawDraft();
  if (bytes(yamlText) > 16384)
    throw Error("配置超过 16 KiB");
  await configRequest("PUT", "config", yamlText);
  status("已保存");
  render();
}
async function reloadConfig() {
  if (dirty() && !confirm("重新加载并放弃未保存修改？")) return;
  await configRequest();
  resetAutoPage();
  status("");
  render();
}
function download(name, content, type) {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  URL.revokeObjectURL(url);
}
function renderConfig() {
  $("#editor").innerHTML =
    `<section class="panel"><div class="actions editor-heading"><h2>配置</h2><button id="reload-config">重新加载</button><button id="save-config">保存</button></div><div class="actions"><label class="file-button">导入 YAML<input id="import-yaml" type="file" accept=".yaml,.yml,application/yaml,text/yaml" hidden></label><button id="export-yaml">导出 YAML</button></div><label>YAML<textarea id="yaml-editor" spellcheck="false" autocapitalize="off" autocomplete="off">${esc(yamlText)}</textarea></label><h3>访问</h3><label>管理 Token<input id="new-admin-token" type="password" autocomplete="new-password" minlength="16" maxlength="128"></label><div class="actions"><button id="save-admin-token" ${dirty() ? "disabled" : ""}>修改管理 Token</button></div><label>上报 Token<input id="collector-token" readonly value="${esc(configDocument.access.collector_token)}"></label><div class="actions"><button id="copy-collector-token">复制</button><button id="generate-collector-token" ${dirty() ? "disabled" : ""}>生成上报 Token</button></div></section>`;
}
document.addEventListener("input", (event) => {
  if (event.target.id !== "yaml-editor") return;
  yamlText = event.target.value;
  dirtySections.clear();
  markDirty("config");
  layoutHistory.length = 0;
  $("#save-admin-token").disabled = $("#generate-collector-token").disabled =
    true;
  status("");
});
document.addEventListener("change", async (event) => {
  if (event.target.id !== "import-yaml") return;
  const file = event.target.files[0];
  if (!file) return;
  try {
    if (dirty() && !confirm("导入并放弃未保存修改？")) return;
    yamlText = await file.text();
    dirtySections.clear();
    markDirty("config");
    layoutHistory.length = 0;
    renderConfig();
    applyRawDraft();
    renderPreview();
    status("");
  } catch (error) {
    status(error.message);
  }
});

async function fetchState() {
  const r = await fetch("/api/v2/state", {
    headers: { Authorization: "Bearer " + token },
  });
  if (r.status === 401) {
    logout();
    throw Error("管理 Token 无效");
  }
  if (!r.ok) {
    let message;
    try {
      const e = await r.json();
      message = e.error;
    } catch {}
    throw Error(message || `请求失败 (${r.status})`);
  }
  return r.json();
}
function logout() {
  if (display) renderPreview();
  clearTimeout(pollTimer);
  token = "";
  sessionStorage.removeItem("curio-token");
  $("main").hidden = true;
  $("#login").hidden = false;
  $("#token").value = "";
}
async function connect() {
  try {
    clearTimeout(pollTimer);
    await configRequest();
    const loadedState = await fetchState();
    resetAutoPage();
    state = loadedState;
    pollError = "";
    sessionStorage.setItem("curio-token", token);
    $("#login").hidden = true;
    $("main").hidden = false;
    selected = Math.max(
      0,
      display.pages.findIndex((p) => p.id === display.default_page),
    );
    status("");
    render();
    schedule();
  } catch (e) {
    status(e.message);
  }
}
function schedule() {
  clearTimeout(pollTimer);
  if (token && !document.hidden) pollTimer = setTimeout(poll, 2500);
}
async function poll() {
  if (polling || document.hidden || !token) return;
  polling = true;
  try {
    state = await fetchState();
    pollError = "";
    updateStatus();
    renderPreview();
    renderOptions();
    document.querySelectorAll("#editor [data-index]").forEach((editor) => {
      const w = page().widgets[Number(editor.dataset.index)];
      widgetTypes[w.type].refresh?.(w, editor);
    });
    if (tab === "data") renderEditor();
  } catch (e) {
    renderPreview();
    pollError = e.message;
    updateStatus();
  } finally {
    polling = false;
    schedule();
  }
}
function field(label, key, value, options, placeholder = "") {
  return `<label>${label}${options ? `<select data-field="${key}">${options.map(([v, t]) => `<option value="${esc(v)}" ${String(value) === String(v) ? "selected" : ""}>${esc(t)}</option>`).join("")}</select>` : `<input data-field="${key}" value="${esc(value)}" ${placeholder ? `placeholder="${esc(placeholder)}"` : ""} ${key.includes("host") ? 'list="hosts"' : key.includes("source") ? 'list="sources"' : key === "key" ? 'list="keys"' : ""} maxlength="${key === "unit" ? 16 : key === "agent" ? 96 : key === "hosts" ? 195 : 48}">`}</label>`;
}
function renderOptions() {
  const all = hosts();
  $("#hosts").innerHTML = [...new Set(all.map((h) => h.host_id))]
    .map((x) => `<option value="${esc(x)}">`)
    .join("");
  $("#sources").innerHTML = [
    ...new Set([
      ...Object.values(widgetTypes)
        .map((module) => module.defaults().source)
        .filter(Boolean),
      ...all.flatMap((h) => entries(h.sources).map(([id]) => id)),
    ]),
  ]
    .map((x) => `<option value="${esc(x)}">`)
    .join("");
  $("#keys").innerHTML = [
    ...new Set([
      ...Object.values(widgetTypes)
        .map((module) => module.defaults().key)
        .filter(Boolean),
      ...all.flatMap((h) =>
        entries(h.sources).flatMap(([, s]) =>
          entries(s.metrics).map(([id]) => id),
        ),
      ),
    ]),
  ]
    .map((x) => `<option value="${esc(x)}">`)
    .join("");
}
function metricEntries() {
  const records = [];
  for (const h of hosts())
    for (const [source_id, source] of entries(h.sources))
      for (const [key, metric] of entries(source.metrics)) {
        records.push({
          host: h.host_id, source: source_id, key,
          unit: metric.unit,
          label: `${h.host_name || h.host_id} · ${h.host_id} / ${source_id} / ${key}`,
        });
      }
  return records;
}
function widgetOptions(p) {
  return Object.entries(widgetTypes)
    .map(
      ([type, module]) =>
        `<option value="${type}" ${p.widgets.length >= 8 ? "disabled" : ""}>${module.label}</option>`,
    )
    .join("");
}
const alertNumber = (label, key, value, min, max, step = "1") =>
  `<label>${label}<input type="number" data-alert-field="${key}" value="${esc(value)}" min="${min}" max="${max}" step="${step}"></label>`;
const alertDecimal = (label, key, value) =>
  `<label>${label}<input type="number" data-alert-field="${key}" value="${esc(value)}" step="any"></label>`;
const alertText = (label, key, value, list) =>
  `<label>${label}<input data-alert-field="${key}" value="${esc(value)}" list="${list}" maxlength="48"></label>`;
function alertSummary(rule) {
  return `<span class="widget-caption"><strong>${alertTypes[rule.type].label}</strong><span>${esc(alertTypes[rule.type].summary(rule))}</span></span>`;
}

function alertRuleEditor(rule, i) {
  const fields = alertTypes[rule.type].editor(rule);
  return `<div class="widget alert-rule" data-alert-index="${i}"><details class="widget-details" data-alert-details="${esc(rule.id)}" ${openAlerts.has(rule.id) ? "open" : ""}><summary class="widget-summary">${alertSummary(rule)}</summary><div class="widget-body"><label>类型<select data-alert-field="type">${Object.entries(
    alertTypes,
  )
    .map(
      ([value, module]) =>
        `<option value="${value}" ${rule.type === value ? "selected" : ""}>${module.label}</option>`,
    )
    .join(
      "",
    )}</select></label>${fields}<div class="fields alert-delivery">${alertNumber("显示时间（秒，0 为手动关闭）", "duration", rule.duration, 0, 300)}</div><div class="widget-remove"><button class="danger" data-alert-remove="${i}">删除规则</button></div></div></details><div class="alert-toggles"><label class="switch-field"><input type="checkbox" data-alert-field="enabled" ${rule.enabled ? "checked" : ""}><span>启用</span></label><label class="switch-field"><input type="checkbox" data-alert-field="sound" ${rule.sound ? "checked" : ""}><span>声音</span></label><label class="switch-field"><input type="checkbox" data-alert-field="vibrate" ${rule.vibrate ? "checked" : ""}><span>振动</span></label><label class="switch-field"><input type="checkbox" data-alert-field="flash" ${rule.flash ? "checked" : ""}><span>闪光</span></label></div></div>`;
}
function renderAlerts() {
  $("#alert-hosts").innerHTML =
    `<option value="*" label="全部机器"></option>${hosts()
      .map(
        (h) =>
          `<option value="${esc(h.host_id)}" label="${esc(h.host_name || h.host_id)}"></option>`,
      )
      .join("")}`;
  $("#alert-units").innerHTML = [
    ...new Set(
      metricEntries()
        .map((m) => m.unit)
        .filter(Boolean),
    ),
  ]
    .map((unit) => `<option value="${esc(unit)}"></option>`)
    .join("");
  $("#editor").innerHTML =
    `<section class="panel"><div class="actions editor-heading"><h2>提醒</h2><button id="reload-alerts">重新加载</button><button id="save-alerts">保存</button></div><div class="fields">${alertNumber("音量", "volume", alerts.volume, 0, 100)}${alertNumber("振动强度（0 为关闭）", "vibration_strength", alerts.vibration_strength, 0, 100)}</div><label class="switch-field"><input type="checkbox" data-alert-field="flash_enabled" ${alerts.flash_enabled ? "checked" : ""}><span>闪光灯</span></label><div class="section-label components-heading">规则<span>${alerts.rules.length} / 8</span></div>${alerts.rules.map(alertRuleEditor).join("")}${alerts.rules.length < 8 ? '<button class="add-alert" id="add-alert">＋ 添加规则</button>' : ""}</section>`;
}
let alertSerial = 0;
function addAlert() {
  const id = "alert" + Date.now().toString(36) + (++alertSerial).toString(36);
  alerts.rules.push({
    id,
    enabled: true,
    type: Object.keys(alertTypes)[0],
    ...alertTypes[Object.keys(alertTypes)[0]].defaults(),
    ...alertTypes[Object.keys(alertTypes)[0]].initial,
    duration: 10,
    sound: false,
    vibrate: false,
    flash: false,
  });
  openAlerts.add(id);
  markDirty("alerts");
  status("");
  renderEditor();
}
const openAlerts = new Set();
function renderEditor() {
  document.querySelectorAll("[data-tab]").forEach((button) => {
    const active = button.dataset.tab === tab;
    button.classList.toggle("active", active);
    button.setAttribute("aria-current", active ? "page" : "false");
  });
  $("main").classList.toggle(
    "alerts-view",
    tab === "alerts" || tab === "config",
  );
  if (tab === "config") {
    renderConfig();
    return;
  }
  if (tab === "alerts") {
    renderAlerts();
    return;
  }
  if (tab === "data") {
    $("#editor").innerHTML =
      `<section class="panel"><div class="actions"><h2>数据</h2><button id="refresh-data">刷新</button></div>${
        hosts()
          .map(
            (h) =>
              `<h3>${esc(h.host_name || h.host_id)} <small>${esc(h.health || "")}</small></h3><div class="code">${esc(h.host_id)}</div>${entries(
                h.sources,
              )
                .map(
                  ([id, s]) =>
                    `<div class="data-row"><strong>${esc(id)}</strong> · ${esc(s.health || s.status)}${entries(
                      s.metrics,
                    )
                      .map(
                        ([key, m]) =>
                          `<p class="code">${esc(key)} · ${esc(m.value ?? "—")} ${esc(m.unit)}</p>`,
                      )
                      .join("")}${Object.values(widgetTypes)
                      .map((module) => module.catalog?.(s, h) || "")
                      .join("")}</div>`,
                )
                .join("")}`,
          )
          .join("") || "<p>暂无上报</p>"
      }</section>`;
    return;
  }
  if (tab === "device") {
    const wifiRows = network.wifi.map((wifi, i) => `
      <div class="wifi-row" data-wifi-index="${i}">
        <div class="wifi-row-heading"><strong>网络 ${i + 1}</strong><div class="wifi-actions">
          <button type="button" data-wifi-move="up" ${i === 0 ? "disabled" : ""} aria-label="网络 ${i + 1} 上移">上移</button>
          <button type="button" data-wifi-move="down" ${i === network.wifi.length - 1 ? "disabled" : ""} aria-label="网络 ${i + 1} 下移">下移</button>
          <button type="button" data-wifi-remove aria-label="删除网络 ${i + 1}">删除</button>
        </div></div>
        <label>Wi-Fi 名称<input data-wifi-field="ssid" maxlength="32" value="${esc(wifi.ssid)}" autocomplete="off"></label>
        <label>Wi-Fi 密码<input data-wifi-field="password" type="password" maxlength="64" value="${esc(wifi.password ?? "")}" autocomplete="off" placeholder="留空表示开放网络"></label>
        ${wifiIpv4Controls(wifi)}
      </div>`).join("");
    $("#editor").innerHTML =
      `<section class="panel"><h2>设备</h2><label>NTP 服务器<input id="ntp-server" maxlength="127" value="${esc(device.ntp_server)}"></label><label>亮度<input id="brightness" type="number" min="0" max="100" value="${device.brightness}"></label><label class="switch-field"><input id="auto-rotate" type="checkbox" role="switch" ${device.auto_rotate ? "checked" : ""}><span>自动旋转</span></label><label>手动方向<select id="rotation" ${device.auto_rotate ? "disabled" : ""}>${[0, 90, 180, 270].map((x) => `<option value="${x}" ${x === device.rotation ? "selected" : ""}>${x}°</option>`).join("")}</select></label><div class="wifi-heading"><h3>Wi-Fi 网络</h3><button id="add-wifi" ${network.wifi.length >= 4 ? "disabled" : ""}>添加网络</button></div>${wifiRows}<label>主机名<input id="network-hostname" maxlength="32" value="${esc(network.hostname)}"></label><p>网络设置和证书在重启后生效。</p><div class="actions"><button id="save-device">保存</button><button id="reload">重新加载</button><button id="restart-device">重启</button><button id="download-certificate" ${configDocument.tls?.certificate ? "" : "disabled"}>下载证书</button></div></section>`;
    return;
  }
  const p = page();
  if (!p.widgets.some((w) => w.id === selectedWidget))
    selectedWidget = p.widgets[0]?.id || "";
  const i = p.widgets.findIndex((w) => w.id === selectedWidget),
    w = p.widgets[i];
  const axis = (label, key, value, count, start = 1) =>
    field(
      label,
      key,
      value,
      Array.from({ length: count }, (_, n) => [n + start, n + start]),
    );
  $("#editor").innerHTML = `<section class="panel">
    <div class="actions editor-heading"><h2>屏幕</h2><button id="reload">重新加载</button><button id="save">保存</button><button id="undo-layout" ${layoutHistory.length ? "" : "disabled"}>撤销</button></div>
    <label>当前页面<select id="page-select">${display.pages.map((p, i) => `<option value="${i}" ${selected === i ? "selected" : ""}>${esc(p.title)}</option>`).join("")}</select></label>
    <div class="actions page-actions"><button id="add-page" ${display.pages.length >= 4 ? "disabled" : ""}>＋ 添加页面</button><button id="delete-page" ${display.pages.length === 1 ? "disabled" : ""}>删除页面</button><button id="default-page">${p.id === display.default_page ? "默认页面" : "设为默认"}</button></div>
    ${field("页面名称", "page.title", p.title)}
    <div class="section-label components-heading">组件<span>${p.widgets.length} / 8</span></div>
    ${
      w
        ? `<label>当前组件<select id="widget-select">${p.widgets.map((item, n) => `<option value="${esc(item.id)}" ${item.id === selectedWidget ? "selected" : ""}>${n + 1}. ${esc(item.title || item.type)}</option>`).join("")}</select></label>
    <div class="widget" data-index="${i}"><h3>${esc(w.title || w.type)}</h3>
    <div class="fields layout-fields">${axis("列", "rect.col", w.at[0] + 1, 4)}${axis("行", "rect.row", w.at[1] + 1, 6)}${axis("宽度（列）", "rect.width", w.size[0], 5 - minimum(w.type)[0], minimum(w.type)[0])}${axis("高度（行）", "rect.height", w.size[1], 7 - minimum(w.type)[1], minimum(w.type)[1])}</div>
    <div class="widget-body">${field("标题", "title", w.title, undefined, widgetTypes[w.type].titlePlaceholder || "")}${widgetTypes[
      w.type
    ].editor(
      w,
    )}<div class="widget-remove"><button class="danger" data-remove="${i}">删除组件</button></div></div></div>`
        : ""
    }
    <div class="actions add-widget-row"><select id="new-type" ${p.widgets.length >= 8 ? "disabled" : ""}>${widgetOptions(p)}</select><button id="add-widget" ${p.widgets.length >= 8 ? "disabled" : ""}>＋ 添加组件</button></div>
  </section>`;
}

function metricHTML(b, unit) {
  const r = resolve(b, unit);
  return `<span class="${r.value === undefined ? "metric-status" : "metric-value"}">${esc(r.text)}</span>`;
}
function renderPreview(items) {
  if (!display) return;
  items ??= page().widgets;
  if (gesture && items === page().widgets) return;
  const p = page(),
    editing = tab === "screen";
  $("#preview-detail").textContent = p.widgets.some(
    (w) => widgetTypes[w.type].browserTime,
  )
    ? "480 × 480 · 浏览器时间"
    : "480 × 480";
  let html = `<div class="screen-head"><button id="screen-page">${esc(p.title)} ▾</button><span class="screen-icons" aria-hidden="true"><svg viewBox="0 0 78 20" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M1 7Q9 -1 17 7M4 10Q9 5 14 10M7 13Q9 11 11 13"/><circle cx="9" cy="16" r="1"/><path d="M27 14V9a6 6 0 0 1 12 0v5M27 10h3v7h-3zM36 10h3v7h-3z"/><rect x="50" y="5" width="21" height="11" rx="2"/><path d="M73 8v5M53 8h12v5H53z"/></svg>⚙</span></div>`;
  if (editing)
    for (let row = 0; row < 6; row++)
      for (let col = 0; col < 4; col++) {
        const r = pixelRect({ at: [col, row], size: [1, 1] });
        html += `<div class="grid-cell" style="left:${r.x}px;top:${r.y}px;width:${r.width}px;height:${r.height}px"></div>`;
      }
  for (const w of items || []) {
    const { x, y, width, height } = pixelRect(w);
    html += `<div class="card ${widgetTypes[w.type].classes(w)} ${editing && w.id === selectedWidget ? "selected" : ""}" data-widget-id="${esc(w.id)}" style="left:${x}px;top:${y}px;width:${width}px;height:${height}px">`;
    html += widgetTypes[w.type].preview(w, { width, height });
    if (editing && selectedWidget === w.id)
      html += '<span class="resize-handle" aria-label="调整尺寸"></span>';
    html += "</div>";
  }
  $("#screen").innerHTML = html;
  document.querySelectorAll(".metric-value").forEach((value) => {
    let size = parseFloat(getComputedStyle(value).fontSize);
    while (value.scrollWidth > value.clientWidth && size > 14)
      value.style.fontSize = `${--size}px`;
  });
  resize();
}
function resize() {
  const scale = Math.min(1, $("aside").clientWidth / 480 - 40 / 480);
  $("#screen").style.transform = `scale(${scale})`;
  $("#frame").style.width = `${480 * scale}px`;
  $("#frame").style.height = `${480 * scale}px`;
}
function render() {
  renderEditor();
  renderOptions();
  renderPreview();
}
function addWidget() {
  const p = page(),
    type = $("#new-type").value;
  if (p.widgets.length >= 8 || !minimum(type)) return;
  const size = minimum(type),
    used = p.widgets.reduce((mask, w) => mask | rectMask(w), 0);
  let at;
  for (let row = 0; row <= 6 - size[1] && !at; row++)
    for (let col = 0; col <= 4 - size[0]; col++)
      if (!(used & rectMask({ at: [col, row], size }))) {
        at = [col, row];
        break;
      }
  if (!at) {
    status("没有可用位置");
    return;
  }
  const w = {
    id: "w" + Date.now().toString(36),
    type,
    at,
    size: [...size],
    ...widgetTypes[type].defaults(),
    ...widgetTypes[type].initial,
  };
  rememberLayout();
  p.widgets.push(w);
  selectedWidget = w.id;
  markDisplayDirty();
  render();
}
$("#login").onsubmit = (e) => {
  e.preventDefault();
  token = $("#token").value.trim();
  connect();
};
$("#logout").onclick = logout;
document.addEventListener(
  "toggle",
  (event) => {
    const id = event.target.dataset?.alertDetails;
    if (id) event.target.open ? openAlerts.add(id) : openAlerts.delete(id);
  },
  true,
);
document.addEventListener("change", (e) => {
  const t = e.target;
  if (
    [...Object.values(widgetTypes), ...Object.values(alertTypes)].some(
      (module) => module.change?.(t),
    )
  )
    return;
  if (t.hasAttribute("data-alert-field")) {
    const key = t.dataset.alertField;
    const rule = t.closest("[data-alert-index]")
      ? alerts.rules[Number(t.closest("[data-alert-index]").dataset.alertIndex)]
      : alerts;
    if (key === "type") {
      const { id, enabled, duration, sound, vibrate, flash } = rule;
      Object.assign(rule, {
        id,
        enabled,
        type: t.value,
        ...alertTypes[t.value].defaults(),
        ...alertTypes[t.value].initial,
        duration,
        sound,
        vibrate,
        flash,
      });
      for (const field of Object.keys(rule))
        if (
          ![
            "id",
            "enabled",
            "type",
            "duration",
            "sound",
            "vibrate",
            "flash",
            ...Object.keys(alertTypes[t.value].types),
          ].includes(field)
        )
          delete rule[field];
      openAlerts.add(rule.id);
    } else if (t.type === "checkbox") rule[key] = t.checked;
    else if (t.type === "number")
      rule[key] = t.value === "" ? NaN : Number(t.value);
    else rule[key] = t.value.trim();
    markDirty("alerts");
    status("");
    if (key === "type") renderEditor();
    else if (rule !== alerts) {
      t.closest(".alert-rule").querySelector(".widget-summary").innerHTML =
        alertSummary(rule);
    }
    return;
  }
  if (t.id === "ntp-server") {
    device.ntp_server = t.value.trim();
    markDirty("device");
    status("");
    return;
  }
  if (t.hasAttribute("data-ipv4-mode") || t.dataset.ipv4Field) {
    const row = t.closest("[data-wifi-index]");
    editWifiIpv4(network.wifi[Number(row.dataset.wifiIndex)], t, row);
    markDirty("device");
    status("");
    return;
  }
  if (t.dataset.wifiField || t.id === "network-hostname") {
    if (t.dataset.wifiField)
      network.wifi[Number(t.closest("[data-wifi-index]").dataset.wifiIndex)][t.dataset.wifiField] = t.value;
    else network.hostname = t.value.trim();
    $("#download-certificate").disabled =
      !configDocument.tls?.certificate ||
      network.hostname !== certificateHostname;
    markDirty("device");
    status("");
    return;
  }
  if (t.id === "brightness" || t.id === "rotation" || t.id === "auto-rotate") {
    if (t.id === "auto-rotate") {
      device.auto_rotate = t.checked;
      $("#rotation").disabled = t.checked;
    } else device[t.id] = Number(t.value);
    markDirty("device");
    status("");
    return;
  }
  if (t.id === "page-select") {
    selected = Number(t.value);
    resetAutoPage();
    render();
    return;
  }
  if (t.id === "widget-select") {
    selectedWidget = t.value;
    render();
    return;
  }
  if (!t.dataset.field) return;
  const key = t.dataset.field,
    p = page();
  if (key === "page.title") {
    if (p.title === t.value) return;
    rememberLayout();
    p.title = t.value;
  } else {
    const w = p.widgets[Number(t.closest("[data-index]").dataset.index)];
    if (key.startsWith("rect.")) {
      const next = clone(w),
        value = Number(t.value);
      if (key === "rect.col") next.at[0] = value - 1;
      if (key === "rect.row") next.at[1] = value - 1;
      if (key === "rect.width") next.size[0] = value;
      if (key === "rect.height") next.size[1] = value;
      commitLayout(layoutProposal(next));
      return;
    }
    const value =
      t.type === "checkbox"
        ? t.checked
        : widgetTypes[w.type].types[key] === "array"
          ? t.value
              .split(",")
              .map((x) => x.trim())
              .filter(Boolean)
          : t.value.trim();
    if (JSON.stringify(w[key]) === JSON.stringify(value)) return;
    rememberLayout();
    w[key] = value;
    widgetTypes[w.type].invalidate?.(w);
  }
  resetAutoPage();
  markDisplayDirty();
  renderPreview();
});
document.addEventListener("click", async (e) => {
  const t = e.target.closest("button");
  if (!t) return;
  if (Object.values(widgetTypes).some((module) => module.click?.(t))) return;
  try {
    if (t.dataset.tab) {
      if (!display || !token) return;
      if (tab === "config") applyRawDraft();
      else syncFormYaml();
      tab = t.dataset.tab;
      resetAutoPage();
      render();
    } else if (t.id === "undo-layout") {
      const old = layoutHistory.pop();
      if (!old) return;
      display = configDocument.screen = old.display;
      selected = old.selected;
      selectedWidget = old.selectedWidget;
      markDirty("screen");
      syncFormYaml();
      if (!old.dirty) dirtySections.delete("screen");
      status("");
      render();
    } else if (t.id === "add-alert") addAlert();
    else if (t.hasAttribute("data-alert-remove")) {
      const removed = alerts.rules.splice(Number(t.dataset.alertRemove), 1)[0];
      openAlerts.delete(removed.id);
      markDirty("alerts");
      status("");
      renderEditor();
    } else if (t.id === "add-wifi") {
      if (network.wifi.length >= 4) return;
      network.wifi.push({ ssid: "", password: "" });
      markDirty("device");
      status("");
      renderEditor();
      $(`.wifi-row[data-wifi-index="${network.wifi.length - 1}"] [data-wifi-field=ssid]`)?.focus();
    } else if (t.hasAttribute("data-wifi-remove")) {
      network.wifi.splice(Number(t.closest("[data-wifi-index]").dataset.wifiIndex), 1);
      markDirty("device");
      status("");
      renderEditor();
    } else if (t.dataset.wifiMove) {
      const i = Number(t.closest("[data-wifi-index]").dataset.wifiIndex);
      const j = i + (t.dataset.wifiMove === "up" ? -1 : 1);
      if (j < 0 || j >= network.wifi.length) return;
      [network.wifi[i], network.wifi[j]] = [network.wifi[j], network.wifi[i]];
      markDirty("device");
      status("");
      renderEditor();
    } else if (t.id === "add-widget") addWidget();
    else if (t.hasAttribute("data-remove")) {
      rememberLayout();
      page().widgets.splice(Number(t.dataset.remove), 1);
      render();
      markDisplayDirty();
    } else if (t.id === "add-page") {
      if (display.pages.length >= 4) return;
      rememberLayout();
      const id = "page" + Date.now().toString(36);
      display.pages.push({
        id,
        title: "Page " + (display.pages.length + 1),
        widgets: [],
      });
      selected = display.pages.length - 1;
      render();
      markDisplayDirty();
    } else if (t.id === "delete-page") {
      if (display.pages.length === 1) return;
      rememberLayout();
      const old = page().id;
      display.pages.splice(selected, 1);
      selected = 0;
      if (display.default_page === old) display.default_page = page().id;
      render();
      markDisplayDirty();
    } else if (t.id === "default-page") {
      if (display.default_page === page().id) return;
      rememberLayout();
      display.default_page = page().id;
      renderEditor();
      markDisplayDirty();
    } else if (["save", "save-device", "save-config", "save-alerts"].includes(t.id)) {
      await saveConfig();
    } else if (["reload", "reload-config", "reload-alerts"].includes(t.id)) {
      await reloadConfig();
    } else if (t.id === "restart-device") {
      if (dirty()) throw Error("请先保存或重新加载配置");
      if (!confirm("重启设备？")) return;
      const response = await fetch("/api/v2/restart", {
        method: "POST",
        headers: { Authorization: "Bearer " + token },
      });
      if (response.status === 401) {
        logout();
        throw Error("管理 Token 无效");
      }
      if (!response.ok) throw Error(`重启失败 (${response.status})`);
      status("设备正在重启，请稍后重新连接");
    } else if (t.id === "download-certificate") {
      const certificate = configDocument.tls?.certificate;
      if (!certificate) return;
      download(`${network.hostname}.crt`, certificate, "application/x-pem-file");
    } else if (t.id === "export-yaml") {
      syncFormYaml();
      download("curio.yaml", yamlText, "application/yaml");
    } else if (t.id === "copy-collector-token") {
      const value = configDocument.access.collector_token;
      if (navigator.clipboard) await navigator.clipboard.writeText(value);
      else {
        $("#collector-token").select();
        if (!document.execCommand("copy")) throw Error("复制失败");
      }
      status("已复制");
    } else if (
      t.id === "save-admin-token" ||
      t.id === "generate-collector-token"
    ) {
      if (dirty()) return;
      if (t.id === "save-admin-token") {
        const value = $("#new-admin-token").value;
        if (!/^[!-~]{16,128}$/.test(value))
          throw Error("管理 Token 须为 16–128 位非空白 ASCII 字符");
        await configRequest("PUT", "tokens/admin", { token: value });
      } else await configRequest("POST", "tokens/collector");
      status("");
      render();
    } else if (t.id === "refresh-data") {
      state = await fetchState();
      render();
    } else if (t.id === "screen-page") {
      selected = (selected + 1) % display.pages.length;
      resetAutoPage();
      render();
    }
  } catch (e) {
    status(e.message);
  }
});
const preview = $("#screen");
preview.tabIndex = 0;
preview.setAttribute("role", "region");
preview.setAttribute("aria-label", "屏幕预览");
let suppressClickUntil = 0;
preview.addEventListener("pointerdown", (event) => {
  if (
    tab !== "screen" ||
    !event.isPrimary ||
    event.button !== 0 ||
    event.target.closest("button")
  )
    return;
  const card = event.target.closest(".card");
  if (!card) return;
  selectedWidget = card.dataset.widgetId;
  const w = page().widgets.find((w) => w.id === selectedWidget),
    scale = preview.getBoundingClientRect().width / 480;
  gesture = {
    id: event.pointerId,
    x: event.clientX,
    y: event.clientY,
    original: clone(w),
    resize: !!event.target.closest(".resize-handle"),
    scale,
  };
  preview.setPointerCapture(event.pointerId);
  document
    .querySelectorAll(".card")
    .forEach((item) =>
      item.classList.toggle(
        "selected",
        item.dataset.widgetId === selectedWidget,
      ),
    );
  renderEditor();
  event.preventDefault();
});
preview.addEventListener("pointermove", (event) => {
  if (!gesture || event.pointerId !== gesture.id) return;
  const dx = Math.round((event.clientX - gesture.x) / (116 * gesture.scale)),
    dy = Math.round((event.clientY - gesture.y) / ((416 / 6) * gesture.scale));
  const next = clone(gesture.original);
  if (gesture.resize) {
    next.size[0] += dx;
    next.size[1] += dy;
  } else {
    next.at[0] += dx;
    next.at[1] += dy;
  }
  if (JSON.stringify(gesture.next) === JSON.stringify(next)) return;
  gesture.next = next;
  gesture.result = layoutProposal(next);
  renderPreview(gesture.result || clone(page().widgets));
  if (!gesture.result) {
    const r = pixelRect(next),
      ghost = document.createElement("div");
    ghost.className = "invalid-placement";
    Object.assign(ghost.style, {
      left: `${r.x}px`,
      top: `${r.y}px`,
      width: `${Math.max(10, r.width)}px`,
      height: `${Math.max(10, r.height)}px`,
    });
    preview.append(ghost);
  }
  suppressClickUntil = performance.now() + 500;
});
preview.addEventListener("pointerup", (event) => {
  if (!gesture || event.pointerId !== gesture.id) return;
  const { next, result } = gesture;
  gesture = null;
  if (next) commitLayout(result);
  else render();
});
preview.addEventListener("pointercancel", () => {
  gesture = null;
  render();
});
preview.addEventListener(
  "click",
  (event) => {
    if (performance.now() < suppressClickUntil) {
      event.preventDefault();
      event.stopImmediatePropagation();
    }
  },
  true,
);
function resetAutoPage() {
  Object.values(widgetTypes).forEach((module) => module.reset?.());
}
let previewClockSecond = -1;
setInterval(() => {
  if (!display || document.hidden || tab !== "screen" || !token || gesture)
    return;
  const second = Math.floor(Date.now() / 1000);
  const changed = page()
    .widgets.map((w) =>
      widgetTypes[w.type].tick?.(w, second !== previewClockSecond),
    )
    .some(Boolean);
  previewClockSecond = second;
  if (changed) renderPreview();
}, 250);
document.addEventListener("visibilitychange", () => {
  clearTimeout(pollTimer);
  resetAutoPage();
  if (!document.hidden) poll();
});
addEventListener("resize", resize);
if (token) connect();
