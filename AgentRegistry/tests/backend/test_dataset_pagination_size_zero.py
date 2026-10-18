# coding: utf-8
"""GET /api/datasets/{dataset}/services?size=0 must be a client error."""

from types import SimpleNamespace

import pytest
from fastapi import HTTPException

from a2x_registry.backend.routers import dataset as dataset_router


class _Entry:
    service_id = "svc-1"
    type = "generic"
    source = "local"

    def __init__(self):
        self.service_data = SimpleNamespace(model_dump=lambda: {"id": "svc-1"})


class _Service:
    def list_services(self, dataset):
        return [{"id": "svc-1", "name": "svc", "description": "demo"}]

    def list_entries(self, dataset):
        return [_Entry()]

    def is_leased(self, dataset, service_id):
        return False

    def is_unhealthy(self, dataset, service_id):
        return False


@pytest.mark.asyncio
async def test_size_zero_is_rejected_instead_of_dividing_by_zero(monkeypatch):
    monkeypatch.setattr(dataset_router, "get_registry_service", lambda: _Service())
    monkeypatch.setattr(dataset_router, "get_cluster_store", lambda: None)
    request = SimpleNamespace(query_params={})
    response = SimpleNamespace(headers={})

    with pytest.raises(HTTPException) as exc_info:
        await dataset_router.list_services(
            "demo",
            request,
            response,
            fields="detail",
            size=0,
            page=1,
            include_leased=False,
            include_unhealthy=False,
        )

    assert exc_info.value.status_code == 400
    assert "size" in str(exc_info.value.detail)
