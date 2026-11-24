(() => {
  function alertMetricSelect(rule) {
    const records = metricEntries();
    const chosen = records.findIndex(
      (m) =>
        m.host === rule.host &&
        m.source === rule.source &&
        m.key === rule.key &&
        m.unit === rule.unit,
    );
    return `<label>已上报指标<select data-alert-metric><option value="">手动填写未上报指标</option>${records.map((m, i) => `<option value="${i}" ${i === chosen ? "selected" : ""}>${esc(m.label)}</option>`).join("")}</select></label>`;
  }
  alertTypes.metric_high = {
    label: "指标过高",
    change(t) {
      if (t.hasAttribute("data-alert-metric")) {
        if (t.value === "") {
          t.closest(".widget-body").querySelector(".manual-fields").open = true;
          return true;
        }
        const m = metricEntries()[Number(t.value)];
        if (!m) return true;
        const rule =
          alerts.rules[
            Number(t.closest("[data-alert-index]").dataset.alertIndex)
          ];
        Object.assign(rule, {
          host: m.host,
          source: m.source,
          key: m.key,
          unit: m.unit,
        });
        markDirty("alerts");
        status("");
        renderEditor();
        return true;
      }
      return false;
    },
    types: {
      host: "string",
      source: "string",
      key: "string",
      unit: "string",
      threshold: "number",
      recover: "number",
      hold: "number",
    },
    defaults: () => ({ hold: 60 }),
    initial: {
      host: "",
      source: "system.memory",
      key: "usage_pct",
      unit: "percent",
      threshold: 90,
      recover: 85,
    },
    editor(rule) {
      return `${alertMetricSelect(rule)}<details class="manual-fields" ${metricEntries().some((m) => m.host === rule.host && m.source === rule.source && m.key === rule.key && m.unit === rule.unit) ? "" : "open"}><summary>手动绑定</summary><div class="fields">${alertText("机器 ID", "host", rule.host, "hosts")}${alertText("来源 ID", "source", rule.source, "sources")}${alertText("指标键", "key", rule.key, "keys")}${alertText("单位", "unit", rule.unit, "alert-units")}</div></details><div class="fields">${alertDecimal("高于", "threshold", rule.threshold)}${alertDecimal("低于或等于恢复", "recover", rule.recover)}${alertNumber("持续（秒）", "hold", rule.hold, 1, 86400)}</div>`;
    },
    validate(rule, { id, number }) {
      if (rule.host === "*") throw Error("指标须指定单台机器");
      id(rule.host, "机器 ID");
      id(rule.source, "来源 ID");
      id(rule.key, "指标键");
      if (!rule.unit || rule.unit.length > 16 || /[^\x21-\x7e]/.test(rule.unit))
        throw Error("单位格式错误");
      if (!Number.isFinite(rule.threshold) || !Number.isFinite(rule.recover))
        throw Error("阈值和恢复值须为有限数字");
      if (rule.recover >= rule.threshold) throw Error("恢复值须低于阈值");
      number(rule.hold, "持续时间", 1, 86400);
    },
    summary(rule) {
      return `${rule.host || "未绑定"} / ${rule.source} / ${rule.key} > ${rule.threshold}${rule.unit === "percent" ? "%" : ` ${rule.unit}`} · ${rule.hold} 秒 · ≤ ${rule.recover} 恢复`;
    },
  };
})();
