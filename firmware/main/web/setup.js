const form = document.querySelector("#setup-form");
const savedPanel = document.querySelector("#setup-saved");
const status = document.querySelector("#setup-status");
let config;
let etag;
let savedYaml;
let preserveDraft = false;
let wifiDraft = [];
let sourceWifi = [];
let selectedWifi = 0;

function setBusy(busy) {
  for (const field of form.querySelectorAll("input, select, button")) field.disabled = busy;
  document.querySelector("#add-setup-wifi").disabled = busy || wifiDraft.length >= 4;
  document.querySelector("#reload-setup").disabled = busy;
  document.querySelector("#reset-device").disabled = busy;
}

const clone = value => JSON.parse(JSON.stringify(value));

function renderWifi() {
  if (!wifiDraft.length) wifiDraft.push({ ssid: "", password: "" });
  const escape = value => value.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;").replaceAll('"', "&quot;");
  document.querySelector("#wifi-select").innerHTML = wifiDraft.map((wifi, i) =>
    `<option value="${i}">${i + 1}. ${escape(wifi.ssid) || "新网络"}</option>`).join("");
  document.querySelector("#wifi-select").value = String(selectedWifi);
  document.querySelector("#add-setup-wifi").disabled = wifiDraft.length >= 4;
  document.querySelector("#ssid").value = wifiDraft[selectedWifi]?.ssid ?? "";
  document.querySelector("#password").value = wifiDraft[selectedWifi]?.password ?? "";
  document.querySelector("#setup-ipv4").innerHTML = wifiIpv4Controls(wifiDraft[selectedWifi]);
}

function fillFields(value) {
  sourceWifi = clone(value.network?.wifi ?? []);
  wifiDraft = clone(sourceWifi);
  selectedWifi = 0;
  renderWifi();
  document.querySelector("#hostname").value = value.network?.hostname || "curio";
}

function mergedDraft(remote) {
  const result = clone(remote);
  for (const [i, wifi] of wifiDraft.entries()) {
    const original = sourceWifi[i];
    if (original && JSON.stringify(wifi) === JSON.stringify(original)) continue;
    const at = original ? result.findIndex(item => item.ssid === original.ssid) : -1;
    if (at >= 0) result[at] = clone(wifi);
    else result.push(clone(wifi));
  }
  return result;
}

function showError(error) {
  status.textContent = error.message;
  status.classList.add("danger");
}

async function responseError(response) {
  let detail;
  try {
    const body = await response.json();
    detail = body.error || body.message;
  } catch (_) {}
  return Error(`${response.status} ${detail || response.statusText}`.trim());
}

function parseConfig(source) {
  const value = jsyaml.load(source);
  if (!value || typeof value !== "object" || Array.isArray(value)) throw Error("配置格式错误");
  return value;
}

function download(name, content) {
  const url = URL.createObjectURL(new Blob([content], { type: "application/octet-stream" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function showSaved(source) {
  const hostname = config.network?.hostname || "curio";
  document.querySelector("#admin-token").textContent = config.access?.admin_token || "";
  document.querySelector("#collector-token").textContent = config.access?.collector_token || "";
  const link = document.querySelector("#device-link");
  link.href = `https://${hostname}.local`;
  link.textContent = `${hostname}.local`;
  savedYaml = source;
  form.hidden = true;
  savedPanel.hidden = false;
  document.querySelector("#download-cert").disabled = !config.tls?.certificate;
  document.querySelector("#restart").disabled = false;
}

function showForm() {
  form.hidden = false;
  savedPanel.hidden = true;
}

async function load(keepFields = false) {
  const hostnameDraft = keepFields ? document.querySelector("#hostname").value : null;
  setBusy(true);
  status.textContent = "";
  status.classList.remove("danger");
  document.querySelector("#reload-setup").hidden = true;
  try {
    const response = await fetch("/api/v2/setup");
    if (!response.ok) throw await responseError(response);
    const source = await response.text();
    const next = parseConfig(source);
    const revision = response.headers.get("ETag");
    if (!revision) throw Error("响应缺少 ETag");
    config = next;
    etag = revision;
    const selectedName = wifiDraft[selectedWifi]?.ssid;
    const merged = keepFields ? mergedDraft(next.network?.wifi ?? []) : null;
    fillFields(next);
    if (merged) {
      wifiDraft = merged;
      selectedWifi = Math.max(0, wifiDraft.findIndex(wifi => wifi.ssid === selectedName));
      renderWifi();
      document.querySelector("#hostname").value = hostnameDraft;
    }
    showForm();
    preserveDraft = false;
  } catch (error) {
    showError(error);
    document.querySelector("#reload-setup").hidden = false;
  } finally {
    setBusy(false);
  }
}

document.querySelector("#wifi-select").addEventListener("change", event => {
  selectedWifi = Number(event.target.value);
  renderWifi();
});
document.querySelector("#add-setup-wifi").addEventListener("click", () => {
  if (wifiDraft.length >= 4) return;
  wifiDraft.push({ ssid: "", password: "" });
  selectedWifi = wifiDraft.length - 1;
  renderWifi();
  document.querySelector("#ssid").focus();
});
for (const field of ["ssid", "password"]) {
  document.querySelector(`#${field}`).addEventListener("input", event => {
    wifiDraft[selectedWifi][field] = event.target.value;
    if (field === "ssid") document.querySelector("#wifi-select").options[selectedWifi].textContent =
      `${selectedWifi + 1}. ${event.target.value || "新网络"}`;
  });
}

document.querySelector("#setup-ipv4").addEventListener("input", event => {
  if (event.target.dataset.ipv4Field)
    editWifiIpv4(wifiDraft[selectedWifi], event.target, event.currentTarget);
});
document.querySelector("#setup-ipv4").addEventListener("change", event => {
  if (event.target.hasAttribute("data-ipv4-mode"))
    editWifiIpv4(wifiDraft[selectedWifi], event.target, event.currentTarget);
});

form.addEventListener("submit", async event => {
  event.preventDefault();
  if (!config || !etag) return;
  status.textContent = "";
  status.classList.remove("danger");
  setBusy(true);
  try {
    const next = clone(config);
    const hostname = document.querySelector("#hostname").value.trim().toLowerCase();
    validateWifi(wifiDraft);
    if ((next.network?.hostname || "curio") !== hostname) delete next.tls;
    next.network = {
      ...next.network,
      wifi: clone(wifiDraft),
      hostname,
    };
    const response = await fetch("/api/v2/setup", {
      method: "PUT",
      headers: { "Content-Type": "application/yaml", "If-Match": etag },
      body: jsyaml.dump(next),
    });
    if (!response.ok) {
      if (response.status === 409) {
        preserveDraft = true;
        document.querySelector("#reload-setup").hidden = false;
      }
      throw await responseError(response);
    }
    const source = await response.text();
    const normalized = parseConfig(source);
    const revision = response.headers.get("ETag");
    if (!revision) throw Error("响应缺少 ETag");
    config = normalized;
    etag = revision;
    preserveDraft = false;
    fillFields(normalized);
    showSaved(source);
  } catch (error) {
    showError(error);
  } finally {
    setBusy(false);
  }
});

document.querySelector("#download-cert").addEventListener("click", () => {
  download("server-cert.pem", config.tls.certificate);
});
document.querySelector("#download-config").addEventListener("click", () => {
  download("curio.yaml", savedYaml);
});
async function restart(event) {
  const button = event.currentTarget, reset = button.id === "reset-device";
  if (reset && !confirm("清除 Curio 的全部配置并重启？Wi-Fi、Token、证书、布局和提醒都会重置。")) return;
  setBusy(true);
  button.disabled = true;
  status.textContent = "";
  status.classList.remove("danger");
  try {
    const response = await fetch(`/api/v2/setup/${reset ? "reset" : "restart"}`, { method: "POST" });
    if (!response.ok) throw await responseError(response);
    if (reset) { form.hidden = true; savedPanel.hidden = true; }
    else setBusy(false);
    status.textContent = reset ? "正在重置，重启后请按屏幕密码重新连接热点。" : "设备正在重启";
  } catch (error) {
    showError(error);
    setBusy(false);
    button.disabled = false;
  }
}
document.querySelector("#restart").addEventListener("click", restart);
document.querySelector("#reset-device").addEventListener("click", restart);
document.querySelector("#edit-setup").addEventListener("click", () => {
  status.textContent = "";
  status.classList.remove("danger");
  showForm();
});
document.querySelector("#reload-setup").addEventListener("click", () => load(preserveDraft));
load();
