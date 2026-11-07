"""Third-party connector SDK and registry."""

from .contracts import Connector, ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope
from .registry import ConnectorRegistry
from .builtin import GmailConnector, HomeAssistantConnector
from .google_calendar import GoogleCalendarConnector
from .feishu import FeishuConnector
from .amap import AmapConnector
from .microsoft_calendar import MicrosoftCalendarConnector
from .notion import NotionConnector
from .github import GitHubConnector
from .slack import SlackConnector
from .todoist import TodoistConnector
from .microsoft_mail import MicrosoftMailConnector

__all__ = [
    "Connector",
    "ConnectorAction",
    "ConnectorContext",
    "ConnectorManifest",
    "ConnectorRegistry",
    "PermissionScope",
    "GmailConnector",
    "HomeAssistantConnector",
    "GoogleCalendarConnector",
    "FeishuConnector",
    "AmapConnector",
    "MicrosoftCalendarConnector",
    "NotionConnector",
    "GitHubConnector",
    "SlackConnector",
    "TodoistConnector",
    "MicrosoftMailConnector",
]
