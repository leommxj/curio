#ifndef CURIO_CORE_H
#define CURIO_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CURIO_MAX_HOSTS 4
#define CURIO_MAX_AGENTS 16
#define CURIO_MAX_SOURCES 8
#define CURIO_MAX_METRICS 32
#define CURIO_MAX_BODY_BYTES 16384
#define CURIO_STALE_MS UINT64_C(10000)
#define CURIO_MAX_AGE_MS UINT64_C(604800000)
#define CURIO_OFFLINE_MS UINT64_C(30000)
#define CURIO_EVICT_OFFLINE_MS UINT64_C(300000)

typedef enum {
    CURIO_STATE_BLOCKED,
    CURIO_STATE_WORKING,
    CURIO_STATE_DONE,
    CURIO_STATE_IDLE,
    CURIO_STATE_UNKNOWN
} curio_agent_state_t;

typedef enum {
    CURIO_HEALTH_FRESH,
    CURIO_HEALTH_STALE,
    CURIO_HEALTH_UNAVAILABLE,
    CURIO_HEALTH_OFFLINE
} curio_health_t;

typedef enum {
    CURIO_OK,
    CURIO_INVALID,
    CURIO_TOO_LARGE,
    CURIO_CAPACITY,
    CURIO_NO_MEMORY
} curio_result_t;

typedef enum { CURIO_FILTER_ALL, CURIO_FILTER_ATTENTION, CURIO_FILTER_WORKING } curio_filter_t;

typedef struct {
    char id[97];
    char name[65];
    char brand[33];
    char project[49];
    curio_agent_state_t state;
} curio_agent_t;

typedef enum { CURIO_SOURCE_METRICS, CURIO_SOURCE_AGENTS, CURIO_SOURCE_STATUS } curio_source_kind_t;
typedef enum {
    CURIO_SOURCE_OK,
    CURIO_SOURCE_ERROR,
    CURIO_SOURCE_UNAVAILABLE,
    CURIO_SOURCE_WARMING_UP
} curio_source_status_t;
typedef struct {
    char key[49];
    char unit[17];
    double value;
    bool has_value;
    uint64_t value_age_ms;
    uint64_t received_ms;
} curio_metric_t;
typedef enum { CURIO_STATUS_NEUTRAL, CURIO_STATUS_OK, CURIO_STATUS_WARNING, CURIO_STATUS_ERROR } curio_status_level_t;
typedef struct {
    char text[65], detail[161];
    curio_status_level_t level;
} curio_status_t;
typedef struct {
    char id[49];
    curio_source_kind_t kind;
    curio_source_status_t status;
    uint64_t sample_age_ms, received_ms, max_age_ms;
    bool has_success;
    size_t metric_count, agent_count;
    curio_metric_t *metrics;
    curio_agent_t *agents;
    curio_status_t *state;
} curio_source_t;
typedef struct {
    bool occupied;
    char host_id[49], host_name[49];
    uint64_t last_report_ms;
    size_t source_count;
    curio_source_t *sources;
} curio_host_t;
typedef struct {
    void *(*alloc)(size_t);
    void (*free)(void *);
} curio_allocator_t;
typedef struct {
    curio_host_t hosts[CURIO_MAX_HOSTS];
    curio_allocator_t allocator;
} curio_core_t;
typedef struct curio_snapshot curio_snapshot_t;
bool curio_utf8_valid(const unsigned char *, size_t);
bool curio_json_valid(const char *, size_t);
void curio_core_init_allocator(curio_core_t *, curio_allocator_t);
void curio_core_destroy(curio_core_t *);
/* Parse does not retain body. Apply consumes contents only on success; always free the snapshot. */
curio_result_t curio_core_parse(const curio_core_t *, const char *, size_t, curio_snapshot_t **);
curio_result_t curio_core_apply(curio_core_t *, curio_snapshot_t *, uint64_t);
void curio_core_snapshot_free(const curio_core_t *, curio_snapshot_t *);
const curio_host_t *curio_core_host(const curio_core_t *, const char *);
const curio_source_t *curio_host_source(const curio_host_t *, const char *);
const curio_metric_t *curio_source_metric(const curio_source_t *, const char *);
curio_health_t curio_host_health(const curio_host_t *, uint64_t);
curio_health_t curio_source_health(const curio_source_t *, uint64_t);
const char *curio_status_level_name(curio_status_level_t);
void curio_format_metric(char *, size_t, double, const char *);
uint64_t curio_metric_age(const curio_metric_t *, uint64_t);
const char *curio_source_status_name(curio_source_status_t);
char *curio_core_catalog_json(const curio_core_t *, uint64_t);
char *curio_core_state_v2_json(const curio_core_t *, uint64_t);

/* Single owner or external lock required, including reads and returned pointers.
 * now_ms is a receiver monotonic clock. Initialize once before any other call. */
void curio_core_init(curio_core_t *core);
curio_result_t curio_core_apply_json(curio_core_t *core, const char *body, size_t length,
                                     uint64_t now_ms);
const char *curio_health_name(curio_health_t health);
const char *curio_state_name(curio_agent_state_t state);

/* Caller owns returned JSON; NULL means allocation failure. */
void curio_core_free_json(char *json);

#ifdef __cplusplus
}
#endif
#endif
