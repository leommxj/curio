(() => {
  function resolveStatus(w) {
    const h = host(w.host);
    if (!h) return { text: "Waiting" };
    const s = source(h, w.source),
      problem = health(h, s);
    if (problem) return { text: problem };
    if (!s) return { text: "Not reported" };
    if (s.kind !== "status") return { text: "Type mismatch" };
    if (s.sample_age_ms > (s.max_age_ms ?? 10000)) return { text: "Stale" };
    return s.state || { text: "Not reported" };
  }
  function statusEntries() {
    return hosts().flatMap((h) =>
      entries(h.sources)
        .filter(([, s]) => s.kind === "status")
        .map(([source_id]) => ({
          host: h.host_id,
          source: source_id,
          label: `${h.host_name || h.host_id} · ${h.host_id} / ${source_id}`,
        })),
    );
  }
  function statusSelect(w) {
    return `<label>数据<select data-status-select><option value="">手动指定</option>${statusEntries()
      .map(
        (s, i) =>
          `<option value="${i}" ${s.host === w.host && s.source === w.source ? "selected" : ""}>${esc(s.label)}</option>`,
      )
      .join("")}</select></label>`;
  }
  widgetTypes.status = {
    label: "状态",
    minimum: [1, 1],
    change(t) {
      if (t.hasAttribute("data-status-select")) {
        const record = t.value === "" ? null : statusEntries()[Number(t.value)];
        if (!record) return true;
        rememberLayout();
        const w =
          page().widgets[Number(t.closest("[data-index]").dataset.index)];
        w.host = record.host;
        w.source = record.source;
        markDisplayDirty();
        render();
        return true;
      }
      return false;
    },
    types: { host: "string", source: "string" },
    defaults: () => ({ title: "Status" }),
    initial: { host: "", source: "" },
    validate(w, check) {
      check(w.host, "机器 ID", 48, true);
      check(w.source, "来源 ID", 48, true);
    },
    editor(w) {
      return `${statusSelect(w)}<details class="manual-fields" open><summary>手动绑定</summary><div class="fields">${field("机器 ID", "host", w.host)}${field("来源 ID", "source", w.source)}</div></details>`;
    },
    refresh(w, editor) {
      editor.querySelector("[data-status-select]").parentElement.outerHTML = statusSelect(w);
    },
    catalog(s) {
      return s.kind === "status" && s.state
        ? `<p class="code status-${esc(s.state.level)}">${esc(s.state.text)}${s.state.detail ? ` · ${esc(s.state.detail)}` : ""}</p>`
        : "";
    },
    classes(w) {
      return `status-card metric-card ${w.size[1] === 1 ? "short-row" : ""} ${w.size[0] === 1 ? "narrow" : ""}`;
    },
    preview(w) {
      const result = resolveStatus(w);
      return `<div class="metric-main"><span class="metric-title">${esc(w.title)}</span><span class="metric-status status-${esc(result.level || "unknown")}">${esc(result.text)}</span></div>${(w.size[1] > 1 || w.size[0] > 1) && result.detail ? `<div class="status-detail">${esc(result.detail)}</div>` : ""}`;
    },
  };
})();
