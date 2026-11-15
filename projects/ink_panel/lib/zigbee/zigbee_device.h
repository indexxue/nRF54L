/**
 * @file zigbee_device.h
 * @brief 本工程 Zigbee 设备描述：EP 上的 Basic + Identify + OnOff Server 声明宏。
 *
 * 仅含 ZBOSS 设备/端点宏，无业务逻辑；实现与回调见 zigbee_app.c。
 * 源自 Nordic HA On/Off Light 样例模式，宏名保留 ZB_* 以对接栈。
 *
 * 组装关系（在 zigbee_app.c 中展开）：
 *   1) ZB_ZCL_DECLARE_*_ATTRIB_LIST  → 各簇属性表（指向 s_dev_ctx）
 *   2) ZB_DECLARE_ON_OFF_LIGHT_CLUSTER_LIST → 把 Basic/Identify/OnOff 绑成簇列表
 *   3) ZB_DECLARE_ON_OFF_LIGHT_EP → Simple Descriptor + Endpoint + 上报上下文
 *   4) ZBOSS_DECLARE_DEVICE_CTX_1_EP → 单 EP 设备上下文交给协议栈
 *
 * 对拍涂鸦 / 网关：Device ID 0x0100、Profile HA、EP1；见 doc/tuya_ed_zcl_mapping.md。
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef ZIGBEE_DEMO_ZIGBEE_DEVICE_H
#define ZIGBEE_DEMO_ZIGBEE_DEVICE_H

/** HA 设备类型：On/Off Light（Zigbee Cluster Library Device ID）。 */
#define ZB_ON_OFF_LIGHT_DEVICE_ID 0x0100

/** 设备版本号，填入 Simple Descriptor 的 DeviceVersion 字段。 */
#define ZB_DEVICE_VER_ON_OFF_LIGHT 0

/** Server 侧入簇个数：Basic + Identify + OnOff。 */
#define ZB_ON_OFF_LIGHT_IN_CLUSTER_NUM 3
/** Client 侧出簇个数：本灯设备不主动当 Client，故为 0。 */
#define ZB_ON_OFF_LIGHT_OUT_CLUSTER_NUM 0

/** 入簇 + 出簇总数（Simple Descriptor 用）。 */
#define ZB_ON_OFF_LIGHT_CLUSTER_NUM \
	(ZB_ON_OFF_LIGHT_IN_CLUSTER_NUM + ZB_ON_OFF_LIGHT_OUT_CLUSTER_NUM)

/**
 * OnOff 簇可上报属性个数（当前即 OnOff attr 0x0000）。
 * Endpoint 声明里用来分配 reporting 上下文表大小。
 */
#define ZB_ON_OFF_LIGHT_REPORT_ATTR_COUNT ZB_ZCL_ON_OFF_REPORT_ATTR_COUNT

/**
 * @brief 声明 On/Off Light 的簇描述符数组（全部 Server 角色）。
 *
 * @param cluster_list_name 生成的数组名，如 app_light_clusters
 * @param basic_attr_list   Basic 属性表（ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST*）
 * @param identify_attr_list Identify 属性表
 * @param on_off_attr_list  OnOff 属性表（控灯主路径）
 *
 * 展开后得到 zb_zcl_cluster_desc_t[]，顺序须与下方 Simple Desc 中的簇 ID 列表一致。
 */
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

/**
 * @brief 声明 HA On/Off Light 的 Simple Descriptor。
 *
 * 告诉协调器/网关：本 EP 是什么 Profile、什么设备类型、有哪些簇。
 * 生成符号：simple_desc_##ep_name（由 ZB_DECLARE_ON_OFF_LIGHT_EP 间接调用）。
 *
 * @param ep_name       端点逻辑名后缀（如 app_light_ep）
 * @param ep_id         端点号（本工程为 ZIGBEE_APP_ENDPOINT=1）
 * @param in_clust_num  入簇数
 * @param out_clust_num 出簇数
 */
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

/**
 * @brief 声明完整 On/Off Light Endpoint（Simple Desc + reporting + AF endpoint）。
 *
 * @param ep_name      端点符号名，如 app_light_ep
 * @param ep_id        端点号
 * @param cluster_list 由 ZB_DECLARE_ON_OFF_LIGHT_CLUSTER_LIST 生成的簇数组
 *
 * 用法示例（zigbee_app.c）：
 *   ZB_DECLARE_ON_OFF_LIGHT_CLUSTER_LIST(app_light_clusters, ...);
 *   ZB_DECLARE_ON_OFF_LIGHT_EP(app_light_ep, ZIGBEE_APP_ENDPOINT, app_light_clusters);
 *   ZBOSS_DECLARE_DEVICE_CTX_1_EP(app_light_ctx, app_light_ep);
 */
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
