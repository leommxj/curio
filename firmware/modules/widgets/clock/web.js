(() => {
  function clockOffset(value) {
    if (typeof value !== "string" || !/^[+-]\d{2}:[0-5]\d$/.test(value))
      throw Error("UTC 偏移须为 ±HH:MM（-12:00 至 +14:00）");
    const minutes =
      (Number(value.slice(1, 3)) * 60 + Number(value.slice(4))) *
      (value[0] === "-" ? -1 : 1);
    if (minutes < -720 || minutes > 840)
      throw Error("UTC 偏移须为 -12:00 至 +14:00");
    return minutes;
  }
  function clockTitle(w, width) {
    if (w.title) return w.title;
    let offset;
    try {
      offset = clockOffset(w.utc_offset);
    } catch {
      return "Time";
    }
    const minutes = Math.abs(offset),
      zone = offset
        ? `${offset < 0 ? "-" : "+"}${Math.floor(minutes / 60)}${minutes % 60 ? ":" + String(minutes % 60).padStart(2, "0") : ""}`
        : "",
      title = `Time (UTC${zone})`,
      context = document.createElement("canvas").getContext("2d");
    context.font = `14px ${getComputedStyle($("#screen")).fontFamily}`;
    return context.measureText(title).width <= width - 16 ? title : "Time";
  }
  function clockTypography(w, width, height) {
    const context = document.createElement("canvas").getContext("2d"),
      family = getComputedStyle($("#screen")).fontFamily,
      available = height - (w.size[1] > 1 ? 56 : 28);
    for (const size of [48, 32, 26, 18, 14]) {
      context.font = `${size}px ${family}`;
      const line = Math.ceil(size * 1.2);
      if (
        context.measureText(w.seconds ? "88:88:88" : "88:88").width <=
          width - 16 &&
        line <= available
      )
        return `font-size:${size}px;line-height:${line}px;top:${25 + Math.floor((available - line) / 2)}px`;
    }
    return "font-size:14px;line-height:17px;top:25px";
  }
  function clockParts(w, epoch = Date.now()) {
    const date = new Date(epoch + clockOffset(w.utc_offset) * 60000);
    const iso = date.toISOString();
    return { time: iso.slice(11, w.seconds ? 19 : 16), date: iso.slice(0, 10) };
  }
  widgetTypes.clock = {
    label: "时钟",
    minimum: [1, 1],
    types: { utc_offset: "string", seconds: "boolean" },
    defaults: () => ({ title: "", utc_offset: "+00:00", seconds: false }),
    title: clockTitle,
    parts: clockParts,
    titlePlaceholder: "自动",
    browserTime: true,
    tick: (w, secondChanged) => secondChanged,
    validate(w, check) {
      clockOffset(w.utc_offset);
    },
    editor(w) {
      return `${field("UTC 偏移", "utc_offset", w.utc_offset)}<label class="switch-field"><input type="checkbox" role="switch" data-field="seconds" ${w.seconds ? "checked" : ""}><span>显示秒</span></label>`;
    },
    classes(w) {
      return `clock-card`;
    },
    preview(w, { width, height }) {
      let parts;
      try {
        parts = clockParts(w);
      } catch {
        parts = { time: "--:--", date: "" };
      }
      return `<span class="clock-title">${esc(clockTitle(w, width))}</span><span class="clock-time" style="${clockTypography(w, width, height)}">${parts.time}</span>${w.size[1] > 1 ? `<span class="clock-date">${parts.date}</span>` : ""}`;
    },
  };
})();
