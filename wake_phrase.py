"""设备声明仅选择已适配的完整唤醒词，不提供跳过校验的开关。"""

# [人工决策-2026-11-13 12:13:07] 牛牛确认助手更名伊迪斯，实体唤醒词同步改为“你好，伊迪斯”。
DEFAULT_WAKE_PHRASE = "你好伊迪斯"
WAKE_PHRASE_LABELS = {DEFAULT_WAKE_PHRASE: "你好，伊迪斯", "你好小智": "你好小智", "你好小燕子": "你好，小燕子"}


def device_wake_phrase(hello):
    phrase = hello.get("wake_phrase", DEFAULT_WAKE_PHRASE)
    if phrase == "你好，伊迪斯":
        phrase = DEFAULT_WAKE_PHRASE
    elif phrase == "你好，小燕子":
        phrase = "你好小燕子"
    if not isinstance(phrase, str) or phrase not in WAKE_PHRASE_LABELS:
        raise ValueError("设备唤醒词不在已适配清单中")
    return phrase


def wake_phrase_display(phrase):
    return WAKE_PHRASE_LABELS[phrase]


def wake_phrase_variants(phrase):
    # 云端真实复现将“伊迪斯”转写为“伊迪丝”；只兼容完整同音词，不做前缀模糊匹配。
    return (phrase, "你好伊迪丝") if phrase == "你好伊迪斯" else (phrase,)
