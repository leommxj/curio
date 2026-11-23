#include "curio_clock.h"
#include "esp_netif_sntp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned starts, stops;
static bool active, fail;
static const char *server_pointer;
static char latest[128];
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config)
{
    assert(!active);
    assert(!config->wait_for_sync);
    starts++;
    server_pointer = config->server;
    strcpy(latest, config->server);
    active = !fail;
    return fail ? -1 : ESP_OK;
}
void esp_netif_sntp_deinit(void)
{
    assert(active);
    assert(!strcmp(server_pointer, latest));
    active = false;
    stops++;
}
int main(void)
{
    curio_clock_update(false, true, false, "first");
    curio_clock_update(true, false, false, "first");
    assert(!starts && !stops);
    char server[128] = "first";
    curio_clock_update(true, true, false, server);
    strcpy(server, "caller-changed");
    assert(active && starts == 1 && !strcmp(server_pointer, "first"));
    curio_clock_update(true, true, false, "first");
    assert(starts == 1 && !stops);
    curio_clock_update(true, true, false, "second");
    assert(starts == 2 && stops == 1 && !strcmp(latest, "second"));
    curio_clock_update(true, true, true, "second");
    assert(starts == 3 && stops == 2);
    curio_clock_update(false, true, false, "second");
    assert(!active && stops == 3);
    curio_clock_update(true, true, false, "third");
    curio_clock_update(true, false, false, "third");
    assert(!active && starts == 4 && stops == 4);
    fail = true;
    curio_clock_update(true, true, false, "failed");
    curio_clock_update(true, true, false, "failed");
    assert(!active && starts == 6 && stops == 4);
    fail = false;
    curio_clock_update(true, true, false, "latest");
    assert(active && starts == 7 && !strcmp(latest, "latest"));
    curio_clock_update(false, false, false, "latest");
    assert(!active && stops == 5);
    puts("SNTP lifecycle tests passed");
}
