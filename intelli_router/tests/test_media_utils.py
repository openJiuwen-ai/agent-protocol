"""Tests for intelli_router.utils.media."""
import pytest

from intelli_router.utils.media import is_data_uri, parse_data_uri


class TestIsDataUri:

    def test_valid_image_png(self):
        assert is_data_uri("data:image/png;base64,iVBOR") is True

    def test_valid_application_pdf(self):
        assert is_data_uri("data:application/pdf;base64,JVBERi0") is True

    def test_https_url(self):
        assert is_data_uri("https://example.com/img.png") is False

    def test_empty_string(self):
        assert is_data_uri("") is False

    def test_none(self):
        assert is_data_uri(None) is False

    def test_partial_prefix(self):
        assert is_data_uri("dat:image/png") is False


class TestParseDataUri:

    def test_image_png(self):
        uri = "data:image/png;base64,iVBORw0KGgo="
        mime, data = parse_data_uri(uri)
        assert mime == "image/png"
        assert data == "iVBORw0KGgo="

    def test_image_jpeg(self):
        uri = "data:image/jpeg;base64,/9j/4AAQ"
        mime, data = parse_data_uri(uri)
        assert mime == "image/jpeg"
        assert data == "/9j/4AAQ"

    def test_application_pdf(self):
        uri = "data:application/pdf;base64,JVBERi0xLjQ="
        mime, data = parse_data_uri(uri)
        assert mime == "application/pdf"
        assert data == "JVBERi0xLjQ="

    def test_data_with_commas(self):
        # base64 data itself may not contain commas, but ensure only first comma splits
        uri = "data:text/plain;base64,aGVsbG8sIHdvcmxk"
        mime, data = parse_data_uri(uri)
        assert mime == "text/plain"
        assert data == "aGVsbG8sIHdvcmxk"

    def test_missing_mime_defaults_to_octet_stream(self):
        uri = "data:;base64,abc123"
        mime, data = parse_data_uri(uri)
        assert mime == "application/octet-stream"
        assert data == "abc123"

    def test_not_data_uri_raises(self):
        with pytest.raises(ValueError):
            parse_data_uri("https://example.com/img.png")

    def test_no_comma_raises(self):
        with pytest.raises(ValueError):
            parse_data_uri("data:image/png;base64")
