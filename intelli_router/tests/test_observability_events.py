"""Tests for intelli_router.observability.events."""
import time
import pytest
from intelli_router.observability.events import RoutingEvent, RoutingEventType


class TestRoutingEventType:
    def test_all_values(self):
        assert RoutingEventType.REQUEST_STARTED == "request_started"
        assert RoutingEventType.REQUEST_SUCCEEDED == "request_succeeded"
        assert RoutingEventType.REQUEST_RETRIED == "request_retried"
        assert RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED == "all_deployments_exhausted"
        assert RoutingEventType.STREAM_STARTED == "stream_started"
        assert RoutingEventType.STREAM_SUCCEEDED == "stream_succeeded"

    def test_is_str_enum(self):
        assert isinstance(RoutingEventType.REQUEST_STARTED, str)

    def test_total_count(self):
        assert len(RoutingEventType) == 6


class TestRoutingEvent:
    def test_minimal_event(self):
        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id="abc123def456",
            model="gpt-4",
        )
        assert event.event_type == RoutingEventType.REQUEST_STARTED
        assert event.request_id == "abc123def456"
        assert event.model == "gpt-4"
        assert event.deployment_id is None
        assert event.latency is None
        assert event.attempt == 0
        assert event.extra == {}

    def test_timestamp_auto_generated(self):
        before = time.time()
        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id="test",
            model="gpt-4",
        )
        after = time.time()
        assert before <= event.timestamp <= after

    def test_full_event(self):
        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_SUCCEEDED,
            request_id="req123",
            model="gpt-4",
            deployment_id="dep-1",
            provider="openai",
            latency=1.23,
            prompt_tokens=100,
            completion_tokens=50,
            total_tokens=150,
            attempt=1,
            total_attempts=3,
        )
        assert event.deployment_id == "dep-1"
        assert event.latency == 1.23
        assert event.total_tokens == 150

    def test_new_request_id_format(self):
        rid = RoutingEvent.new_request_id()
        assert len(rid) == 12
        assert all(c in "0123456789abcdef" for c in rid)

    def test_new_request_id_unique(self):
        ids = {RoutingEvent.new_request_id() for _ in range(100)}
        assert len(ids) == 100

    def test_to_dict_minimal(self):
        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id="abc123",
            model="gpt-4",
            timestamp=1000.0,
        )
        d = event.to_dict()
        assert d == {
            "event_type": "request_started",
            "request_id": "abc123",
            "model": "gpt-4",
            "timestamp": 1000.0,
        }

    def test_to_dict_omits_none(self):
        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id="abc",
            model="gpt-4",
            timestamp=1000.0,
        )
        d = event.to_dict()
        assert "deployment_id" not in d
        assert "latency" not in d
        assert "error_type" not in d

    def test_to_dict_includes_set_fields(self):
        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_SUCCEEDED,
            request_id="abc",
            model="gpt-4",
            timestamp=1000.0,
            deployment_id="dep-1",
            latency=0.5,
            prompt_tokens=10,
            completion_tokens=20,
            total_tokens=30,
            attempt=1,
            total_attempts=3,
        )
        d = event.to_dict()
        assert d["deployment_id"] == "dep-1"
        assert d["latency"] == 0.5
        assert d["prompt_tokens"] == 10
        assert d["attempt"] == 1
        assert d["total_attempts"] == 3

    def test_to_dict_includes_extra(self):
        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id="abc",
            model="gpt-4",
            timestamp=1000.0,
            extra={"custom_key": "value"},
        )
        d = event.to_dict()
        assert d["extra"] == {"custom_key": "value"}

    def test_to_dict_excludes_empty_extra(self):
        event = RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id="abc",
            model="gpt-4",
            timestamp=1000.0,
        )
        d = event.to_dict()
        assert "extra" not in d
