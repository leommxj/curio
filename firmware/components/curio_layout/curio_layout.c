#include "curio_layout.h"
#include "curio_module.h"
#include <string.h>

const curio_widget_type_t *curio_widget_type_find(const char *id)
{
    for (size_t i = 0; i < curio_widget_type_count; i++)
        if (!strcmp(curio_widget_types[i]->id, id)) return curio_widget_types[i];
    return NULL;
}
void curio_widget_config_free(curio_widget_config_t *w)
{
    cJSON_free(w->options);
    memset(w, 0, sizeof(*w));
}
bool curio_widget_config_clone(curio_widget_config_t *dst, const curio_widget_config_t *src)
{
    *dst = *src;
    dst->options = cJSON_malloc(src->type->options_size);
    if (!dst->options) return false;
    memcpy(dst->options, src->options, src->type->options_size);
    return true;
}
void curio_page_free(curio_page_config_t *p)
{
    for (size_t i = 0; i < p->widget_count; i++) curio_widget_config_free(&p->widgets[i]);
    memset(p, 0, sizeof(*p));
}
bool curio_page_clone(curio_page_config_t *dst, const curio_page_config_t *src)
{
    *dst = *src;
    dst->widget_count = 0;
    for (size_t i = 0; i < src->widget_count; i++) {
        if (!curio_widget_config_clone(&dst->widgets[i], &src->widgets[i])) {
            curio_page_free(dst);
            return false;
        }
        dst->widget_count++;
    }
    return true;
}
void curio_layout_free(curio_display_config_t *c)
{
    for (size_t i = 0; i < c->page_count; i++) curio_page_free(&c->pages[i]);
    memset(c, 0, sizeof(*c));
}
bool curio_layout_clone(curio_display_config_t *dst, const curio_display_config_t *src)
{
    *dst = *src;
    dst->page_count = 0;
    for (size_t i = 0; i < src->page_count; i++) {
        if (!curio_page_clone(&dst->pages[i], &src->pages[i])) {
            curio_layout_free(dst);
            return false;
        }
        dst->page_count++;
    }
    return true;
}
bool curio_layout_equal(const curio_display_config_t *a, const curio_display_config_t *b)
{
    if (a->page_count != b->page_count || strcmp(a->default_page, b->default_page)) return false;
    for (size_t i = 0; i < a->page_count; i++) {
        const curio_page_config_t *x = &a->pages[i], *y = &b->pages[i];
        if (x->widget_count != y->widget_count || strcmp(x->id, y->id) || strcmp(x->title, y->title)) return false;
        for (size_t j = 0; j < x->widget_count; j++) {
            const curio_widget_config_t *u = &x->widgets[j], *v = &y->widgets[j];
            if (u->type != v->type || strcmp(u->id, v->id) || strcmp(u->title, v->title) ||
                u->col != v->col || u->row != v->row || u->cols != v->cols || u->rows != v->rows ||
                memcmp(u->options, v->options, u->type->options_size)) return false;
        }
    }
    return true;
}
unsigned curio_layout_services(const curio_display_config_t *c)
{
    unsigned services = 0;
    for (size_t p = 0; p < c->page_count; p++)
        for (size_t w = 0; w < c->pages[p].widget_count; w++) services |= c->pages[p].widgets[w].type->services;
    return services;
}
bool curio_layout_default(curio_display_config_t *c)
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
    w->type = curio_widget_type_find("agents");
    strcpy(w->title, w->type->title);
    w->cols = CURIO_LAYOUT_COLUMNS;
    w->rows = CURIO_LAYOUT_ROWS;
    w->options = cJSON_malloc(w->type->options_size);
    if (w->options && w->type->parse(NULL, w->options)) return true;
    curio_layout_free(c);
    return false;
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
        !curio_identifier(c->default_page, 32))
        return CURIO_INVALID;
    bool found = false;
    for (size_t i = 0; i < c->page_count; i++) {
        const curio_page_config_t *p = &c->pages[i];
        if (!curio_identifier(p->id, 32) || strlen(p->title) > 48 ||
            p->widget_count > CURIO_LAYOUT_MAX_WIDGETS)
            return CURIO_INVALID;
        found |= !strcmp(p->id, c->default_page);
        for (size_t j = 0; j < i; j++)
            if (!strcmp(p->id, c->pages[j].id))
                return CURIO_INVALID;
        uint32_t occupied = 0;
        for (size_t j = 0; j < p->widget_count; j++) {
            const curio_widget_config_t *w = &p->widgets[j];
            if (!curio_identifier(w->id, 32) || strlen(w->title) > 48 || !w->type || !w->options)
                return CURIO_INVALID;
            for (size_t k = 0; k < j; k++)
                if (!strcmp(w->id, p->widgets[k].id))
                    return CURIO_INVALID;
            if (w->cols < w->type->min_cols || w->rows < w->type->min_rows || w->cols > CURIO_LAYOUT_COLUMNS ||
                w->rows > CURIO_LAYOUT_ROWS || w->col > CURIO_LAYOUT_COLUMNS - w->cols ||
                w->row > CURIO_LAYOUT_ROWS - w->rows || !w->type->validate(w->options))
                return CURIO_INVALID;
            uint32_t cells = 0;
            for (unsigned row = w->row; row < w->row + w->rows; row++)
                cells |= ((1U << w->cols) - 1) << (row * CURIO_LAYOUT_COLUMNS + w->col);
            if (occupied & cells)
                return CURIO_INVALID;
            occupied |= cells;
        }
    }
    return found ? CURIO_OK : CURIO_INVALID;
}
