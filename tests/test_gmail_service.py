import asyncio
import base64
import json
import tempfile
import unittest
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from gmail_service import GmailError, GmailService, MemoryTokenVault
from gmail_tools import TOOL_SCHEMAS, execute_tool


class FakeResponse:
    def __init__(self, payload, status=200):
        self.payload = payload
        self.status = status

    async def json(self, content_type=None):
        return self.payload


class FakeClient:
    def __init__(self):
        self.calls = []
        self.raw = base64.urlsafe_b64encode(b"Subject: Hi\n\nhello").rstrip(b"=").decode()
        self.send_missing_id = False
        self.send_timeout = False

    async def request(self, method, url, **kwargs):
        self.calls.append((method, url, kwargs))
        if url == "https://oauth2.googleapis.com/token":
            return FakeResponse({"access_token": "access", "refresh_token": "refresh", "expires_in": 3600})
        if url.endswith("/profile"):
            return FakeResponse({"emailAddress": "owner@example.com"})
        if url.endswith("/messages"):
            return FakeResponse({"messages": [{"id": "m1"}], "nextPageToken": "next"})
        if "/messages/m1" in url:
            data = base64.urlsafe_b64encode("Hello <b>world</b>".encode()).rstrip(b"=").decode()
            return FakeResponse({"id": "m1", "threadId": "t1", "payload": {"headers": [{"name": "Subject", "value": "Test"}, {"name": "From", "value": "a@example.com"}], "mimeType": "text/html", "body": {"data": data}}})
        if url.endswith("/drafts") and method == "POST":
            return FakeResponse({"id": "d1", "message": {"id": "x"}})
        if "/drafts/d1" in url and method == "GET":
            return FakeResponse({"id": "d1", "message": {"id": "m", "raw": self.raw, "payload": {"headers": [{"name": "Subject", "value": "Hi"}, {"name": "To", "value": "a@example.com"}], "mimeType": "text/plain", "body": {"data": base64.urlsafe_b64encode(b"hello").rstrip(b"=").decode()}}}})
        if url.endswith("/drafts/send"):
            if self.send_timeout:
                raise asyncio.TimeoutError()
            if self.send_missing_id:
                return FakeResponse({})
            return FakeResponse({"id": "sent-1"})
        return FakeResponse({})


class GmailServiceTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.client = FakeClient()
        self.temp = tempfile.TemporaryDirectory()
        self.config = {"gmail_client_id": "client", "gmail_client_secret": "secret", "gmail_redirect_uri": "http://127.0.0.1:8765/api/gmail/callback", "gmail_send_enabled": False}
        self.service = GmailService(self.config, self.client, self.temp.name, MemoryTokenVault())

    async def asyncTearDown(self):
        await self.service.close()
        self.temp.cleanup()

    async def test_oauth_pkce_and_profile_without_logging_code(self):
        started = self.service.start_authorization()
        query = parse_qs(urlparse(started["authorization_url"]).query)
        self.assertEqual(query["code_challenge_method"], ["S256"])
        self.assertNotIn("secret", started["authorization_url"])
        result = await self.service.complete_authorization(started["state"], "auth-code")
        self.assertTrue(result["authorized"])
        self.assertEqual(self.service.status()["email"], "owner@example.com")
        with self.assertRaises(GmailError):
            await self.service.complete_authorization(started["state"], "auth-code")

    async def test_proxy_used_for_token_exchange_and_authenticated_mail_request(self):
        self.config["gmail_proxy_url"] = "http://127.0.0.1:7890"
        started = self.service.start_authorization()
        await self.service.complete_authorization(started["state"], "mock-authorization-code")
        await self.service.search("is:unread", limit=1)
        self.assertTrue(any(url == "https://oauth2.googleapis.com/token" for _, url, _ in self.client.calls))
        self.assertTrue(any(url.endswith("/messages") for _, url, _ in self.client.calls))
        self.assertTrue(all(kwargs["proxy"] == self.config["gmail_proxy_url"] for _, _, kwargs in self.client.calls))
        self.assertTrue(all(kwargs["allow_redirects"] is False for _, _, kwargs in self.client.calls))

    async def test_invalid_proxy_never_submits_credentials(self):
        self.config["gmail_proxy_url"] = "http://name:secret@127.0.0.1:7890"
        started = self.service.start_authorization()
        with self.assertRaisesRegex(GmailError, "代理配置无效"):
            await self.service.complete_authorization(started["state"], "mock-authorization-code")
        self.assertEqual(self.client.calls, [])

    async def test_search_read_and_tool_surface(self):
        self.service._token = {"access_token": "access", "expires_at": 9999999999}
        result = await self.service.search("from:a@example.com", limit=999)
        self.assertEqual(result["items"][0]["id"], "m1")
        params = next(call[2]["params"] for call in self.client.calls if call[0] == "GET" and call[1].endswith("/messages"))
        self.assertEqual(params["maxResults"], 20)
        message = await self.service.read_message("m1")
        self.assertTrue(message["body_untrusted"])
        self.assertIn("Hello", message["body"])
        self.assertEqual(len(TOOL_SCHEMAS), 6)
        tool_result = await execute_tool(self.service, "gmail_search", {"query": "x", "limit": 1})
        self.assertEqual(tool_result["items"][0]["id"], "m1")

    async def test_mime_header_injection_rejected_and_send_confirmation(self):
        self.service._token = {"access_token": "access", "expires_at": 9999999999}
        with self.assertRaises(ValueError):
            await self.service.create_draft("a@example.com", "bad\nBcc: x", "body")
        created = await self.service.create_draft("a@example.com, b@example.com", "Hi", "body")
        self.assertEqual(created["id"], "d1")
        self.config["gmail_send_enabled"] = True
        prepared = await self.service.prepare_send("d1")
        self.assertEqual(prepared["draft"]["id"], "d1")
        sent = await self.service.confirm_send("d1", prepared["confirmation_id"], prepared["raw_sha256"])
        self.assertEqual(sent["status"], "sent")
        send_call = next(call for call in self.client.calls if call[0] == "POST" and call[1].endswith("/drafts/send"))
        self.assertEqual(send_call[2]["json"]["id"], "d1")
        with self.assertRaises(GmailError):
            await self.service.confirm_send("d1", prepared["confirmation_id"], prepared["raw_sha256"])

    async def test_prepare_rejects_attachment_and_corrupt_ledger_fails_closed(self):
        self.service._token = {"access_token": "access", "expires_at": 9999999999}
        self.config["gmail_send_enabled"] = True
        original = self.service.get_draft

        async def attachment_draft(draft_id, include_raw=False):
            return {"draft_id": draft_id, "raw": "raw", "attachments": [{"filename": "a.pdf"}], "body_is_plain": True, "body_truncated": False}

        self.service.get_draft = attachment_draft
        with self.assertRaises(GmailError):
            await self.service.prepare_send("d1")
        self.service.get_draft = original
        Path(self.service._attempt_path).write_text("[]", encoding="utf-8")
        self.service._confirmations["d1"] = type("Confirmation", (), {"expires_at": 9999999999, "nonce": "n"})()
        with self.assertRaises(GmailError):
            self.service._load_attempts()

    async def test_timeout_unknown_and_missing_id_never_sent(self):
        self.service._token = {"access_token": "access", "expires_at": 9999999999}
        self.config["gmail_send_enabled"] = True
        prepared = await self.service.prepare_send("d1")
        self.client.send_timeout = True
        with self.assertRaises(GmailError):
            await self.service.confirm_send("d1", prepared["confirmation_id"], prepared["raw_sha256"])
        self.assertEqual(json.loads(Path(self.service._attempt_path).read_text())["d1"]["status"], "UNKNOWN")
        self.client.send_timeout = False
        prepared = await self.service.prepare_send("d1")
        self.client.send_missing_id = True
        result = await self.service.confirm_send("d1", prepared["confirmation_id"], prepared["raw_sha256"])
        self.assertEqual(result["status"], "unknown")
        self.assertNotEqual(json.loads(Path(self.service._attempt_path).read_text())["d1"]["status"], "SENT")

    async def test_changed_raw_invalidates_confirmation_without_post(self):
        self.service._token = {"access_token": "access", "expires_at": 9999999999}
        self.config["gmail_send_enabled"] = True
        prepared = await self.service.prepare_send("d1")
        original = self.service.get_draft
        async def changed(draft_id, include_raw=False):
            value = await original(draft_id, include_raw)
            value["raw"] = "changed"
            return value
        self.service.get_draft = changed
        before = len(self.client.calls)
        with self.assertRaises(GmailError):
            await self.service.confirm_send("d1", prepared["confirmation_id"], prepared["raw_sha256"])
        self.assertFalse(any(url.endswith("/drafts/send") for _, url, _ in self.client.calls[before:]))


if __name__ == "__main__":
    unittest.main()
