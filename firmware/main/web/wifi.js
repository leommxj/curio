"use strict";
function validateWifi(list) {
  if (!Array.isArray(list) || list.length > 4)
    throw Error("network.wifi 须为最多 4 个网络的列表");
  const bytes = value => new TextEncoder().encode(value).length;
  const names = new Set();
  for (const [i, wifi] of list.entries()) {
    const path = `network.wifi[${i}]`;
    if (typeof wifi?.ssid !== "string" || bytes(wifi.ssid) < 1 || bytes(wifi.ssid) > 32)
      throw Error(`${path}.ssid 须为 1–32 字节`);
    if (names.has(wifi.ssid)) throw Error(`Wi-Fi 名称重复: ${wifi.ssid}`);
    names.add(wifi.ssid);
    if (wifi.password !== undefined &&
      (typeof wifi.password !== "string" ||
        (wifi.password !== "" &&
          !((bytes(wifi.password) >= 8 && bytes(wifi.password) <= 63) ||
            /^[0-9a-fA-F]{64}$/.test(wifi.password)))))
      throw Error(`${path}.password 密码须为 8–63 字节或 64 位十六进制 PSK`);
    if (wifi.ipv4 !== undefined) validateIpv4(wifi.ipv4, `${path}.ipv4`);
  }
}
const ipv4Fields = { address: "IP 地址", netmask: "子网掩码", gateway: "网关", dns: "DNS 服务器" };
function validateIpv4(value, path) {
  const fail = () => { throw Error(`${path} 须填写有效的 address、netmask、gateway、dns；地址和网关须为同子网内不同的可用地址，掩码须为 /1–30`); };
  if (!value || typeof value !== "object" || Array.isArray(value) ||
      Object.keys(value).some(key => !Object.keys(ipv4Fields).includes(key))) fail();
  const parse = text => {
    if (typeof text !== "string" || !/^(0|[1-9][0-9]{0,2})(\.(0|[1-9][0-9]{0,2})){3}$/.test(text)) fail();
    const parts = text.split(".").map(Number);
    if (parts.some(part => part > 255)) fail();
    return parts.reduce((number, part) => (number * 256 + part) >>> 0, 0);
  };
  const [address, mask, gateway, dns] = Object.keys(ipv4Fields).map(key => parse(value[key]));
  const hostMask = (~mask) >>> 0;
  if (hostMask < 3 || hostMask > 0x7fffffff || (hostMask & (hostMask + 1)) !== 0) fail();
  for (const ip of [address, gateway, dns]) {
    const first = ip >>> 24;
    if (first < 1 || first > 223 || first === 127) fail();
  }
  if (address === gateway || (address & mask) !== (gateway & mask)) fail();
  for (const ip of [address, gateway])
    if ((ip & hostMask) === 0 || (ip & hostMask) === hostMask) fail();
}
function wifiIpv4Controls(wifi) {
  const escape = value => String(value ?? "").replaceAll("&", "&amp;").replaceAll('"', "&quot;").replaceAll("<", "&lt;");
  return `<label>IP 配置<select data-ipv4-mode><option value="dhcp">自动获取（DHCP）</option><option value="static" ${wifi.ipv4 ? "selected" : ""}>静态 IP</option></select></label>
    <div data-ipv4-fields ${wifi.ipv4 ? "" : "hidden"}>${Object.entries(ipv4Fields).map(([key, label]) =>
      `<label>${label}<input data-ipv4-field="${key}" value="${escape(wifi.ipv4?.[key])}" inputmode="decimal" autocomplete="off"></label>`).join("")}</div>`;
}
function editWifiIpv4(wifi, target, container) {
  if (target.hasAttribute("data-ipv4-mode")) {
    if (target.value === "static") wifi.ipv4 = Object.fromEntries(Object.keys(ipv4Fields).map(key => [key, ""]));
    else delete wifi.ipv4;
    container.querySelector("[data-ipv4-fields]").hidden = !wifi.ipv4;
    for (const input of container.querySelectorAll("[data-ipv4-field]")) input.value = wifi.ipv4?.[input.dataset.ipv4Field] ?? "";
  } else wifi.ipv4[target.dataset.ipv4Field] = target.value;
}
