"""运营向导人话文案（主界面不出现协议词）。"""

from __future__ import annotations

APP_TITLE = "ink_panel 桌牌助手"

STEP_CONNECT = "① 连接"
STEP_PAGE = "② 选页"
STEP_EDIT = "③ 编辑"
STEP_PUSH = "④ 推送"

BTN_CONNECT = "连接桌牌"
BTN_DISCONNECT = "断开"
BTN_PUSH_SHOW = "推送并显示"
BTN_PUSH_SAVE = "仅保存到设备"
BTN_ENABLE_NOTES = "添加记事第 1 页"
BTN_ENABLE_INFO = "添加信息第 1 页"
BTN_ADD_PAGE = "＋ 添加一页"
BTN_DEL_PAGE = "－ 删除最后一页"
BTN_IMPORT = "导入图片…"
BTN_ADVANCED = "高级 / 详细日志"

GROUP_HOME = "首页"
GROUP_NOTES = "记事"
GROUP_INFO = "信息"

STATUS = {
    "idle": "未连接 · 点击右上角「连接桌牌」开始",
    "scanning": "正在查找附近的桌牌…",
    "no_device": "没找到桌牌。请确认已开机并靠近电脑后重试",
    "connecting": "正在连接…",
    "reading": "已连接 · 正在读取桌牌信息…",
    "ready_pick_page": "已连接 · 请选择要更新的页",
    "need_content": "已选页 · 请填写内容或导入图片",
    "ready_push": "就绪 · 可以推送到「{group} · 第{n}页」",
    "pushing": "正在推送到桌牌，请稍候…",
    "ok_show": "已更新并显示：{group} · 第{n}页",
    "ok_save": "已保存到设备（当前屏幕未切换）",
    "busy": "桌牌正忙，请等屏幕刷新完成后再试",
    "disconnected": "已断开。请稍等约 1 秒后再连接",
    "hint_disabled_connect": "请先连接桌牌",
    "hint_disabled_page": "请在左侧选择要更新的页",
    "hint_disabled_content": "请填写内容或导入图片",
}


def format_status(key: str, **kwargs: object) -> str:
    text = STATUS.get(key, key)
    try:
        return text.format(**kwargs)
    except (KeyError, ValueError):
        return text


def page_label(group: str, page: int) -> str:
    if group == GROUP_HOME:
        return GROUP_HOME
    return f"{group} · 第{page}页"
