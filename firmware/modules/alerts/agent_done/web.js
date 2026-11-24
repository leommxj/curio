(() => {
  alertTypes.agent_done = {
    label: "Agent 完成",
    types: { host: "string", source: "string", include_idle: "boolean" },
    defaults: () => ({ host: "*", source: "herdr", include_idle: true }),
    editor(rule) {
      return `<div class="fields">${alertText("机器 ID（* 为全部）", "host", rule.host, "alert-hosts")}${alertText("来源 ID", "source", rule.source, "sources")}</div><label class="switch-field"><input type="checkbox" data-alert-field="include_idle" ${rule.include_idle ? "checked" : ""}><span>包含空闲</span></label>`;
    },
    validate(rule, { id, number }) {
      if (rule.host !== "*") id(rule.host, "机器 ID");
      id(rule.source, "来源 ID");
    },
    summary(rule) {
      return `${rule.host === "*" ? "全部机器" : rule.host || "未绑定"} · ${rule.source}${rule.include_idle ? " · 含空闲" : ""}`;
    },
  };
})();
