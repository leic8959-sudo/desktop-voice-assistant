"""仅通过 MA 已认证 API 交付正式队列 URL；不新增公开音源解析端点。"""

import asyncio
import secrets

from music_assistant_models.auth import Scope
from music_assistant_models.enums import QueueOption, RepeatMode
from music_assistant_models.errors import InsufficientPermissions
from music_assistant.controllers.webserver.helpers.auth_middleware import get_current_user
from music_assistant.models.player_provider import PlayerProvider

from .player import PLAYER_ID, XiaoyanziPlayer


class XiaoyanziProvider(PlayerProvider):
    async def get_config_entries(self):
        return ()

    async def handle_async_init(self):
        self.ready = asyncio.Event()
        self.lock = asyncio.Lock()
        self.lease = ""
        self.task = None
        self.timer = None
        self.player = None
        self.unregister = [
            self.mass.register_api_command("xiaoyanzi/resolve", self.resolve, required_scope=Scope.QUEUES_CONTROL),
            self.mass.register_api_command("xiaoyanzi/report", self.report, required_scope=Scope.PLAYERS_CONTROL),
            self.mass.register_api_command("xiaoyanzi/stop", self.release, required_scope=Scope.PLAYERS_CONTROL),
        ]

    async def discover_players(self):
        if self.player is None:
            self.player = XiaoyanziPlayer(self)
            await self.mass.players.register(self.player)

    @staticmethod
    def _permission():
        user = get_current_user()
        if user is None or (user.player_filter and PLAYER_ID not in user.player_filter):
            raise InsufficientPermissions("No access to Xiaoyanzi gateway player")

    async def resolve(self, uri: str, position_s: int = 0) -> dict:
        self._permission()
        if not isinstance(uri, str) or len(uri) > 1024 or "://track/" not in uri:
            raise ValueError("Only provider track URIs are supported")
        if not 0 <= position_s <= 1800:
            raise ValueError("Position is outside the 30 minute limit")
        async with self.lock:
            await self._release()
            if self.player is None:
                raise RuntimeError("Xiaoyanzi player is not registered")
            self.lease = secrets.token_urlsafe(24)
            self.ready.clear()
            self.task = asyncio.create_task(self._prepare(uri, position_s))
            waiter = asyncio.create_task(self.ready.wait())
            try:
                done, _ = await asyncio.wait((self.task, waiter), timeout=30, return_when=asyncio.FIRST_COMPLETED)
                if self.task in done:
                    await self.task
                if not self.ready.is_set() or not self.player.stream:
                    raise RuntimeError("Music provider did not prepare a playable stream")
                self.timer = asyncio.get_running_loop().call_later(15, self._expire, self.lease)
                return {**self.player.stream, "lease": self.lease, "source_position_s": position_s}
            except BaseException:
                await self._release()
                raise
            finally:
                waiter.cancel()

    async def _prepare(self, uri, position_s):
        try:
            # ADD 只装载不播放；清空后只保留这一首，再由官方 seek 准备恢复点。
            self.mass.player_queues.clear(PLAYER_ID, skip_stop=True)
            self.mass.player_queues.set_autoplay(PLAYER_ID, False)
            await self.mass.player_queues.set_repeat(PLAYER_ID, RepeatMode.OFF)
            self.mass.player_queues.set_crossfade(PLAYER_ID, False)
            await self.mass.player_queues.play_media(PLAYER_ID, uri, option=QueueOption.ADD)
            await self.mass.player_queues.play_index(PLAYER_ID, 0, seek_position=position_s)
        except asyncio.CancelledError:
            raise
        except Exception:
            self.logger.exception("xiaoyanzi_queue_prepare_failed")
            raise

    def _expire(self, lease):
        self.mass.create_task(self._expire_lease(lease))

    async def _expire_lease(self, lease):
        async with self.lock:
            if lease == self.lease:
                await self._release()

    async def report(self, lease: str, state: str, position_s: int = 0) -> dict:
        self._permission()
        if state not in {"started", "playing", "paused", "stopped", "finished", "error"} or not 0 <= position_s <= 1800:
            raise ValueError("Invalid playback report")
        async with self.lock:
            if not self.lease or not secrets.compare_digest(lease, self.lease):
                raise ValueError("Expired playback lease")
            if self.timer is not None:
                self.timer.cancel()
                self.timer = None
            self.player.report(state, position_s)
            if state in {"stopped", "finished", "error"}:
                await self._release()
            else:
                # 网关退出或板端断线而丢失最终回执时，不永久占用音源会话。
                self.timer = asyncio.get_running_loop().call_later(90, self._expire, self.lease)
            return {"accepted": True}

    async def release(self, lease: str = "") -> dict:
        self._permission()
        async with self.lock:
            if lease and self.lease and not secrets.compare_digest(lease, self.lease):
                return {"accepted": False}
            await self._release()
            return {"accepted": True}

    async def _release(self):
        if self.timer is not None:
            self.timer.cancel()
            self.timer = None
        task, self.task = self.task, None
        if task is not None:
            if not task.done():
                task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
            except Exception:
                # _prepare 已保留原始异常堆栈；清理仍必须继续。
                self.logger.exception("xiaoyanzi_queue_release_after_failure")
        self.lease = ""
        if self.player is not None:
            await self.player.stop()

    async def unload(self, is_removed=False):
        async with self.lock:
            await self._release()
        for unregister in self.unregister:
            unregister()
        if self.player is not None:
            await self.mass.players.unregister(PLAYER_ID)
