(() => {
  widgetTypes.machine = {
    label: "单机卡",
    minimum: [2, 2],
    types: { host: "string" },
    defaults: () => ({ title: "Machine" }),
    initial: { host: "" },
    validate(w, check) {
      check(w.host, "机器 ID", 48, true);
    },
    editor(w) {
      return field("机器 ID", "host", w.host);
    },
    classes(w) {
      return `machine-card`;
    },
    preview(w, { width, height }) {
      return `<span>${esc(w.title || host(w.host)?.host_name || w.host)}</span><div class="metric-host">${esc(w.host)} · ${esc(host(w.host)?.health || "Waiting")}</div>${[
        ["CPU", "system.cpu"],
        ["RAM", "system.memory"],
      ]
        .map(
          ([name, id]) =>
            `<div class="machine-value"><span>${name}</span>${metricHTML({ host: w.host, source: id, key: "usage_pct" }, "percent")}</div>`,
        )
        .join("")}`;
    },
  };
})();
