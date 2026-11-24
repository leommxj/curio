(() => {
  function metricSelect(w) {
    return `<label>数据<select data-metric-select><option value="">手动指定</option>${metricEntries()
      .map(
        (m, i) =>
          `<option value="${i}" ${sameMetric(m, w) ? "selected" : ""}>${esc(m.label)}</option>`,
      )
      .join("")}</select></label>`;
  }
  widgetTypes.metric = {
    label: "指标",
    minimum: [1, 1],
    change(t) {
      if (t.hasAttribute("data-metric-select")) {
        if (t.value === "") {
          t.closest(".widget-body").querySelector(".manual-fields").open = true;
          return true;
        }
        const m = metricEntries()[Number(t.value)];
        if (t.value !== "" && m) {
          const w =
            page().widgets[Number(t.closest("[data-index]").dataset.index)];
          if (sameMetric(w, m) && w.unit === m.unit) return true;
          rememberLayout();
          Object.assign(w, { host: m.host, source: m.source, key: m.key });
          w.unit = m.unit;
          render();
          markDisplayDirty();
        }
        return true;
      }
      return false;
    },
    types: { host: "string", source: "string", key: "string", unit: "string" },
    defaults: () => ({ title: "Metric", unit: "" }),
    initial: {
      host: "",
      source: "system.cpu",
      key: "usage_pct",
      unit: "percent",
    },
    validate(w, check) {
      check(w.host, "机器 ID", 48, true);
      check(w.source, "来源 ID", 48, true);
      check(w.key, "指标键", 48, true);
      check(w.unit, "单位", 16, false, true);
      if (/[^\x21-\x7e]/.test(w.unit)) throw Error("单位格式错误");
    },
    editor(w) {
      return `${metricSelect(w)}<details class="manual-fields" ${w.host ? "" : "open"}><summary>手动绑定</summary><div class="fields">${field("机器 ID", "host", w.host)}${field("来源 ID", "source", w.source)}${field("指标键", "key", w.key)}${field("单位", "unit", w.unit)}</div></details>`;
    },
    classes(w) {
      return `metric-card ${w.size[0] === 1 ? "narrow" : ""}`;
    },
    preview(w, { width, height }) {
      return `<div class="metric-main"><span class="metric-title">${esc(w.title)}</span>${metricHTML(w, w.unit)}</div>${w.size[0] === 1 && w.size[1] === 1 ? "" : `<div class="metric-host">${esc(host(w.host)?.host_name || w.host)}</div>`}`;
    },
  };
})();
