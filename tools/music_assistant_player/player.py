"""MA 提供流，网关收到板端回执后再更新播放状态。"""

import time

from music_assistant_models.enums import PlaybackState, PlayerFeature
from music_assistant.models.player import Player

PLAYER_ID = "xiaoyanzi_gateway"


class XiaoyanziPlayer(Player):
    def __init__(self, provider):
        super().__init__(provider, PLAYER_ID)
        self._attr_name = "小燕子实体喇叭（网关控制）"
        self._attr_supported_features = {PlayerFeature.PLAY_MEDIA, PlayerFeature.PAUSE}
        self._attr_supported_sample_rates = [(24000, 16)]
        self._attr_can_group_with = set()
        self.stream = None
        self.ready = provider.ready

    @property
    def needs_poll(self):
        return False

    @property
    def requires_flow_mode(self):
        # MA 默认把未声明 ENQUEUE 的播放器视为连续流；本网关每次只解析一首。
        return False

    async def play_media(self, media):
        item = self.provider.mass.player_queues.get_item(media.source_id, media.queue_item_id)
        details = item.streamdetails if item is not None else None
        data = details.data if details is not None else None
        preview = data.get("preview") if isinstance(data, dict) else None
        url = await self.provider.mass.streams.resolve_stream_url(self.player_id, media)
        self._attr_current_media = media
        self._attr_active_source = media.source_id
        # 试听最多交付30秒，不能沿用曲库标注的整曲时长。
        duration = min(details.duration or 30, 30) if preview is True else details.duration if details is not None else None
        self.stream = {"url": url, "queue_item_id": media.queue_item_id, "playback_access": "preview" if preview is True else "full" if preview is False else "unknown", "duration_s": duration}
        self.update_state()
        self.ready.set()

    async def play(self):
        raise RuntimeError("请在小燕子点歌页面点击继续，由实体设备回执确认")

    async def pause(self):
        raise RuntimeError("请在小燕子点歌页面点击暂停，由网关关闭音频流")

    async def stop(self):
        self.stream = None
        self._attr_current_media = None
        self._attr_active_source = None
        self._attr_playback_state = PlaybackState.IDLE
        self.update_state()

    def report(self, state, position_s):
        self._attr_playback_state = {
            "started": PlaybackState.PLAYING,
            "playing": PlaybackState.PLAYING,
            "paused": PlaybackState.PAUSED,
            "stopped": PlaybackState.IDLE,
            "finished": PlaybackState.IDLE,
            "error": PlaybackState.IDLE,
        }[state]
        self._attr_elapsed_time = position_s
        self._attr_elapsed_time_last_updated = time.time()
        self.update_state()
