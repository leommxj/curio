# Widgets and alerts

Add each type under `firmware/modules/widgets/<name>/` or `firmware/modules/alerts/<name>/`, then add its name to `firmware/modules/registry.json`. That registry drives firmware, host tests and browser bundles.

| File | Purpose |
| --- | --- |
| `config.h` | Options structure and type descriptor declaration |
| `config.c` for widgets | Configuration parsing, validation and YAML output |
| `view.c`, `view.h` for widgets | LVGL view, sampled values and rendering |
| `rule.c` for alerts | Configuration, detection state and notifications |
| `web.js` | Browser schema, editor and preview or summary |
| `style.css` | Optional module styles |
| `test.c` | Module tests |
| `cases.json` | Shared C/browser configuration cases |

Supporting files stay in the same directory. Shared helpers live in `modules/config.c`, `modules/view.c` and `modules/include/`. Page controls, persistence, geometry and notification delivery remain in the common components.

## Configuration

Export `curio_widget_<name>` (`curio_widget_type_t`) or `curio_alert_<name>` (`curio_alert_type_t`). Descriptors declare the type name, allowed fields, `options_size`, and `parse`, `validate`, `write` callbacks. Widgets also declare their default title and minimum grid size; alerts provide a title and `describe` for device rule details.

Common code handles widget `id/type/title/at/size` and alert `id/type/enabled/duration/sound/vibrate/flash`. Callbacks receive the full mapping and handle their own fields. `parse` initializes the entire options structure, including padding, and validates it. `validate` also checks options changed through C APIs. `write` omits optional values equal to their defaults.

Options contain plain values and fixed arrays. Common code allocates, copies, compares and frees them using `options_size`. Initialize `curio_config_t` to zero; parsing replaces it only on success. Use clone/free APIs for owned configurations. A structure assignment transfers ownership only when the source is cleared afterward.

## Widget views

Export `curio_widget_<name>_view` (`curio_widget_view_t`):

- `create` builds children under the supplied card and allocates one private data block with `cJSON_malloc`.
- `sample` copies display values from the supplied data model, time and interaction state while the application holds its data lock.
- `render` updates LVGL from those values after the lock is released.

Common code deletes the card and frees its data and configuration. Only the active page creates views. Views share the UI task; sampling must not call LVGL or retain model pointers. Use `invalidate` to request a refresh after input.

`services` declares shared requirements such as `CURIO_SERVICE_TIME`, which enables SNTP while any configured widget needs it. Optional `paging` and `set_paging` callbacks expose automatic paging in device Settings.

## Alert rules

Declare `state_size` and `evaluate`. The engine allocates a zeroed state block only for enabled rules and frees it when rules change. The block contains plain values and fixed arrays, with per-host state indexed by the supplied core's host slots. Reset a slot when its host ID changes or becomes unoccupied.

`evaluate` receives the data model and monotonic time. Use its `emit` callback for a notification with level, title, body and detail, plus a key unique within the rule (up to 148 bytes). Repeated rule/key pairs are deduplicated. `cancel` removes matching pending notifications while preserving the visible one.

The engine owns priority, queue capacity, timeout and dismissal. It applies configured sound, vibration and flash settings. Changing rules resets detection and the queue; changing global output levels preserves them.

## Browser modules

Each `web.js` registers a descriptor in `widgetTypes` or `alertTypes`. `types` maps YAML field names to `string`, `number`, `boolean` or `array`; it also supplies the allowed field list. Declare `label`, `defaults`, `validate` and `editor`. Widgets add `minimum`, `classes` and `preview`; alerts add `summary`. Optional `initial` values suggest inputs for new entries, without filling required fields during import.

Common code manages IDs, geometry, YAML drafts, saves and ETags. Modules can handle specialized input with `change` and `click`, manage preview state with `tick`, `clear`, `invalidate` and `reset`, and add source details with `catalog`. Shared helpers are available when callbacks run.

## Build and tests

`scripts/modules.py` generates C registries, source lists and combined `/app.js` and `/style.css` assets under `build/modules/`.

Name module tests `test_widget_<name>()` or `test_alert_<name>()` in `test.c`; the layout and alert suites run them automatically. Each `cases.json` entry has `name`, `options` and `valid`, plus optional `expected` normalized values. The browser suite checks each case against both parsers. Shared configuration, UI and browser scenarios remain in `tests/`; see [test commands](../tests/README.md).
