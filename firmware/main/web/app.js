"use strict";
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
  yamlDirty = false,
  display,
  device,
  alerts,
  state = { hosts: [] },
  tab = "screen",
  selected = 0,
  selectedWidget = "",
  polling = false,
  pollTimer,
  displayDirty = false,
  deviceDirty = false,
  alertsDirty = false,
  message = "",
  pollError = "",
  gesture = null;
const previewAgents = new Map(),
  layoutHistory = [];
const minimum = (type) =>
  ({ metric: [1, 1], status: [1, 1], clock: [1, 1], machine: [2, 2], agents: [3, 3] })[type];
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
function agentPreview(w) {
  const key = `${page().id}/${w.id}`;
  if (!previewAgents.has(key))
    previewAgents.set(key, {
      page: 0,
      filter: w.filter,
      due: performance.now() + 10000,
    });
  return previewAgents.get(key);
}
function rememberLayout() {
  layoutHistory.push({
    display: clone(display),
    selected,
    selectedWidget,
    dirty: displayDirty,
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
    yamlDirty ? "配置未保存" : "",
    displayDirty ? "屏幕未保存" : "",
    deviceDirty ? "设备未保存" : "",
    alertsDirty ? "提醒未保存" : "",
  ]
    .filter(Boolean)
    .join(" · ");
}
function status(s) {
  message = s;
  updateStatus();
}
function markDisplayDirty() {
  displayDirty = true;
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
  const rounded = (n, scale) => Math.sign(n) * Math.round(Math.abs(n) * scale) / scale;
  const decimal = (n) => String(Number((Math.abs(n) < 1e14 ? rounded(n, 10) : n).toFixed(1)));
  const scaled = (base, labels) => {
    let i = 0;
    while (Math.abs(value) >= base && i < labels.length - 1) {
      value /= base;
      i++;
    }
    return `${decimal(value)} ${labels[i]}`;
  };
  if (unit === "bytes" || unit === "bytes_per_second")
    return scaled(1024, ["B", "KiB", "MiB", "GiB", "TiB", "PiB"].map((u) => u + (unit === "bytes_per_second" ? "/s" : "")));
  if (unit === "watts") return scaled(1000, ["W", "kW", "MW"]);
  if (unit === "hertz") return scaled(1000, ["Hz", "kHz", "MHz", "GHz"]);
  if (unit === "seconds" || unit === "milliseconds") {
    if (unit === "milliseconds") value /= 1000;
    if (value !== 0 && Math.abs(value) < 1) return `${decimal(value * 1000)} ms`;
    let label = "s";
    for (const [base, next] of [[60, "min"], [60, "h"], [24, "d"]]) {
      if (Math.abs(value) < base) break;
      value /= base;
      label = next;
    }
    return `${decimal(value)} ${label}`;
  }
  if (unit === "count") return String(Math.abs(value) < 1e14 ? rounded(value, 1) : value);
  return `${decimal(value)}${unit === "percent" ? "%" : unit === "number" || !unit ? "" : ` ${{celsius: "C"}[unit] || unit}`}`;
}
function resolve(b, unit) {
  const h = host(b.host_id);
  if (!h) return { text: "Waiting" };
  const s = source(h, b.source_id), problem = health(h, s);
  if (problem) return { text: problem };
  if (!s) return { text: "Not reported" };
  if (s.kind !== "metrics") return { text: "Type mismatch" };
  const m = s.metrics?.[b.key];
  if (!m) return { text: "Not reported" };
  if (unit && m.unit !== unit) return { text: "Type mismatch" };
  if (m.age_ms > (s.max_age_ms ?? 10000)) return { text: "Stale" };
  if (typeof m.value !== "number" || !Number.isFinite(m.value)) return { text: "Unavailable" };
  return { value: m.value, text: formatMetric(m.value, m.unit) };
}
function resolveStatus(w) {
  const h = host(w.host_id);
  if (!h) return { text: "Waiting" };
  const s = source(h, w.source_id), problem = health(h, s);
  if (problem) return { text: problem };
  if (!s) return { text: "Not reported" };
  if (s.kind !== "status") return { text: "Type mismatch" };
  if (s.sample_age_ms > (s.max_age_ms ?? 10000)) return { text: "Stale" };
  return s.state || { text: "Not reported" };
}
function clockOffset(value) {
  if (typeof value !== "string" || !/^[+-]\d{2}:[0-5]\d$/.test(value))
    throw Error("UTC 偏移须为 ±HH:MM（-12:00 至 +14:00）");
  const minutes = (Number(value.slice(1, 3)) * 60 + Number(value.slice(4))) * (value[0] === "-" ? -1 : 1);
  if (minutes < -720 || minutes > 840)
    throw Error("UTC 偏移须为 -12:00 至 +14:00");
  return minutes;
}
function validateNtp(value) {
  if (typeof value !== "string" || !value || value.length > 127 ||
      !/^[A-Za-z0-9.:-]+$/.test(value))
    throw Error("NTP 服务器须为主机名或 IP，最多 127 字符");
}
function clockTitle(w, width) {
  if (w.title) return w.title;
  let offset;
  try { offset = clockOffset(w.utc_offset); } catch { return "Time"; }
  const minutes = Math.abs(offset),
    zone = offset ? `${offset < 0 ? "-" : "+"}${Math.floor(minutes / 60)}${minutes % 60 ? ":" + String(minutes % 60).padStart(2, "0") : ""}` : "",
    title = `Time (UTC${zone})`,
    context = document.createElement("canvas").getContext("2d");
  context.font = `14px ${getComputedStyle($("#screen")).fontFamily}`;
  return context.measureText(title).width <= width - 16 ? title : "Time";
}
function clockTypography(w, width, height) {
  const context = document.createElement("canvas").getContext("2d"),
    family = getComputedStyle($("#screen")).fontFamily,
    available = height - (w.size[1] > 1 ? 56 : 28);
  for (const size of [48, 32, 26, 18, 14]) {
    context.font = `${size}px ${family}`;
    const line = Math.ceil(size * 1.2);
    if (context.measureText(w.seconds ? "88:88:88" : "88:88").width <= width - 16 && line <= available)
      return `font-size:${size}px;line-height:${line}px;top:${25 + Math.floor((available - line) / 2)}px`;
  }
  return "font-size:14px;line-height:17px;top:25px";
}
function clockParts(w, epoch = Date.now()) {
  const date = new Date(epoch + clockOffset(w.utc_offset) * 60000);
  const iso = date.toISOString();
  return { time: iso.slice(11, w.seconds ? 19 : 16), date: iso.slice(0, 10) };
}
function validateDisplay() {
  const bytes = (value) => new TextEncoder().encode(value).length;
  const check = (value, name, max, id = false, optional = false) => {
    if (typeof value !== "string" || (!value && !optional))
      throw Error(`${name}不能为空`);
    if (bytes(value) > max) throw Error(`${name}最多 ${max} 字节`);
    if (id && value && !/^[A-Za-z0-9._-]+$/.test(value))
      throw Error(`${name}格式错误`);
  };
  if (display.pages.length < 1 || display.pages.length > 4)
    throw Error("须有 1–4 个页面");
  for (const p of display.pages) {
    validateLayout(p);
    check(p.title, "页面名称", 48, false, true);
    for (const w of p.widgets) {
      check(w.title, "Widget 标题", 48, false, true);
      if (w.type === "metric") {
        check(w.binding.host_id, "机器 ID", 48, true);
        check(w.binding.source_id, "来源 ID", 48, true);
        check(w.binding.key, "指标键", 48, true);
        check(w.unit, "单位", 16, false, true);
        if (/[^\x21-\x7e]/.test(w.unit)) throw Error("单位格式错误");
      } else if (w.type === "status") {
        check(w.host_id, "机器 ID", 48, true);
        check(w.source_id, "来源 ID", 48, true);
      } else if (w.type === "clock") clockOffset(w.utc_offset);
      else if (w.type === "machine") check(w.host_id, "机器 ID", 48, true);
      else {
        if (w.hosts.length > 4) throw Error("最多 4 台机器");
        if (w.hosts.includes("*") && w.hosts.length !== 1)
          throw Error("全部机器不能与机器 ID 混用");
        if (new Set(w.hosts).size !== w.hosts.length)
          throw Error("机器 ID 重复");
        for (const h of w.hosts) if (h !== "*") check(h, "机器 ID", 48, true);
        check(w.source_id, "来源 ID", 48, true);
        check(w.project, "项目", 48, false, true);
        check(w.agent, "Agent ID", 96, false, true);
      }
    }
  }
}
function dirty() {
  return yamlDirty || displayDirty || deviceDirty || alertsDirty;
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
  keys(value, ["version", "access", "device", "screen", "alerts"], "config");
  keys(value.access, ["admin_token", "collector_token"], "access");
  if (value.device !== undefined)
    keys(value.device, ["brightness", "rotation", "auto_rotate", "ntp_server"], "device");
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
      const fields =
        widget?.type === "clock"
          ? ["utc_offset", "seconds"]
          : widget?.type === "agents"
          ? ["hosts", "source", "filter", "auto_page", "project", "agent"]
          : widget?.type === "status"
            ? ["host", "source"]
            : widget?.type === "metric"
            ? ["host", "source", "key", "unit"]
            : ["host"];
      keys(
        widget,
        ["id", "type", "title", "at", "size", ...fields],
        `${path}.widgets[${j}]`,
      );
      checkId(widget.id ?? `${widget.type}-${j + 1}`, widgetIds, `${path}.widgets[${j}].id`);
      if (widget.type === "clock") clockOffset(widget.utc_offset ?? "+00:00");
    }
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
  for (const [i, rule] of (value.alerts?.rules ?? []).entries()) {
    const fields =
      rule?.type === "agent_done"
        ? ["source", "include_idle"]
        : rule?.type === "metric_high"
          ? ["source", "key", "unit", "threshold", "recover", "hold"]
          : ["hold"];
    keys(
      rule,
      [
        "id",
        "type",
        "enabled",
        "host",
        "duration",
        "sound",
        "vibrate",
        "flash",
        ...fields,
      ],
      `alerts.rules[${i}]`,
    );
  }
  const types = {
    version: "number", brightness: "number", rotation: "number",
    volume: "number", vibration_strength: "number", duration: "number",
    threshold: "number", recover: "number", hold: "number",
    auto_rotate: "boolean", flash_enabled: "boolean", auto_page: "boolean",
    enabled: "boolean", sound: "boolean", vibrate: "boolean", flash: "boolean",
    include_idle: "boolean", seconds: "boolean", utc_offset: "string", ntp_server: "string",
  };
  function scalarTypes(object, path) {
    for (const [key, item] of Object.entries(object)) {
      const name = `${path}.${key}`;
      if (item === null) throw Error(`${name} 不能为 null`);
      if (types[key] && typeof item !== types[key])
        throw Error(`${name} 类型错误`);
      if (typeof item === "object") scalarTypes(item, name);
    }
  }
  scalarTypes(value, "config");
  validateNtp(value.device?.ntp_server ?? "pool.ntp.org");
  if (value.version !== undefined && value.version !== 1)
    throw Error("version 须为 1");
  if (value.device?.rotation !== undefined &&
      ![0, 90, 180, 270].includes(value.device.rotation))
    throw Error("device.rotation 须为 0、90、180 或 270");
  if (value.alerts?.vibration_strength !== undefined &&
      (!Number.isInteger(value.alerts.vibration_strength) ||
       value.alerts.vibration_strength < 0 || value.alerts.vibration_strength > 100))
    throw Error("alerts.vibration_strength 须为 0–100 的整数");
  return value;
}
function projectConfig(value) {
  const projectedDisplay = {
    default_page: value.screen.default_page ?? value.screen.pages[0].id,
    pages: value.screen.pages.map((p) => ({
      id: p.id,
      title: p.title ?? p.id,
      widgets: p.widgets.map((w, i) => {
        const projected = {
          id: w.id ?? `${w.type}-${i + 1}`,
          type: w.type,
          title:
            w.title ??
            { agents: "Agents", metric: "Metric", status: "Status", clock: "", machine: "Machine" }[
              w.type
            ] ??
            "",
          at: [...w.at],
          size: [...w.size],
        };
        if (w.type === "agents")
          Object.assign(projected, {
            hosts: w.hosts ?? ["*"],
            source_id: w.source ?? "herdr",
            filter: w.filter ?? "all",
            auto_page: w.auto_page ?? true,
            project: w.project ?? "",
            agent: w.agent ?? "",
          });
        else if (w.type === "metric")
          Object.assign(projected, {
            binding: { host_id: w.host, source_id: w.source, key: w.key },
            unit: w.unit ?? "",
          });
        else if (w.type === "status") Object.assign(projected, { host_id: w.host, source_id: w.source });
        else if (w.type === "clock") Object.assign(projected, { utc_offset: w.utc_offset ?? "+00:00", seconds: w.seconds ?? false });
        else if (w.type === "machine") projected.host_id = w.host;
        else throw Error(`Widget 类型无效: ${w.type}`);
        return projected;
      }),
    })),
  };
  const projectedDevice = {
    ntp_server: "pool.ntp.org",
    brightness: 75,
    rotation: 0,
    auto_rotate: false,
    ...value.device,
  };
  const projectedAlerts = {
    volume: value.alerts?.volume ?? 35,
    vibration_strength: value.alerts?.vibration_strength ?? 60,
    flash_enabled: value.alerts?.flash_enabled ?? false,
    rules: (value.alerts?.rules ?? []).map((r, i) => {
      const projected = {
        id: r.id ?? `rule-${i + 1}`,
        type: r.type,
        enabled: r.enabled ?? true,
        host_id: r.host ?? "*",
        display_s: r.duration ?? 10,
        sound: r.sound ?? false,
        vibrate: r.vibrate ?? false,
        flash: r.flash ?? false,
      };
      if (r.type === "agent_done")
        Object.assign(projected, {
          source_id: r.source ?? "herdr",
          include_idle: r.include_idle ?? true,
        });
      else if (r.type === "metric_high")
        Object.assign(projected, {
          source_id: r.source,
          key: r.key,
          unit: r.unit,
          threshold: r.threshold,
          recover: r.recover,
          hold_s: r.hold ?? 60,
        });
      else if (r.type === "host_offline") projected.hold_s = r.hold ?? 30;
      else throw Error(`提醒类型无效: ${r.type}`);
      return projected;
    }),
  };
  display = projectedDisplay;
  device = projectedDevice;
  alerts = projectedAlerts;
  selected = Math.max(0, Math.min(selected, display.pages.length - 1));
}
function configFromForms() {
  return {
    ...configDocument,
    device: {
      brightness: device.brightness,
      rotation: device.rotation,
      auto_rotate: device.auto_rotate,
      ...(device.ntp_server !== "pool.ntp.org" ? { ntp_server: device.ntp_server } : {}),
    },
    screen: {
      default_page: display.default_page,
      pages: display.pages.map((p) => ({
        id: p.id,
        title: p.title,
        widgets: p.widgets.map((w) => {
          const value = {
            id: w.id,
            type: w.type,
            ...(w.type !== "clock" || w.title ? { title: w.title } : {}),
            at: [...w.at],
            size: [...w.size],
          };
          if (w.type === "agents")
            Object.assign(value, {
              hosts: w.hosts,
              source: w.source_id,
              filter: w.filter,
              auto_page: w.auto_page,
              project: w.project,
              agent: w.agent,
            });
          else if (w.type === "metric")
            Object.assign(value, {
              host: w.binding.host_id,
              source: w.binding.source_id,
              key: w.binding.key,
              unit: w.unit,
            });
          else if (w.type === "clock") Object.assign(value, {
            ...(w.utc_offset !== "+00:00" ? { utc_offset: w.utc_offset } : {}),
            ...(w.seconds ? { seconds: true } : {}),
          });
          else {
            value.host = w.host_id;
            if (w.type === "status") value.source = w.source_id;
          }
          return value;
        }),
      })),
    },
    alerts: {
      volume: alerts.volume,
      vibration_strength: alerts.vibration_strength,
      flash_enabled: alerts.flash_enabled,
      rules: alerts.rules.map((r) => {
        const value = {
          id: r.id,
          type: r.type,
          enabled: r.enabled,
          host: r.host_id,
          duration: r.display_s,
          sound: r.sound,
          ...(r.vibrate ? { vibrate: true } : {}),
          ...(r.flash ? { flash: true } : {}),
        };
        if (r.type === "agent_done")
          Object.assign(value, {
            source: r.source_id,
            include_idle: r.include_idle,
          });
        else if (r.type === "metric_high")
          Object.assign(value, {
            source: r.source_id,
            key: r.key,
            unit: r.unit,
            threshold: r.threshold,
            recover: r.recover,
            hold: r.hold_s,
          });
        else value.hold = r.hold_s;
        return value;
      }),
    },
  };
}
function syncFormYaml() {
  if (!displayDirty && !deviceDirty && !alertsDirty) return;
  configDocument = configFromForms();
  yamlText = jsyaml.dump(configDocument, {
    noRefs: true,
    lineWidth: -1,
    sortKeys: false,
  });
}
function applyRawDraft() {
  const parsed = parseConfig(yamlText);
  previewAgents.clear();
  projectConfig(parsed);
  configDocument = parsed;
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
    projectConfig(parsed);
    configDocument = parsed;
    yamlText = source;
    configEtag = etag;
    token = parsed.access.admin_token;
    sessionStorage.setItem("curio-token", token);
    displayDirty = deviceDirty = alertsDirty = yamlDirty = false;
    layoutHistory.length = 0;
    previewAgents.clear();
  } finally {
    $("main").inert = false;
  }
}
async function saveConfig() {
  syncFormYaml();
  applyRawDraft();
  validateDisplay();
  validateAlerts();
  if (
    !Number.isInteger(device.brightness) ||
    device.brightness < 0 ||
    device.brightness > 100
  )
    throw Error("亮度须为 0–100");
  if (new TextEncoder().encode(yamlText).length > 16384)
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
function renderConfig() {
  $("#editor").innerHTML =
    `<section class="panel"><div class="actions editor-heading"><h2>配置</h2><button id="reload-config">重新加载</button><button id="save-config">保存</button></div><div class="actions"><label class="file-button">导入 YAML<input id="import-yaml" type="file" accept=".yaml,.yml,application/yaml,text/yaml" hidden></label><button id="export-yaml">导出 YAML</button></div><label>YAML<textarea id="yaml-editor" spellcheck="false" autocapitalize="off" autocomplete="off">${esc(yamlText)}</textarea></label><h3>访问</h3><label>管理 Token<input id="new-admin-token" type="password" autocomplete="new-password" minlength="16" maxlength="128"></label><div class="actions"><button id="save-admin-token" ${dirty() ? "disabled" : ""}>修改管理 Token</button></div><label>上报 Token<input id="collector-token" readonly value="${esc(configDocument.access.collector_token)}"></label><div class="actions"><button id="copy-collector-token">复制</button><button id="generate-collector-token" ${dirty() ? "disabled" : ""}>生成上报 Token</button></div></section>`;
}
document.addEventListener("input", (event) => {
  if (event.target.id !== "yaml-editor") return;
  yamlText = event.target.value;
  yamlDirty = true;
  layoutHistory.length = 0;
  displayDirty = deviceDirty = alertsDirty = false;
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
    yamlDirty = true;
    layoutHistory.length = 0;
    displayDirty = deviceDirty = alertsDirty = false;
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
    displayDirty = deviceDirty = alertsDirty = false;
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
    document.querySelectorAll("[data-metric-select]").forEach((select) => {
      const w =
        page().widgets[Number(select.closest("[data-index]").dataset.index)];
      select.outerHTML = metricSelect(w).replace(
        /^<label>数据|<\/label>$/g,
        "",
      );
    });
    document.querySelectorAll("[data-status-select]").forEach((select) => {
      const w = page().widgets[Number(select.closest("[data-index]").dataset.index)];
      select.outerHTML = statusSelect(w).replace(/^<label>数据|<\/label>$/g, "");
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
  return `<label>${label}${options ? `<select data-field="${key}">${options.map(([v, t]) => `<option value="${esc(v)}" ${String(value) === String(v) ? "selected" : ""}>${esc(t)}</option>`).join("")}</select>` : `<input data-field="${key}" value="${esc(value)}" ${placeholder ? `placeholder="${esc(placeholder)}"` : ""} ${key.includes("host") ? 'list="hosts"' : key.includes("source") ? 'list="sources"' : key === "binding.key" ? 'list="keys"' : ""} maxlength="${key === "unit" ? 16 : key === "agent" ? 96 : key === "hosts" ? 195 : 48}">`}</label>`;
}
function renderOptions() {
  const all = hosts();
  $("#hosts").innerHTML = [...new Set(all.map((h) => h.host_id))]
    .map((x) => `<option value="${esc(x)}">`)
    .join("");
  $("#sources").innerHTML = [
    ...new Set([
      "system.cpu",
      "system.memory",
      "herdr",
      ...all.flatMap((h) => entries(h.sources).map(([id]) => id)),
    ]),
  ]
    .map((x) => `<option value="${esc(x)}">`)
    .join("");
  $("#keys").innerHTML = [
    ...new Set([
      "usage_pct",
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
  const records = new Map();
  for (const h of hosts())
    for (const [source_id, source] of entries(h.sources))
      for (const [key, metric] of entries(source.metrics)) {
        const binding = { host_id: h.host_id, source_id, key };
        records.set(JSON.stringify(binding), {
          binding,
          unit: metric.unit,
          label: `${h.host_name || h.host_id} · ${h.host_id} / ${source_id} / ${key}`,
        });
      }
  return [...records.values()];
}
function metricSelect(w) {
  return `<label>数据<select data-metric-select><option value="">手动指定</option>${metricEntries()
    .map(
      (m, i) =>
        `<option value="${i}" ${JSON.stringify(m.binding) === JSON.stringify(w.binding) ? "selected" : ""}>${esc(m.label)}</option>`,
    )
    .join("")}</select></label>`;
}
function statusEntries() {
  return hosts().flatMap((h) => entries(h.sources).filter(([, s]) => s.kind === "status").map(([source_id]) => ({
    host_id: h.host_id, source_id, label: `${h.host_name || h.host_id} · ${h.host_id} / ${source_id}`,
  })));
}
function statusSelect(w) {
  return `<label>数据<select data-status-select><option value="">手动指定</option>${statusEntries().map((s, i) => `<option value="${i}" ${s.host_id === w.host_id && s.source_id === w.source_id ? "selected" : ""}>${esc(s.label)}</option>`).join("")}</select></label>`;
}
function widgetTypes(p) {
  return ["metric", "status", "clock", "machine", "agents"]
    .map(
      (type) =>
        `<option value="${type}" ${p.widgets.length >= 8 ? "disabled" : ""}>${{ metric: "指标", status: "状态", clock: "时钟", machine: "单机卡", agents: "Agent 列表" }[type]}</option>`,
    )
    .join("");
}
function agentText(a, hostName) {
  const main =
    a.brand && a.name && a.name !== a.brand
      ? a.name
      : a.project || a.name || a.brand || "Agent";
  const detail = [
    ...new Set(
      [a.brand || a.name, a.project !== main ? a.project : "", hostName].filter(
        Boolean,
      ),
    ),
  ].join(" · ");
  return { main, detail };
}
const alertNames = {
  agent_done: "Agent 完成",
  metric_high: "指标过高",
  host_offline: "机器离线",
};
const alertDefaults = {
  agent_done: { host_id: "*", source_id: "herdr", include_idle: true },
  metric_high: {
    host_id: "",
    source_id: "system.memory",
    key: "usage_pct",
    unit: "percent",
    threshold: 90,
    recover: 85,
    hold_s: 60,
  },
  host_offline: { host_id: "*", hold_s: 30 },
};
const alertNumber = (label, key, value, min, max, step = "1") =>
  `<label>${label}<input type="number" data-alert-field="${key}" value="${esc(value)}" min="${min}" max="${max}" step="${step}"></label>`;
const alertDecimal = (label, key, value) =>
  `<label>${label}<input type="number" data-alert-field="${key}" value="${esc(value)}" step="any"></label>`;
const alertText = (label, key, value, list) =>
  `<label>${label}<input data-alert-field="${key}" value="${esc(value)}" list="${list}" maxlength="48"></label>`;
function alertSummary(rule) {
  const binding =
    rule.type === "metric_high"
      ? `${rule.host_id || "未绑定"} / ${rule.source_id} / ${rule.key} > ${rule.threshold}${rule.unit === "percent" ? "%" : ` ${rule.unit}`} · ${rule.hold_s} 秒 · ≤ ${rule.recover} 恢复`
      : rule.type === "agent_done"
        ? `${rule.host_id === "*" ? "全部机器" : rule.host_id || "未绑定"} · ${rule.source_id}${rule.include_idle ? " · 含空闲" : ""}`
        : `${rule.host_id === "*" ? "全部机器" : rule.host_id || "未绑定"} · ${rule.hold_s} 秒`;
  return `<span class="widget-caption"><strong>${alertNames[rule.type]}</strong><span>${esc(binding)}</span></span>`;
}
function alertMetricSelect(rule) {
  const records = metricEntries();
  const chosen = records.findIndex(
    (m) =>
      m.binding.host_id === rule.host_id &&
      m.binding.source_id === rule.source_id &&
      m.binding.key === rule.key &&
      m.unit === rule.unit,
  );
  return `<label>已上报指标<select data-alert-metric><option value="">手动填写未上报指标</option>${records.map((m, i) => `<option value="${i}" ${i === chosen ? "selected" : ""}>${esc(m.label)}</option>`).join("")}</select></label>`;
}
function alertRuleEditor(rule, i) {
  let fields;
  if (rule.type === "metric_high")
    fields = `${alertMetricSelect(rule)}<details class="manual-fields" ${metricEntries().some((m) => m.binding.host_id === rule.host_id && m.binding.source_id === rule.source_id && m.binding.key === rule.key && m.unit === rule.unit) ? "" : "open"}><summary>手动绑定</summary><div class="fields">${alertText("机器 ID", "host_id", rule.host_id, "hosts")}${alertText("来源 ID", "source_id", rule.source_id, "sources")}${alertText("指标键", "key", rule.key, "keys")}${alertText("单位", "unit", rule.unit, "alert-units")}</div></details><div class="fields">${alertDecimal("高于", "threshold", rule.threshold)}${alertDecimal("低于或等于恢复", "recover", rule.recover)}${alertNumber("持续（秒）", "hold_s", rule.hold_s, 1, 86400)}</div>`;
  else if (rule.type === "agent_done")
    fields = `<div class="fields">${alertText("机器 ID（* 为全部）", "host_id", rule.host_id, "alert-hosts")}<label>来源 ID<input value="herdr" disabled></label></div><label class="switch-field"><input type="checkbox" data-alert-field="include_idle" ${rule.include_idle ? "checked" : ""}><span>包含空闲</span></label>`;
  else
    fields = `<div class="fields">${alertText("机器 ID（* 为全部）", "host_id", rule.host_id, "alert-hosts")}${alertNumber("持续（秒）", "hold_s", rule.hold_s, 5, 86400)}</div>`;
  return `<div class="widget alert-rule" data-alert-index="${i}"><details class="widget-details" data-alert-details="${esc(rule.id)}" ${openAlerts.has(rule.id) ? "open" : ""}><summary class="widget-summary">${alertSummary(rule)}</summary><div class="widget-body"><label>类型<select data-alert-field="type">${Object.entries(
    alertNames,
  )
    .map(
      ([value, name]) =>
        `<option value="${value}" ${rule.type === value ? "selected" : ""}>${name}</option>`,
    )
    .join(
      "",
    )}</select></label>${fields}<div class="fields alert-delivery">${alertNumber("显示时间（秒，0 为手动关闭）", "display_s", rule.display_s, 0, 300)}</div><div class="widget-remove"><button class="danger" data-alert-remove="${i}">删除规则</button></div></div></details><div class="alert-toggles"><label class="switch-field"><input type="checkbox" data-alert-field="enabled" ${rule.enabled ? "checked" : ""}><span>启用</span></label><label class="switch-field"><input type="checkbox" data-alert-field="sound" ${rule.sound ? "checked" : ""}><span>声音</span></label><label class="switch-field"><input type="checkbox" data-alert-field="vibrate" ${rule.vibrate ? "checked" : ""}><span>振动</span></label><label class="switch-field"><input type="checkbox" data-alert-field="flash" ${rule.flash ? "checked" : ""}><span>闪光</span></label></div></div>`;
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
function validateAlerts() {
  const number = (value, label, min, max, integer = true) => {
    if (
      !Number.isFinite(value) ||
      (integer && !Number.isInteger(value)) ||
      value < min ||
      value > max
    )
      throw Error(`${label}须为 ${min}–${max}`);
  };
  const id = (value, label) => {
    if (
      !value ||
      new TextEncoder().encode(value).length > 48 ||
      !/^[A-Za-z0-9._-]+$/.test(value)
    )
      throw Error(`${label}格式错误`);
  };
  number(alerts.volume, "音量", 0, 100);
  number(alerts.vibration_strength, "振动强度", 0, 100);
  if (typeof alerts.flash_enabled !== "boolean")
    throw Error("闪光灯须为布尔值");
  if (alerts.rules.length > 8) throw Error("最多 8 条规则");
  if (new Set(alerts.rules.map((r) => r.id)).size !== alerts.rules.length)
    throw Error("规则 ID 重复");
  for (const rule of alerts.rules) {
    id(rule.id, "规则 ID");
    if (rule.host_id !== "*" || rule.type === "metric_high")
      id(rule.host_id, "机器 ID");
    if (rule.type === "metric_high") {
      if (rule.host_id === "*") throw Error("指标须指定单台机器");
      id(rule.source_id, "来源 ID");
      id(rule.key, "指标键");
      if (!rule.unit || rule.unit.length > 16 || /[^\x21-\x7e]/.test(rule.unit))
        throw Error("单位格式错误");
      if (!Number.isFinite(rule.threshold) || !Number.isFinite(rule.recover))
        throw Error("阈值和恢复值须为有限数字");
      if (rule.recover >= rule.threshold) throw Error("恢复值须低于阈值");
      number(rule.hold_s, "持续时间", 1, 86400);
    } else if (rule.type === "agent_done") {
      if (rule.source_id !== "herdr") throw Error("Agent 来源须为 herdr");
    } else if (rule.type === "host_offline")
      number(rule.hold_s, "持续时间", 5, 86400);
    else throw Error("规则类型错误");
    number(rule.display_s, "显示时间", 0, 300);
    if (typeof rule.vibrate !== "boolean") throw Error("振动须为布尔值");
    if (typeof rule.flash !== "boolean") throw Error("闪光须为布尔值");
  }
}
let alertSerial = 0;
function addAlert() {
  const id = "alert" + Date.now().toString(36) + (++alertSerial).toString(36);
  alerts.rules.push({
    id,
    enabled: true,
    type: "metric_high",
    ...alertDefaults.metric_high,
    display_s: 10,
    sound: false,
    vibrate: false,
    flash: false,
  });
  openAlerts.add(id);
  alertsDirty = true;
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
                      .join("")}${s.kind === "status" && s.state ? `<p class="code status-${esc(s.state.level)}">${esc(s.state.text)}${s.state.detail ? ` · ${esc(s.state.detail)}` : ""}</p>` : ""}${(s.agents || [])
                      .map((a) => {
                        const t = agentText(a, h.host_name || h.host_id);
                        return `<p class="catalog-agent"><strong>${esc(t.main)}</strong><span>${esc(a.state)}</span><small>${esc(t.detail)} · ${esc(a.id)}</small></p>`;
                      })
                      .join("")}</div>`,
                )
                .join("")}`,
          )
          .join("") || "<p>暂无上报</p>"
      }</section>`;
    return;
  }
  if (tab === "device") {
    $("#editor").innerHTML =
      `<section class="panel"><h2>设备</h2><label>NTP 服务器<input id="ntp-server" maxlength="127" value="${esc(device.ntp_server)}"></label><label>亮度<input id="brightness" type="number" min="0" max="100" value="${device.brightness}"></label><label class="switch-field"><input id="auto-rotate" type="checkbox" role="switch" ${device.auto_rotate ? "checked" : ""}><span>自动旋转</span></label><label>手动方向<select id="rotation" ${device.auto_rotate ? "disabled" : ""}>${[0, 90, 180, 270].map((x) => `<option value="${x}" ${x === device.rotation ? "selected" : ""}>${x}°</option>`).join("")}</select></label><div class="actions"><button id="save-device">保存</button><button id="reload">重新加载</button></div></section>`;
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
    <div class="widget-body">${field("标题", "title", w.title, undefined, w.type === "clock" ? "自动" : "")}${
      w.type === "clock"
        ? `${field("UTC 偏移", "utc_offset", w.utc_offset)}<label class="switch-field"><input type="checkbox" role="switch" data-field="seconds" ${w.seconds ? "checked" : ""}><span>显示秒</span></label>`
        : w.type === "metric"
        ? `${metricSelect(w)}<details class="manual-fields" ${w.binding.host_id ? "" : "open"}><summary>手动绑定</summary><div class="fields">${field("机器 ID", "binding.host_id", w.binding.host_id)}${field("来源 ID", "binding.source_id", w.binding.source_id)}${field("指标键", "binding.key", w.binding.key)}${field("单位", "unit", w.unit)}</div></details>`
        : w.type === "status"
          ? `${statusSelect(w)}<details class="manual-fields" open><summary>手动绑定</summary><div class="fields">${field("机器 ID", "host_id", w.host_id)}${field("来源 ID", "source_id", w.source_id)}</div></details>`
        : w.type === "machine"
          ? field("机器 ID", "host_id", w.host_id)
          : `<div class="fields">${field("机器 ID（逗号分隔，* 为全部）", "hosts", w.hosts.join(", "))}${field("来源 ID", "source_id", w.source_id)}${field(
              "默认状态",
              "filter",
              w.filter,
              [
                ["all", "全部"],
                ["attention", "需关注"],
                ["working", "进行中"],
              ],
            )}</div><details class="manual-fields"><summary>筛选条件</summary><div class="fields">${field("项目", "project", w.project)}${field("Agent ID", "agent", w.agent)}</div></details><label class="switch-field"><input type="checkbox" role="switch" data-field="auto_page" ${w.auto_page !== false ? "checked" : ""}><span>自动翻页</span><small>10 秒</small></label>`
    }<div class="widget-remove"><button class="danger" data-remove="${i}">删除组件</button></div></div></div>`
        : ""
    }
    <div class="actions add-widget-row"><select id="new-type" ${p.widgets.length >= 8 ? "disabled" : ""}>${widgetTypes(p)}</select><button id="add-widget" ${p.widgets.length >= 8 ? "disabled" : ""}>＋ 添加组件</button></div>
  </section>`;
}

function metricHTML(b, unit) {
  const r = resolve(b, unit);
  return `<span class="${r.value === undefined ? "metric-status" : "metric-value"}">${esc(r.text)}</span>`;
}
function agentRows(w, filter = w.filter) {
  let rows = [],
    missing = [];
  const chosen = w.hosts.includes("*")
    ? hosts()
    : w.hosts.map((id) => host(id) || { host_id: id });
  for (const h of chosen) {
    const s = source(h, w.source_id);
    const problem = !host(h.host_id)
      ? "Waiting"
      : s && s.kind !== "agents"
        ? "Type mismatch"
        : health(h, s) || (!s ? "Not reported" : "");
    if (problem) missing.push({ h, problem });
    for (const a of s?.kind === "agents" ? s.agents || [] : []) {
      if (
        (w.project && a.project !== w.project) ||
        (w.agent && a.id !== w.agent)
      )
        continue;
      if (
        (filter === "attention" && a.state !== "blocked") ||
        (filter === "working" && a.state !== "working")
      )
        continue;
      rows.push({ h, a, problem });
    }
  }
  const rank = { blocked: 0, working: 1, idle: 2, done: 3, unknown: 4 };
  rows.sort(
    (a, b) =>
      (rank[a.a.state] ?? 4) - (rank[b.a.state] ?? 4) ||
      a.h.host_id.localeCompare(b.h.host_id) ||
      (a.a.id || "").localeCompare(b.a.id || ""),
  );
  return { rows, missing };
}
function renderPreview(items) {
  if (!display) return;
  items ??= page().widgets;
  if (gesture && items === page().widgets) return;
  const p = page(),
    editing = tab === "screen";
  $("#preview-detail").textContent = p.widgets.some((w) => w.type === "clock") ? "480 × 480 · 浏览器时间" : "480 × 480";
  let html = `<div class="screen-head"><button id="screen-page">${esc(p.title)} ▾</button><span class="screen-icons" aria-hidden="true"><svg viewBox="0 0 78 20" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M1 7Q9 -1 17 7M4 10Q9 5 14 10M7 13Q9 11 11 13"/><circle cx="9" cy="16" r="1"/><path d="M27 14V9a6 6 0 0 1 12 0v5M27 10h3v7h-3zM36 10h3v7h-3z"/><rect x="50" y="5" width="21" height="11" rx="2"/><path d="M73 8v5M53 8h12v5H53z"/></svg>⚙</span></div>`;
  if (editing)
    for (let row = 0; row < 6; row++)
      for (let col = 0; col < 4; col++) {
        const r = pixelRect({ at: [col, row], size: [1, 1] });
        html += `<div class="grid-cell" style="left:${r.x}px;top:${r.y}px;width:${r.width}px;height:${r.height}px"></div>`;
      }
  for (const w of items || []) {
    const { x, y, width, height } = pixelRect(w);
    html += `<div class="card ${w.type === "status" ? `status-card ${w.size[1] === 1 ? "short-row" : ""}` : ""} ${w.type === "clock" ? "clock-card" : w.type === "agents" ? "agent-card" : (w.type === "metric" || w.type === "status") ? "metric-card" : "machine-card"} ${(w.type === "metric" || w.type === "status") && w.size[0] === 1 ? "narrow" : ""} ${editing && w.id === selectedWidget ? "selected" : ""}" data-widget-id="${esc(w.id)}" style="left:${x}px;top:${y}px;width:${width}px;height:${height}px">`;
    if (w.type === "clock") {
      let parts;
      try { parts = clockParts(w); } catch { parts = { time: "--:--", date: "" }; }
      html += `<span class="clock-title">${esc(clockTitle(w, width))}</span><span class="clock-time" style="${clockTypography(w, width, height)}">${parts.time}</span>${w.size[1] > 1 ? `<span class="clock-date">${parts.date}</span>` : ""}`;
    } else if (w.type === "metric")
      html += `<div class="metric-main"><span class="metric-title">${esc(w.title)}</span>${metricHTML(w.binding, w.unit)}</div>${w.size[0] === 1 && w.size[1] === 1 ? "" : `<div class="metric-host">${esc(host(w.binding.host_id)?.host_name || w.binding.host_id)}</div>`}`;
    else if (w.type === "status") {
      const result = resolveStatus(w);
      html += `<div class="metric-main"><span class="metric-title">${esc(w.title)}</span><span class="metric-status status-${esc(result.level || "unknown")}">${esc(result.text)}</span></div>${(w.size[1] > 1 || w.size[0] > 1) && result.detail ? `<div class="status-detail">${esc(result.detail)}</div>` : ""}`;
    } else if (w.type === "machine")
      html += `<span>${esc(w.title || host(w.host_id)?.host_name || w.host_id)}</span><div class="metric-host">${esc(w.host_id)} · ${esc(host(w.host_id)?.health || "Waiting")}</div>${[
        ["CPU", "system.cpu"],
        ["RAM", "system.memory"],
      ]
        .map(
          ([name, id]) =>
            `<div class="machine-value"><span>${name}</span>${metricHTML({ host_id: w.host_id, source_id: id, key: "usage_pct" }, "percent")}</div>`,
        )
        .join("")}`;
    else {
      const view = agentPreview(w),
        { rows, missing } = agentRows(w, view.filter),
        capacity = Math.max(1, Math.floor((height - 72) / 47));
      const pages = Math.max(1, Math.ceil(rows.length / capacity));
      view.page = Math.min(view.page, pages - 1);
      html += `<div class="filters">${["all", "attention", "working"].map((f, i) => `<button data-preview-filter="${f}" data-widget-id="${esc(w.id)}" class="${view.filter === f ? "active" : ""}">${["All", "Attention", "Working"][i]}</button>`).join("")}</div>`;
      html +=
        rows
          .slice(view.page * capacity, (view.page + 1) * capacity)
          .map(({ h, a, problem }) => {
            const t = agentText(a, h.host_name || h.host_id);
            return `<div class="agent-row"><div class="agent-name"><strong>${esc(t.main)}</strong><span class="state-${esc(a.state)}">${esc(problem || a.state)}</span></div><div class="agent-detail">${esc(t.detail)}</div></div>`;
          })
          .join("") || '<div class="empty">No matching agents</div>';
      const summary = missing
        .map(({ h, problem }) => `${h.host_name || h.host_id}: ${problem}`)
        .join(" · ");
      html += `<div class="agent-summary" title="${esc(summary)}">${esc(summary)}</div><div class="agent-footer"><button data-agent-turn="-1" data-widget-id="${esc(w.id)}" ${view.page === 0 ? "disabled" : ""} aria-label="上一页">‹</button><span>${rows.length} agents · ${view.page + 1} / ${pages}</span><button data-agent-turn="1" data-widget-id="${esc(w.id)}" ${view.page >= pages - 1 ? "disabled" : ""} aria-label="下一页">›</button></div>`;
    }
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
    title: { metric: "Metric", status: "Status", clock: "", machine: "Machine", agents: "Agents" }[type],
  };
  if (type === "metric")
    Object.assign(w, {
      binding: { host_id: "", source_id: "system.cpu", key: "usage_pct" },
      unit: "percent",
    });
  else if (type === "status") Object.assign(w, { host_id: "", source_id: "" });
  else if (type === "clock") Object.assign(w, { utc_offset: "+00:00", seconds: false });
  else if (type === "machine") w.host_id = "";
  else
    Object.assign(w, {
      hosts: ["*"],
      source_id: "herdr",
      filter: "all",
      auto_page: true,
      project: "",
      agent: "",
    });
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
  if (t.hasAttribute("data-alert-metric")) {
    if (t.value === "") {
      t.closest(".widget-body").querySelector(".manual-fields").open = true;
      return;
    }
    const m = metricEntries()[Number(t.value)];
    if (!m) return;
    const rule =
      alerts.rules[Number(t.closest("[data-alert-index]").dataset.alertIndex)];
    Object.assign(rule, m.binding, { unit: m.unit });
    alertsDirty = true;
    status("");
    renderEditor();
    return;
  }
  if (t.hasAttribute("data-alert-field")) {
    const key = t.dataset.alertField;
    const rule = t.closest("[data-alert-index]")
      ? alerts.rules[Number(t.closest("[data-alert-index]").dataset.alertIndex)]
      : alerts;
    if (key === "type") {
      const { id, enabled, display_s, sound, vibrate, flash } = rule;
      Object.assign(rule, {
        id,
        enabled,
        type: t.value,
        ...alertDefaults[t.value],
        display_s,
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
            "display_s",
            "sound",
            "vibrate",
            "flash",
            ...Object.keys(alertDefaults[t.value]),
          ].includes(field)
        )
          delete rule[field];
      openAlerts.add(rule.id);
    } else if (t.type === "checkbox") rule[key] = t.checked;
    else if (t.type === "number")
      rule[key] = t.value === "" ? NaN : Number(t.value);
    else rule[key] = t.value.trim();
    alertsDirty = true;
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
    deviceDirty = true;
    status("");
    return;
  }
  if (t.id === "brightness" || t.id === "rotation" || t.id === "auto-rotate") {
    if (t.id === "auto-rotate") {
      device.auto_rotate = t.checked;
      $("#rotation").disabled = t.checked;
    } else device[t.id] = Number(t.value);
    deviceDirty = true;
    status("");
    return;
  }
  if (t.hasAttribute("data-status-select")) {
    const record = t.value === "" ? null : statusEntries()[Number(t.value)];
    if (!record) return;
    rememberLayout();
    const w = page().widgets[Number(t.closest("[data-index]").dataset.index)];
    w.host_id = record.host_id;
    w.source_id = record.source_id;
    markDisplayDirty();
    render();
    return;
  }
  if (t.hasAttribute("data-metric-select")) {
    if (t.value === "") {
      t.closest(".widget-body").querySelector(".manual-fields").open = true;
      return;
    }
    const m = metricEntries()[Number(t.value)];
    if (t.value !== "" && m) {
      const w = page().widgets[Number(t.closest("[data-index]").dataset.index)];
      if (
        JSON.stringify(w.binding) === JSON.stringify(m.binding) &&
        w.unit === m.unit
      )
        return;
      rememberLayout();
      w.binding = { ...m.binding };
      w.unit = m.unit;
      render();
      markDisplayDirty();
    }
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
      (key === "auto_page" || key === "seconds")
        ? t.checked
        : key === "hosts"
          ? t.value
              .split(",")
              .map((x) => x.trim())
              .filter(Boolean)
          : t.value.trim();
    const bindingKey = key.startsWith("binding.") ? key.slice(8) : null;
    if (
      JSON.stringify(bindingKey ? w.binding[bindingKey] : w[key]) ===
      JSON.stringify(value)
    )
      return;
    rememberLayout();
    if (bindingKey) w.binding[bindingKey] = value;
    else w[key] = value;
    previewAgents.delete(`${p.id}/${w.id}`);
  }
  resetAutoPage();
  markDisplayDirty();
  renderPreview();
});
document.addEventListener("click", async (e) => {
  const t = e.target.closest("button");
  if (!t) return;
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
      display = old.display;
      selected = old.selected;
      selectedWidget = old.selectedWidget;
      displayDirty = true;
      syncFormYaml();
      displayDirty = old.dirty;
      status("");
      render();
    } else if (t.id === "add-alert") addAlert();
    else if (t.hasAttribute("data-alert-remove")) {
      const removed = alerts.rules.splice(Number(t.dataset.alertRemove), 1)[0];
      openAlerts.delete(removed.id);
      alertsDirty = true;
      status("");
      renderEditor();
    } else if (t.id === "save-alerts") {
      await saveConfig();
    } else if (t.id === "reload-alerts") {
      await reloadConfig();
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
    } else if (t.id === "save") {
      await saveConfig();
    } else if (t.id === "save-device" || t.id === "save-config") {
      await saveConfig();
    } else if (t.id === "reload" || t.id === "reload-config") {
      await reloadConfig();
    } else if (t.id === "export-yaml") {
      syncFormYaml();
      const url = URL.createObjectURL(
        new Blob([yamlText], { type: "application/yaml" }),
      );
      const link = document.createElement("a");
      link.href = url;
      link.download = "curio.yaml";
      link.click();
      URL.revokeObjectURL(url);
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
    } else if (t.dataset.previewFilter) {
      const w = page().widgets.find((w) => w.id === t.dataset.widgetId),
        view = agentPreview(w);
      view.filter = t.dataset.previewFilter;
      view.page = 0;
      view.due = performance.now() + 10000;
      renderPreview();
    } else if (t.dataset.agentTurn) {
      turnAgents(t.dataset.widgetId, Number(t.dataset.agentTurn));
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
function turnAgents(id, direction) {
  const w = page().widgets.find((w) => w.id === id && w.type === "agents");
  if (!w) return;
  const view = agentPreview(w),
    capacity = Math.max(1, Math.floor((pixelRect(w).height - 72) / 47));
  const pages = Math.max(
    1,
    Math.ceil(agentRows(w, view.filter).rows.length / capacity),
  );
  view.page = Math.max(0, Math.min(pages - 1, view.page + direction));
  view.due = performance.now() + 10000;
  renderPreview();
}
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
  const due = performance.now() + 10000;
  for (const view of previewAgents.values()) view.due = due;
}
let previewClockSecond = -1;
setInterval(() => {
  if (!display || document.hidden || tab !== "screen" || !token || gesture)
    return;
  const second = Math.floor(Date.now() / 1000);
  let changed = second !== previewClockSecond && page().widgets.some((w) => w.type === "clock");
  previewClockSecond = second;
  for (const w of page().widgets.filter(
    (w) => w.type === "agents" && w.auto_page !== false,
  )) {
    const view = agentPreview(w);
    if (performance.now() < view.due) continue;
    const capacity = Math.max(1, Math.floor((pixelRect(w).height - 72) / 47));
    const pages = Math.max(
      1,
      Math.ceil(agentRows(w, view.filter).rows.length / capacity),
    );
    view.page = (view.page + 1) % pages;
    view.due = performance.now() + 10000;
    changed = true;
  }
  if (changed) renderPreview();
}, 250);
document.addEventListener("visibilitychange", () => {
  clearTimeout(pollTimer);
  resetAutoPage();
  if (!document.hidden) poll();
});
addEventListener("resize", resize);
if (token) connect();
