(() => {
  const previewAgents = new Map();
  function turn(id, direction) {
    const w = page().widgets.find(
      (w) => w.id === id && widgetTypes[w.type].view,
    );
    if (!w) return;
    const view = widgetTypes[w.type].view(w),
      capacity = Math.max(1, Math.floor((pixelRect(w).height - 72) / 47));
    const pages = Math.max(
      1,
      Math.ceil(
        widgetTypes[w.type].rows(w, view.filter).rows.length / capacity,
      ),
    );
    view.page = Math.max(0, Math.min(pages - 1, view.page + direction));
    view.due = performance.now() + 10000;
    renderPreview();
  }
  function agentText(a, hostName) {
    const main =
      a.brand && a.name && a.name !== a.brand
        ? a.name
        : a.project || a.name || a.brand || "Agent";
    const detail = [
      ...new Set(
        [
          a.brand || a.name,
          a.project !== main ? a.project : "",
          hostName,
        ].filter(Boolean),
      ),
    ].join(" · ");
    return { main, detail };
  }
  function agentPreview(w) {
    const key = `${page().id}/${w.id}`;
    if (!previewAgents.has(key))
      previewAgents.set(key, {
        page: 0,
        filter: w.filter,
        due: performance.now() + 10000,
      });
    return previewAgents.get(key);
  }
  function agentRows(w, filter = w.filter) {
    let rows = [],
      missing = [];
    const chosen = w.hosts.includes("*")
      ? hosts()
      : w.hosts.map((id) => host(id) || { host_id: id });
    for (const h of chosen) {
      const s = source(h, w.source);
      const problem = !host(h.host_id)
        ? "Waiting"
        : s && s.kind !== "agents"
          ? "Type mismatch"
          : health(h, s) || (!s ? "Not reported" : "");
      if (problem) missing.push({ h, problem });
      for (const a of s?.kind === "agents" ? s.agents || [] : []) {
        if (
          (w.project && a.project !== w.project) ||
          (w.agent && a.id !== w.agent)
        )
          continue;
        if (
          (filter === "attention" && a.state !== "blocked") ||
          (filter === "working" && a.state !== "working")
        )
          continue;
        rows.push({ h, a, problem });
      }
    }
    const rank = { blocked: 0, working: 1, idle: 2, done: 3, unknown: 4 };
    rows.sort(
      (a, b) =>
        (rank[a.a.state] ?? 4) - (rank[b.a.state] ?? 4) ||
        a.h.host_id.localeCompare(b.h.host_id) ||
        (a.a.id || "").localeCompare(b.a.id || ""),
    );
    return { rows, missing };
  }
  widgetTypes.agents = {
    label: "Agent 列表",
    minimum: [3, 3],
    types: {
      hosts: "array",
      source: "string",
      filter: "string",
      auto_page: "boolean",
      project: "string",
      agent: "string",
    },
    defaults: () => ({
      title: "Agents",
      hosts: ["*"],
      source: "herdr",
      filter: "all",
      auto_page: true,
      project: "",
      agent: "",
    }),
    clear() {
      previewAgents.clear();
    },
    invalidate(w) {
      previewAgents.delete(`${page().id}/${w.id}`);
    },
    reset() {
      for (const view of previewAgents.values())
        view.due = performance.now() + 10000;
    },
    click(t) {
      if (t.dataset.previewFilter) {
        const w = page().widgets.find((w) => w.id === t.dataset.widgetId),
          view = agentPreview(w);
        view.filter = t.dataset.previewFilter;
        view.page = 0;
        view.due = performance.now() + 10000;
        renderPreview();
        return true;
      }
      if (t.dataset.agentTurn) {
        turn(t.dataset.widgetId, Number(t.dataset.agentTurn));
        return true;
      }
      return false;
    },
    tick(w) {
      if (w.auto_page === false) return false;
      const view = agentPreview(w);
      if (performance.now() < view.due) return false;
      const capacity = Math.max(1, Math.floor((pixelRect(w).height - 72) / 47));
      const pages = Math.max(
        1,
        Math.ceil(agentRows(w, view.filter).rows.length / capacity),
      );
      view.page = (view.page + 1) % pages;
      view.due = performance.now() + 10000;
      return true;
    },
    catalog(s, h) {
      return (s.agents || [])
        .map((a) => {
          const t = agentText(a, h.host_name || h.host_id);
          return `<p class="catalog-agent"><strong>${esc(t.main)}</strong><span>${esc(a.state)}</span><small>${esc(t.detail)} · ${esc(a.id)}</small></p>`;
        })
        .join("");
    },
    view: agentPreview,
    rows: agentRows,
    validate(w, check) {
      if (!Array.isArray(w.hosts)) throw Error("hosts 须为列表");
      if (!["all", "attention", "working"].includes(w.filter))
        throw Error("filter 格式错误");
      if (w.hosts.length > 4) throw Error("最多 4 台机器");
      if (w.hosts.includes("*") && w.hosts.length !== 1)
        throw Error("全部机器不能与机器 ID 混用");
      if (new Set(w.hosts).size !== w.hosts.length) throw Error("机器 ID 重复");
      for (const h of w.hosts) if (h !== "*") check(h, "机器 ID", 48, true);
      check(w.source, "来源 ID", 48, true);
      check(w.project, "项目", 48, false, true);
      check(w.agent, "Agent ID", 96, false, true);
    },
    editor(w) {
      return `<div class="fields">${field("机器 ID（逗号分隔，* 为全部）", "hosts", w.hosts.join(", "))}${field("来源 ID", "source", w.source)}${field(
        "默认状态",
        "filter",
        w.filter,
        [
          ["all", "全部"],
          ["attention", "需关注"],
          ["working", "进行中"],
        ],
      )}</div><details class="manual-fields"><summary>筛选条件</summary><div class="fields">${field("项目", "project", w.project)}${field("Agent ID", "agent", w.agent)}</div></details><label class="switch-field"><input type="checkbox" role="switch" data-field="auto_page" ${w.auto_page !== false ? "checked" : ""}><span>自动翻页</span><small>10 秒</small></label>`;
    },
    classes(w) {
      return `agent-card`;
    },
    preview(w, { width, height }) {
      let html = "";
      const view = agentPreview(w),
        { rows, missing } = agentRows(w, view.filter),
        capacity = Math.max(1, Math.floor((height - 72) / 47));
      const pages = Math.max(1, Math.ceil(rows.length / capacity));
      view.page = Math.min(view.page, pages - 1);
      html += `<div class="filters">${["all", "attention", "working"].map((f, i) => `<button data-preview-filter="${f}" data-widget-id="${esc(w.id)}" class="${view.filter === f ? "active" : ""}">${["All", "Attention", "Working"][i]}</button>`).join("")}</div>`;
      html +=
        rows
          .slice(view.page * capacity, (view.page + 1) * capacity)
          .map(({ h, a, problem }) => {
            const t = agentText(a, h.host_name || h.host_id);
            return `<div class="agent-row"><div class="agent-name"><strong>${esc(t.main)}</strong><span class="state-${esc(a.state)}">${esc(problem || a.state)}</span></div><div class="agent-detail">${esc(t.detail)}</div></div>`;
          })
          .join("") || '<div class="empty">No matching agents</div>';
      const summary = missing
        .map(({ h, problem }) => `${h.host_name || h.host_id}: ${problem}`)
        .join(" · ");
      html += `<div class="agent-summary" title="${esc(summary)}">${esc(summary)}</div><div class="agent-footer"><button data-agent-turn="-1" data-widget-id="${esc(w.id)}" ${view.page === 0 ? "disabled" : ""} aria-label="上一页">‹</button><span>${rows.length} agents · ${view.page + 1} / ${pages}</span><button data-agent-turn="1" data-widget-id="${esc(w.id)}" ${view.page >= pages - 1 ? "disabled" : ""} aria-label="下一页">›</button></div>`;
      return html;
    },
  };
})();
