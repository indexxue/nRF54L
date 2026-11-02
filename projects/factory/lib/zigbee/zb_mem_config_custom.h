/*
 * Factory zbft: ZC + ZR tables so soft-reset can run Coord or Router-DUT.
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef FACTORY_ZB_MEM_CONFIG_CUSTOM_H
#define FACTORY_ZB_MEM_CONFIG_CUSTOM_H 1

/* One role only (zb_mem_config_common.h). ZC tables; DUT still uses router API. */
#define ZB_CONFIG_ROLE_ZC

#define ZB_CONFIG_OVERALL_NETWORK_SIZE 16U
#define ZB_CONFIG_MODERATE_TRAFFIC
#define ZB_CONFIG_APPLICATION_MODERATE

#include "zb_mem_config_common.h"
#include "zb_mem_config_context.h"

#endif /* FACTORY_ZB_MEM_CONFIG_CUSTOM_H */
