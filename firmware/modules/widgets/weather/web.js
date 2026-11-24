(() => {
  const results = new WeakMap();
  function widget(t) {
    return page().widgets[Number(t.closest("[data-index]").dataset.index)];
  }
  async function search(t) {
    const box = t.closest(".weather-search"),
      query = box.querySelector("input").value.trim(),
      output = box.querySelector(".weather-results");
    if (!query) return;
    t.disabled = true;
    output.textContent = "搜索中…";
    try {
      const response = await fetch(`https://geocoding-api.open-meteo.com/v1/search?name=${encodeURIComponent(query)}&count=5&language=zh&format=json`);
      if (!response.ok) throw Error("城市搜索失败");
      const data = await response.json(), records = data.results || [];
      if (!box.isConnected) return;
      results.set(box, records);
      output.innerHTML = records.length ? records.map((r, i) =>
        `<button type="button" data-weather-result="${i}">${esc([r.name, r.admin1, r.country].filter(Boolean).join(" · "))}</button>`
      ).join("") : "未找到城市";
    } catch (e) {
      output.textContent = `搜索失败：${e.message}`;
    } finally {
      t.disabled = false;
    }
  }
  widgetTypes.weather = {
    label: "天气",
    minimum: [1, 1],
    types: { city: "string", latitude: "number", longitude: "number" },
    defaults: () => ({ title: "" }),
    initial: { city: "", latitude: 0, longitude: 0 },
    titlePlaceholder: "城市名称",
    validate(w, check) {
      check(w.city, "城市", 64);
      for (const [key, max] of [["latitude", 90], ["longitude", 180]])
        if (typeof w[key] !== "number" || !Number.isFinite(w[key]) || Math.abs(w[key]) > max)
          throw Error(`${key === "latitude" ? "纬度" : "经度"}须为 ${-max} 至 ${max} 的数字`);
    },
    click(t) {
      if (t.hasAttribute("data-weather-search")) {
        void search(t);
        return true;
      }
      if (!t.hasAttribute("data-weather-result")) return false;
      const r = results.get(t.closest(".weather-search"))?.[Number(t.dataset.weatherResult)];
      if (r) {
        rememberLayout();
        Object.assign(widget(t), { city: r.name, latitude: r.latitude, longitude: r.longitude });
        markDisplayDirty();
        render();
      }
      return true;
    },
    change(t) {
      if (!t.hasAttribute("data-weather-coordinate")) return false;
      rememberLayout();
      widget(t)[t.dataset.weatherCoordinate] = t.value.trim() === "" ? null : Number(t.value);
      markDisplayDirty();
      renderPreview();
      return true;
    },
    editor(w) {
      return `<div class="weather-search"><label>搜索城市<input data-weather-query value="${esc(w.city || "")}" maxlength="64"></label><button type="button" data-weather-search>搜索</button><div class="weather-results" aria-live="polite"></div></div><details class="manual-fields"><summary>城市与坐标</summary><div class="fields"><label>城市<input data-field="city" value="${esc(w.city || "")}" maxlength="64"></label>${[ ["latitude", "纬度", 90], ["longitude", "经度", 180] ].map(([key, label, max]) => `<label>${label}<input type="number" step="any" min="${-max}" max="${max}" data-weather-coordinate="${key}" value="${esc(w[key] ?? "")}"></label>`).join("")}</div></details><p class="weather-provider">天气数据：<a href="https://open-meteo.com/" target="_blank" rel="noopener noreferrer">Open-Meteo</a></p>`;
    },
    classes: () => "weather-card",
    preview(w, { height }) {
      const tall = w.size[1] > 1, wide = w.size[0] > 1,
        top = tall && height > 142 ? Math.floor((height - 86) / 2) : 25;
      return `<span class="weather-title">${esc(w.title || w.city || "天气")}</span><span class="weather-temperature" style="top:${top}px;font-size:${tall && wide ? 48 : 26}px;${!tall && wide ? "right:50%" : ""}">--°</span>${tall || wide ? `<span class="weather-condition" style="${tall ? "bottom:25px" : "top:30px;left:50%;text-align:right"}">Waiting</span>` : ""}`;
    },
  };
})();
