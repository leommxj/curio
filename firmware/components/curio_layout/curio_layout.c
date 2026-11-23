#include "curio_layout.h"
#include <string.h>
static bool id(const char *s, size_t max)
{
    size_t n = strlen(s);
    if (!n || n > max)
        return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              strchr("._-", *s)))
            return false;
    return true;
}
static bool unit(const char *s)
{
    if (strlen(s) > 16)
        return false;
    for (; *s; s++)
        if ((unsigned char)*s < 33 || (unsigned char)*s > 126)
            return false;
    return true;
}
void curio_layout_default(curio_display_config_t *c)
{
    memset(c, 0, sizeof(*c));
    strcpy(c->default_page, "agents");
    c->page_count = 1;
    curio_page_config_t *p = c->pages;
    strcpy(p->id, "agents");
    strcpy(p->title, "Agents");
    p->widget_count = 1;
    curio_widget_config_t *w = p->widgets;
    strcpy(w->id, "agents");
    strcpy(w->title, "Agents");
    strcpy(w->source_id, "herdr");
    w->type = CURIO_WIDGET_AGENTS;
    w->auto_page = true;
    w->cols = CURIO_LAYOUT_COLUMNS;
    w->rows = CURIO_LAYOUT_ROWS;
    w->all_hosts = true;
}
curio_widget_rect_t curio_widget_rect(const curio_widget_config_t *w)
{
    int x = (int)(w->col * 464 / CURIO_LAYOUT_COLUMNS);
    int y = (int)(w->row * 416 / CURIO_LAYOUT_ROWS);
    return (curio_widget_rect_t){
        .x = 12 + x,
        .y = 60 + y,
        .width = (int)((w->col + w->cols) * 464 / CURIO_LAYOUT_COLUMNS) - x - 8,
        .height = (int)((w->row + w->rows) * 416 / CURIO_LAYOUT_ROWS) - y - 8,
    };
}
curio_result_t curio_layout_validate(const curio_display_config_t *c)
{
    if (!c || c->revision >= UINT32_MAX - 1ULL || !c->page_count || c->page_count > CURIO_LAYOUT_MAX_PAGES ||
        !id(c->default_page, 32))
        return CURIO_INVALID;
    bool found = false;
    for (size_t i = 0; i < c->page_count; i++) {
        const curio_page_config_t *p = &c->pages[i];
        if (!id(p->id, 32) || strlen(p->title) > 48 ||
            p->widget_count > CURIO_LAYOUT_MAX_WIDGETS)
            return CURIO_INVALID;
        found |= !strcmp(p->id, c->default_page);
        for (size_t j = 0; j < i; j++)
            if (!strcmp(p->id, c->pages[j].id))
                return CURIO_INVALID;
        uint32_t occupied = 0;
        for (size_t j = 0; j < p->widget_count; j++) {
            const curio_widget_config_t *w = &p->widgets[j];
            if (!id(w->id, 32) || strlen(w->title) > 48 || w->type > CURIO_WIDGET_CLOCK)
                return CURIO_INVALID;
            for (size_t k = 0; k < j; k++)
                if (!strcmp(w->id, p->widgets[k].id))
                    return CURIO_INVALID;
            unsigned minimum = w->type == CURIO_WIDGET_AGENTS ? 3 : w->type == CURIO_WIDGET_MACHINE ? 2 : 1;
            if (w->cols < minimum || w->rows < minimum || w->cols > CURIO_LAYOUT_COLUMNS ||
                w->rows > CURIO_LAYOUT_ROWS || w->col > CURIO_LAYOUT_COLUMNS - w->cols ||
                w->row > CURIO_LAYOUT_ROWS - w->rows)
                return CURIO_INVALID;
            uint32_t cells = 0;
            for (unsigned row = w->row; row < w->row + w->rows; row++)
                cells |= ((1U << w->cols) - 1) << (row * CURIO_LAYOUT_COLUMNS + w->col);
            if (occupied & cells)
                return CURIO_INVALID;
            occupied |= cells;
            if (w->type == CURIO_WIDGET_METRIC || w->type == CURIO_WIDGET_STATUS) {
                if (!id(w->host_id, 48) || !id(w->source_id, 48))
                    return CURIO_INVALID;
            }
            if (w->type == CURIO_WIDGET_METRIC && (!id(w->key, 48) || !unit(w->unit)))
                return CURIO_INVALID;
            if (w->type == CURIO_WIDGET_CLOCK && (w->utc_offset_minutes < -720 || w->utc_offset_minutes > 840 ||
                *w->host_id || *w->source_id)) return CURIO_INVALID;
            if (w->type == CURIO_WIDGET_MACHINE && !id(w->host_id, 48))
                return CURIO_INVALID;
            if (w->type == CURIO_WIDGET_AGENTS) {
                if (!id(w->source_id, 48) || w->host_count > CURIO_MAX_HOSTS || (w->all_hosts && w->host_count) ||
                    w->filter > CURIO_FILTER_WORKING || strlen(w->project) > 48 ||
                    strlen(w->agent) > 96)
                    return CURIO_INVALID;
                for (size_t k = 0; k < w->host_count; k++) {
                    if (!id(w->hosts[k], 48))
                        return CURIO_INVALID;
                    for (size_t n = 0; n < k; n++)
                        if (!strcmp(w->hosts[k], w->hosts[n]))
                            return CURIO_INVALID;
                }
            }
        }
    }
    return found ? CURIO_OK : CURIO_INVALID;
}
