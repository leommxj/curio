#pragma once

#ifdef CURIO_HAS_LOCAL_CONFIG
#include "curio_config.h"
#endif

#ifndef CURIO_ADMIN_TOKEN
#define CURIO_ADMIN_TOKEN ""
#endif

#ifndef CURIO_CONFIGURED
#define CURIO_CONFIGURED 0
#endif
#ifndef CURIO_HOSTNAME
#define CURIO_HOSTNAME "curio"
#endif

#if !CURIO_CONFIGURED
#define CURIO_WIFI_SSID ""
#define CURIO_WIFI_PASSWORD ""
#define CURIO_DEVICE_TOKEN ""
#endif
