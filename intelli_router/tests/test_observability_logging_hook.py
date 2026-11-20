"""Tests for intelli_router.observability.logging_hook."""
import json
import logging
import pytest
from unittest.mock import MagicMock
from intelli_router.observability.logging_hook import LoggingHook
from intelli_router.observability.events import RoutingEvent, RoutingEventType


def _make_success_event() -> RoutingEvent:
    return RoutingEvent(
        event_type=RoutingEventType.REQUEST_SUCCEEDED,
        request_id="abc123def456",
        model="gpt-4",
        timestamp=1700000000.0,
        deployment_id="dep-1",
        provider="openai",
        latency=1.234,
        prompt_tokens=100,
        completion_tokens=50,
        total_tokens=150,
        attempt=1,
        total_attempts=3,
    )


def _make_error_event() -> RoutingEvent:
    return RoutingEvent(
        event_type=RoutingEventType.REQUEST_RETRIED,
        request_id="abc123def456",
        model="gpt-4",
        timestamp=1700000000.0,
        deployment_id="dep-1",
        provider="openai",
        attempt=1,
        total_attempts=3,
        error_type="TimeoutError",
        error_message="Request timed out",
    )


@pytest.mark.asyncio
class TestLoggingHookJson:
    async def test_json_format_success(self):
        mock_logger = MagicMock()
        hook = LoggingHook(logger_name="test", level=logging.INFO, format="json")
        hook._logger = mock_logger

        await hook.handle_event(_make_success_event())

        mock_logger.log.assert_called_once()
        call_args = mock_logger.log.call_args
        assert call_args[0][0] == logging.INFO
        data = json.loads(call_args[0][1])
        assert data["event_type"] == "request_succeeded"
        assert data["request_id"] == "abc123def456"
        assert data["model"] == "gpt-4"
        assert data["deployment_id"] == "dep-1"
        assert data["latency"] == 1.234
        assert data["total_tokens"] == 150
        assert data["attempt"] == 1

    async def test_json_format_error(self):
        mock_logger = MagicMock()
        hook = LoggingHook(format="json")
        hook._logger = mock_logger

        await hook.handle_event(_make_error_event())

        data = json.loads(mock_logger.log.call_args[0][1])
        assert data["error_message"] == "Request timed out"

    async def test_json_omits_none_fields(self):
        mock_logger = MagicMock()
        hook = LoggingHook(format="json")
        hook._logger = mock_logger

        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id="abc",
            model="gpt-4",
            timestamp=1000.0,
        )
        await hook.handle_event(event)

        data = json.loads(mock_logger.log.call_args[0][1])
        assert "deployment_id" not in data
        assert "latency" not in data
        assert "total_tokens" not in data
        assert "error_message" not in data


@pytest.mark.asyncio
class TestLoggingHookText:
    async def test_text_format_success(self):
        mock_logger = MagicMock()
        hook = LoggingHook(format="text")
        hook._logger = mock_logger

        await hook.handle_event(_make_success_event())

        message = mock_logger.log.call_args[0][1]
        assert "[request_succeeded]" in message
        assert "req=abc123de" in message
        assert "model=gpt-4" in message
        assert "dep=dep-1" in message
        assert "lat=1.234s" in message
        assert "tokens=150" in message

    async def test_text_format_error(self):
        mock_logger = MagicMock()
        hook = LoggingHook(format="text")
        hook._logger = mock_logger

        await hook.handle_event(_make_error_event())

        message = mock_logger.log.call_args[0][1]
        assert "[request_retried]" in message
        assert "err=Request timed out" in message

    async def test_text_format_minimal(self):
        mock_logger = MagicMock()
        hook = LoggingHook(format="text")
        hook._logger = mock_logger

        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id="abcdef123456",
            model="gpt-4",
            timestamp=1000.0,
        )
        await hook.handle_event(event)

        message = mock_logger.log.call_args[0][1]
        assert "[request_started]" in message
        assert "model=gpt-4" in message
        # 不应包含未设置的字段
        assert "dep=" not in message
        assert "lat=" not in message


@pytest.mark.asyncio
class TestLoggingHookConfig:
    async def test_custom_log_level(self):
        mock_logger = MagicMock()
        hook = LoggingHook(level=logging.DEBUG, format="json")
        hook._logger = mock_logger

        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id="abc",
            model="gpt-4",
        )
        await hook.handle_event(event)

        assert mock_logger.log.call_args[0][0] == logging.DEBUG

    async def test_all_event_types_no_crash(self):
        mock_logger = MagicMock()
        hook = LoggingHook(format="json")
        hook._logger = mock_logger

        for event_type in RoutingEventType:
            event = RoutingEvent(
                event_type=event_type,
                request_id="abc",
                model="gpt-4",
            )
            await hook.handle_event(event)

        assert mock_logger.log.call_count == len(RoutingEventType)

    async def test_stream_event_with_chunks(self):
        mock_logger = MagicMock()
        hook = LoggingHook(format="json")
        hook._logger = mock_logger

        event = RoutingEvent(
            event_type=RoutingEventType.STREAM_SUCCEEDED,
            request_id="abc",
            model="gpt-4",
            timestamp=1000.0,
            chunk_count=42,
            latency=2.5,
        )
        await hook.handle_event(event)

        data = json.loads(mock_logger.log.call_args[0][1])
        assert data["chunk_count"] == 42
