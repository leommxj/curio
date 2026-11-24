(() => {
  alertTypes.host_offline = {
    label: "机器离线",
    types: { host: "string", hold: "number" },
    defaults: () => ({ host: "*", hold: 30 }),
    editor(rule) {
      return `<div class="fields">${alertText("机器 ID（* 为全部）", "host", rule.host, "alert-hosts")}${alertNumber("持续（秒）", "hold", rule.hold, 5, 86400)}</div>`;
    },
    validate(rule, { id, number }) {
      if (rule.host !== "*") id(rule.host, "机器 ID");
      number(rule.hold, "持续时间", 5, 86400);
    },
    summary(rule) {
      return `${rule.host === "*" ? "全部机器" : rule.host || "未绑定"} · ${rule.hold} 秒`;
    },
  };
})();
