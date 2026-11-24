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
  }
}
