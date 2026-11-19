/**
 * @file zigbee_device.h
 * @brief EP1：Basic + Identify + OnOff Server；DispText(0xFC10) 由 EP handler 收（无簇表项）。
 *
 * 对拍涂鸦：OnOff 灯 + 自定义文本命令；见 doc/tuya_ed_zcl_mapping.md。
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef ZIGBEE_DEMO_ZIGBEE_DEVICE_H
#define ZIGBEE_DEMO_ZIGBEE_DEVICE_H

/** HA 设备类型：On/Off Light（Zigbee Cluster Library Device ID）。 */
#define ZB_ON_OFF_LIGHT_DEVICE_ID 0x0100

/** 设备版本号，填入 Simple Descriptor 的 DeviceVersion 字段。 */
#define ZB_DEVICE_VER_ON_OFF_LIGHT 0

/**
 * 墨水屏文本簇（厂商自定义）。不进 ZCL 簇表（避免缺 ROLE_INIT）；
 * 由 ZB_AF_SET_ENDPOINT_HANDLER 按 cluster_id 拦截。
 * 涂鸦转换：ep=1 cluster=0xFC10 cmd=0x00 payload=[len]+ASCII
 */
#define ZB_ZCL_CLUSTER_ID_DISP_TEXT 0xFC10U
#define ZB_ZCL_CMD_DISP_TEXT_SET 0x00U

/** Server 侧入簇个数：Basic + Identify + OnOff。 */
#define ZB_ON_OFF_LIGHT_IN_CLUSTER_NUM 3
/** Client 侧出簇个数：本灯设备不主动当 Client，故为 0。 */
#define ZB_ON_OFF_LIGHT_OUT_CLUSTER_NUM 0

#define ZB_ON_OFF_LIGHT_CLUSTER_NUM \
	(ZB_ON_OFF_LIGHT_IN_CLUSTER_NUM + ZB_ON_OFF_LIGHT_OUT_CLUSTER_NUM)

#define ZB_ON_OFF_LIGHT_REPORT_ATTR_COUNT ZB_ZCL_ON_OFF_REPORT_ATTR_COUNT

#define ZB_DECLARE_ON_OFF_LIGHT_CLUSTER_LIST(cluster_list_name, basic_attr_list,               \
					     identify_attr_list, on_off_attr_list)             \
	zb_zcl_cluster_desc_t cluster_list_name[] = {                                          \
		ZB_ZCL_CLUSTER_DESC(ZB_ZCL_CLUSTER_ID_BASIC,                                   \
				    ZB_ZCL_ARRAY_SIZE(basic_attr_list, zb_zcl_attr_t),         \
				    (basic_attr_list), ZB_ZCL_CLUSTER_SERVER_ROLE,             \
				    ZB_ZCL_MANUF_CODE_INVALID),                                \
		ZB_ZCL_CLUSTER_DESC(ZB_ZCL_CLUSTER_ID_IDENTIFY,                                \
				    ZB_ZCL_ARRAY_SIZE(identify_attr_list, zb_zcl_attr_t),      \
				    (identify_attr_list), ZB_ZCL_CLUSTER_SERVER_ROLE,          \
				    ZB_ZCL_MANUF_CODE_INVALID),                                \
		ZB_ZCL_CLUSTER_DESC(ZB_ZCL_CLUSTER_ID_ON_OFF,                                  \
				    ZB_ZCL_ARRAY_SIZE(on_off_attr_list, zb_zcl_attr_t),        \
				    (on_off_attr_list), ZB_ZCL_CLUSTER_SERVER_ROLE,            \
				    ZB_ZCL_MANUF_CODE_INVALID),                                \
	}

#define ZB_ZCL_DECLARE_ON_OFF_LIGHT_SIMPLE_DESC(ep_name, ep_id, in_clust_num, out_clust_num)  \
	ZB_DECLARE_SIMPLE_DESC(in_clust_num, out_clust_num);                                   \
	ZB_AF_SIMPLE_DESC_TYPE(in_clust_num, out_clust_num) simple_desc_##ep_name = {          \
		ep_id,                                                                         \
		ZB_AF_HA_PROFILE_ID,                                                           \
		ZB_ON_OFF_LIGHT_DEVICE_ID,                                                     \
		ZB_DEVICE_VER_ON_OFF_LIGHT,                                                    \
		0,                                                                             \
		in_clust_num,                                                                  \
		out_clust_num,                                                                 \
		{                                                                              \
			ZB_ZCL_CLUSTER_ID_BASIC,                                               \
			ZB_ZCL_CLUSTER_ID_IDENTIFY,                                            \
			ZB_ZCL_CLUSTER_ID_ON_OFF,                                              \
		}                                                                              \
	}

#define ZB_DECLARE_ON_OFF_LIGHT_EP(ep_name, ep_id, cluster_list)                               \
	ZB_ZCL_DECLARE_ON_OFF_LIGHT_SIMPLE_DESC(ep_name, ep_id, ZB_ON_OFF_LIGHT_IN_CLUSTER_NUM, \
						ZB_ON_OFF_LIGHT_OUT_CLUSTER_NUM);              \
	ZBOSS_DEVICE_DECLARE_REPORTING_CTX(reporting_info##ep_name,                            \
					   ZB_ON_OFF_LIGHT_REPORT_ATTR_COUNT);                 \
	ZB_AF_DECLARE_ENDPOINT_DESC(                                                           \
		ep_name, ep_id, ZB_AF_HA_PROFILE_ID, 0, NULL,                                  \
		ZB_ZCL_ARRAY_SIZE(cluster_list, zb_zcl_cluster_desc_t), cluster_list,          \
		(zb_af_simple_desc_1_1_t *)&simple_desc_##ep_name,                             \
		ZB_ON_OFF_LIGHT_REPORT_ATTR_COUNT, reporting_info##ep_name, 0, NULL)

#endif /* ZIGBEE_DEMO_ZIGBEE_DEVICE_H */
