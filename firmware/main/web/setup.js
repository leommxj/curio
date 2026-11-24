const form = document.querySelector("#setup-form");
const savedPanel = document.querySelector("#setup-saved");
const status = document.querySelector("#setup-status");
let config;
let etag;
let savedYaml;
let preserveDraft = false;

function setBusy(busy) {
  for (const field of form.querySelectorAll("input, button")) field.disabled = busy;
  document.querySelector("#reload-setup").disabled = busy;
}

function fillFields(value) {
  document.querySelector("#ssid").value = value.network?.ssid || "";
  document.querySelector("#password").value = value.network?.password || "";
  document.querySelector("#hostname").value = value.network?.hostname || "curio";
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
  const documentConfig = parseConfig(source);
  const hostname = documentConfig.network?.hostname || "curio";
  document.querySelector("#admin-token").textContent = documentConfig.access?.admin_token || "";
  document.querySelector("#collector-token").textContent = documentConfig.access?.collector_token || "";
  const link = document.querySelector("#device-link");
  link.href = `https://${hostname}.local`;
  link.textContent = `${hostname}.local`;
  savedYaml = source;
  form.hidden = true;
  savedPanel.hidden = false;
  document.querySelector("#download-cert").disabled = !documentConfig.tls?.certificate;
  document.querySelector("#restart").disabled = false;
}

function showForm() {
  form.hidden = false;
  savedPanel.hidden = true;
}

async function load(keepFields = false) {
  const fields = keepFields ? ["#ssid", "#password", "#hostname"].map(id => document.querySelector(id).value) : null;
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
    fillFields(next);
    if (fields) ["#ssid", "#password", "#hostname"].forEach((id, index) => {
      document.querySelector(id).value = fields[index];
    });
    showForm();
    preserveDraft = false;
  } catch (error) {
    showError(error);
    document.querySelector("#reload-setup").hidden = false;
  } finally {
    setBusy(false);
  }
}

form.addEventListener("submit", async event => {
  event.preventDefault();
  if (!config || !etag) return;
  status.textContent = "";
  status.classList.remove("danger");
  setBusy(true);
  try {
    const next = JSON.parse(JSON.stringify(config));
    const hostname = document.querySelector("#hostname").value.trim().toLowerCase();
    if ((next.network?.hostname || "curio") !== hostname) delete next.tls;
    next.network = {
      ...next.network,
      ssid: document.querySelector("#ssid").value,
      password: document.querySelector("#password").value,
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
  download("server-cert.pem", parseConfig(savedYaml).tls.certificate);
});
document.querySelector("#download-config").addEventListener("click", () => {
  download("curio.yaml", savedYaml);
});
document.querySelector("#restart").addEventListener("click", async () => {
  const button = document.querySelector("#restart");
  button.disabled = true;
  status.textContent = "";
  status.classList.remove("danger");
  try {
    const response = await fetch("/api/v2/setup/restart", { method: "POST" });
    if (!response.ok) throw await responseError(response);
    status.textContent = "设备正在重启";
  } catch (error) {
    showError(error);
    button.disabled = false;
  }
});
document.querySelector("#edit-setup").addEventListener("click", () => {
  status.textContent = "";
  status.classList.remove("danger");
  showForm();
});
document.querySelector("#reload-setup").addEventListener("click", () => load(preserveDraft));
load();
