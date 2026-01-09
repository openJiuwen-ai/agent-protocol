/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "server/http_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
extern "C" {
#include <http_parser.h>
}
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "mcp_log.h"
#include "shared/http_common.h"
#include "shared/thread_utils.h"

#define HTTP_TASK_QUEUE_CAPACITY 1024
#define HTTP_TASK_QUEUE_BATCH_SIZE 64

namespace Mcp::Http {

constexpr int HTTPS_READ_BUFFER_SIZE = 4096;
constexpr int HTTP_LISTEN_BACKLOG = 128;

namespace {

struct ParserContext {
    HttpRequest* request;
    std::string currentField;
    std::string currentValue;
    bool lastWasValue{false};
    bool messageCompleted{false};
};

int HandleUrlCallback(http_parser* parser, const char* at, size_t length)
{
    auto* ctx = static_cast<ParserContext*>(parser->data);
    ctx->request->url.append(at, length);
    return 0;
}

int HandleHeaderFieldCallback(http_parser* parser, const char* at, size_t length)
{
    auto* ctx = static_cast<ParserContext*>(parser->data);
    if (ctx->lastWasValue) {
        TrimInPlace(ctx->currentField);
        TrimInPlace(ctx->currentValue);
        if (!ctx->currentField.empty()) {
            ctx->request->headers[ctx->currentField] = ctx->currentValue;
        }
        ctx->currentField.clear();
        ctx->currentValue.clear();
        ctx->lastWasValue = false;
    }
    ctx->currentField.append(at, length);
    return 0;
}

int HandleHeaderValueCallback(http_parser* parser, const char* at, size_t length)
{
    auto* ctx = static_cast<ParserContext*>(parser->data);
    ctx->currentValue.append(at, length);
    ctx->lastWasValue = true;
    return 0;
}

int HandleBodyCallback(http_parser* parser, const char* at, size_t length)
{
    auto* ctx = static_cast<ParserContext*>(parser->data);
    ctx->request->body.append(at, length);
    return 0;
}

int HandleHeadersCompleteCallback(http_parser* parser)
{
    auto* ctx = static_cast<ParserContext*>(parser->data);
    if (!ctx->currentField.empty()) {
        TrimInPlace(ctx->currentField);
        TrimInPlace(ctx->currentValue);
        if (!ctx->currentField.empty()) {
            ctx->request->headers[ctx->currentField] = ctx->currentValue;
        }
        ctx->currentField.clear();
        ctx->currentValue.clear();
    }
    ctx->request->method = http_method_str(static_cast<http_method>(parser->method));
    ctx->request->version = "HTTP/" + std::to_string(parser->http_major) + "." + std::to_string(parser->http_minor);
    return 0;
}

int HandleMessageCompleteCallback(http_parser* parser)
{
    auto* ctx = static_cast<ParserContext*>(parser->data);
    ctx->messageCompleted = true;
    return 0;
}

} // namespace

HttpServer::HttpServer(const std::string& host, uint16_t port, const TlsConfig& tlsConfig, RouteMap& routes,
    size_t ioThreadIndex)
    : eventSystem_(true, ioThreadIndex),
      listener_(std::make_unique<Mcp::Net::TcpListener>(eventSystem_)),
      routes_(routes),
      host_(host),
      port_(port),
      tlsConfig_(tlsConfig),
      ioThreadIndex_(ioThreadIndex)
{
}

void HttpServer::Run()
{
    const bool useTls = tlsConfig_.enabled;

    if (running_.exchange(true)) {
        return;
    }

    if (!eventSystem_.Init()) {
        running_ = false;
        throw std::runtime_error("EventSystem init failed");
    }

    // Initialize task queue
    if (taskQueue_ == nullptr) {
        taskQueue_ = std::make_unique<Mcp::MPSCNotifyQueue<std::function<void()>>>(HTTP_TASK_QUEUE_CAPACITY,
            HTTP_TASK_QUEUE_BATCH_SIZE);
    }
    if (taskQueue_ != nullptr && !taskQueue_->IsInitialized()) {
        bool inited = taskQueue_->Initialize(&eventSystem_, [](const std::function<void()>& cmd) {
            if (cmd) {
                cmd();
            }
        });
        if (!inited) {
            running_ = false;
            throw std::runtime_error("taskQueue initialize failed");
        }
    }

    if (useTls) {
        InitializeSslContext();
    }
    listener_->OnError([](const Mcp::Net::SocketPtr& socket, int errorCode, const std::string& message) {
        int fileDescriptor = socket ? socket->Fd() : -1;
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "listener error fd=" + std::to_string(fileDescriptor) +
                " err=" + std::to_string(errorCode) + " msg=" + message);
    });

    listener_->OnNewConnection(
        [this](const Mcp::Net::TcpSocketPtr& connection) { this->HandleNewConnection(connection); });

    if (!listener_->Listen(host_, port_, HTTP_LISTEN_BACKLOG, true)) {
        if (sslContext_ != nullptr) {
            SSL_CTX_free(reinterpret_cast<SSL_CTX*>(sslContext_));
            sslContext_ = nullptr;
        }
        running_ = false;
        throw std::runtime_error("listen failed");
    }

    if (!listener_->Start()) {
        if (sslContext_ != nullptr) {
            SSL_CTX_free(reinterpret_cast<SSL_CTX*>(sslContext_));
            sslContext_ = nullptr;
        }
        running_ = false;
        throw std::runtime_error("start listener failed");
    }

    if (useTls) {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "HTTPS listening on " + host_ + ":" + std::to_string(port_));
    } else {
        MCP_LOG(MCP_LOG_LEVEL_INFO, "listening on " + host_ + ":" + std::to_string(port_));
    }

    eventThread_ = std::thread([this]() {
        Mcp::SetCurrentThreadName("MCP-IO-" + std::to_string(ioThreadIndex_));
        eventSystem_.Start(false);
        running_ = false;
    });
}

void HttpServer::Stop()
{
    if (!running_.exchange(false)) {
        return;
    }
    if (taskQueue_ != nullptr) {
        taskQueue_->Cleanup();
    }
    eventSystem_.Stop();
    if (eventThread_.joinable()) {
        eventThread_.join();
    }
}

void HttpServer::SendResponseAsync(const HttpResponse& response, const RequestContext& ctx)
{
    if (!running_.load()) {
        return;
    }
    if (taskQueue_ == nullptr) {
        return;
    }
    HttpResponse responseCopy = response;
    ConnectionId connectionId = ctx.connectionId;
    taskQueue_->Send([this, connectionId, responseCopy]() { this->SendResponse(connectionId, responseCopy); });
}

void HttpServer::HandleNewConnection(const Mcp::Net::TcpSocketPtr& connection)
{
    if (!connection) {
        return;
    }
    int fileDescriptor = connection->Fd();
    ConnectionContext context;
    context.connection = connection;
    context.lastActivity = std::chrono::steady_clock::now();

    if (tlsConfig_.enabled) {
        SSL* ssl = SSL_new(sslContext_);
        if (ssl == nullptr) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "SSL_new failed for HTTPS connection");
            connection->Close();
            return;
        }

        BIO* rbio = BIO_new(BIO_s_mem());
        BIO* wbio = BIO_new(BIO_s_mem());
        if (rbio == nullptr || wbio == nullptr) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "BIO_new failed for HTTPS connection");
            if (rbio != nullptr) {
                BIO_free(rbio);
            }
            if (wbio != nullptr) {
                BIO_free(wbio);
            }
            SSL_free(ssl);
            connection->Close();
            return;
        }

        SSL_set_bio(ssl, rbio, wbio);
        SSL_set_accept_state(ssl);

        context.ssl = ssl;
        context.rbio = rbio;
        context.wbio = wbio;
        context.handshaked = false;
    }

    connections_[fileDescriptor] = std::move(context);

    connection->OnError([this](const Mcp::Net::SocketPtr& socket, int errorCode, const std::string& message) {
        this->HandleError(socket, errorCode, message);
    });

    connection->OnClose([this](const Mcp::Net::SocketPtr& socket) { this->HandleClose(socket); });

    connection->OnRead([this, connection](const Mcp::Net::SocketPtr& /* socket */) { this->HandleRead(connection); });
}

void HttpServer::HandleRead(const Mcp::Net::TcpSocketPtr& connection)
{
    if (!connection) {
        return;
    }
    int fileDescriptor = connection->Fd();

    auto iterator = connections_.find(fileDescriptor);
    if (iterator == connections_.end()) {
        return;
    }

    ConnectionContext& context = iterator->second;
    context.lastActivity = std::chrono::steady_clock::now();

    Mcp::Net::Buffer& buffer = connection->InputBuffer();
    if (buffer.ReadableBytes() == 0) {
        return;
    }

    if (context.ssl == nullptr) {
        std::string data = buffer.RetrieveAllAsString();
        OnRead(fileDescriptor, data);
        return;
    }

    std::string encryptedData = buffer.RetrieveAllAsString();
    if (encryptedData.empty()) {
        return;
    }

    int written = BIO_write(context.rbio, encryptedData.data(), static_cast<int>(encryptedData.size()));
    if (written <= 0) {
        MCP_LOG(MCP_LOG_LEVEL_ERROR, "BIO_write failed for HTTPS connection, fd=" + std::to_string(fileDescriptor));
        CleanupConnection(fileDescriptor);
        return;
    }

    auto flushTlsPendingData = [&context, fileDescriptor, this]() {
        while (true) {
            char outBuffer[HTTPS_READ_BUFFER_SIZE];
            int pending = BIO_read(context.wbio, outBuffer, sizeof(outBuffer));
            if (pending <= 0) {
                break;
            }

            if (!context.connection || !context.connection->Send(outBuffer, static_cast<size_t>(pending))) {
                MCP_LOG(MCP_LOG_LEVEL_ERROR, "failed to send TLS handshake data, fd=" + std::to_string(fileDescriptor));
                CleanupConnection(fileDescriptor);
                return false;
            }
        }
        return true;
    };

    if (!context.handshaked) {
        int result = SSL_accept(context.ssl);
        if (result == 1) {
            context.handshaked = true;
            MCP_LOG(MCP_LOG_LEVEL_INFO, "SSL_accept succeeded for HTTPS connection, fd=" +
                    std::to_string(fileDescriptor));

            if (!flushTlsPendingData()) {
                return;
            }
        } else {
            int errorCode = SSL_get_error(context.ssl, result);
            if (errorCode == SSL_ERROR_WANT_READ || errorCode == SSL_ERROR_WANT_WRITE) {
                if (!flushTlsPendingData()) {
                    return;
                }
                return;
            }
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "SSL_accept failed for HTTPS connection, fd=" +
                std::to_string(fileDescriptor) + " err=" + std::to_string(errorCode));
            CleanupConnection(fileDescriptor);
            return;
        }
    }

    while (true) {
        char plainBuffer[HTTPS_READ_BUFFER_SIZE];
        int bytesRead = SSL_read(context.ssl, plainBuffer, sizeof(plainBuffer));
        if (bytesRead <= 0) {
            int errorCode = SSL_get_error(context.ssl, bytesRead);
            if (errorCode == SSL_ERROR_WANT_READ || errorCode == SSL_ERROR_WANT_WRITE) {
                break;
            }
            MCP_LOG(MCP_LOG_LEVEL_INFO, "SSL_read returned " + std::to_string(bytesRead) +
                    " (err=" + std::to_string(errorCode) + "), closing HTTPS connection fd=" +
                    std::to_string(fileDescriptor));
            CleanupConnection(fileDescriptor);
            return;
        }

        OnRead(fileDescriptor, std::string(plainBuffer, static_cast<size_t>(bytesRead)));
    }
}

void HttpServer::HandleClose(const Mcp::Net::SocketPtr& socket)
{
    int fileDescriptor = socket ? socket->Fd() : -1;
    MCP_LOG(MCP_LOG_LEVEL_INFO, "connection closed, fd=" + std::to_string(fileDescriptor));
    if (fileDescriptor >= 0) {
        CleanupConnection(fileDescriptor);
    }
}

void HttpServer::HandleError(const Mcp::Net::SocketPtr& socket, int errorCode, const std::string& message)
{
    int fileDescriptor = socket ? socket->Fd() : -1;
    MCP_LOG(MCP_LOG_LEVEL_ERROR, "conn error fd=" + std::to_string(fileDescriptor) +
            " err=" + std::to_string(errorCode) + " msg=" + message);
    if (fileDescriptor >= 0) {
        CleanupConnection(fileDescriptor);
    }
}

int HttpServer::ParseRequest(const std::string& buffer, HttpRequest& outRequest, std::size_t& consumedBytes)
{
    outRequest = HttpRequest{};

    if (buffer.empty()) {
        return HTTP_PARSE_NEED_MORE;
    }

    http_parser parser;
    http_parser_settings settings{};
    ParserContext context{&outRequest};

    settings.on_url = HandleUrlCallback;
    settings.on_header_field = HandleHeaderFieldCallback;
    settings.on_header_value = HandleHeaderValueCallback;
    settings.on_body = HandleBodyCallback;
    settings.on_headers_complete = HandleHeadersCompleteCallback;
    settings.on_message_complete = HandleMessageCompleteCallback;

    http_parser_init(&parser, HTTP_REQUEST);
    parser.data = &context;

    size_t parsed = http_parser_execute(&parser, &settings, buffer.data(), buffer.size());
    if (parsed == 0 || parsed > buffer.size()) {
        return HTTP_PARSE_ERROR;
    }

    if (!context.messageCompleted) {
        return HTTP_PARSE_NEED_MORE;
    }

    consumedBytes = parsed;
    return HTTP_PARSE_OK;
}

void HttpServer::OnRead(int connectionFd, const std::string& data)
{
    auto iterator = connections_.find(connectionFd);
    if (iterator == connections_.end()) {
        return;
    }

    ConnectionContext& context = iterator->second;
    context.requestBuffer.append(data);

    while (true) {
        HttpRequest request;
        std::size_t consumedSize = 0;
        int parseResult = ParseRequest(context.requestBuffer, request, consumedSize);
        if (parseResult == HTTP_PARSE_NEED_MORE) {
            break; // need more data
        } else if (parseResult != HTTP_PARSE_OK) {
            HttpResponse response;
            response.statusCode = HTTP_STATUS_BAD_REQUEST;
            response.statusText = "Bad Request";
            response.body = "Failed to parse HTTP request";
            std::string rawResponse = BuildHttpResponse(response);
            SendRawResponse(connectionFd, rawResponse);
            context.requestBuffer.clear();
            break;
        }

        context.currentRequest = std::move(request);
        HandleRequest(connectionFd, context);

        context.requestBuffer.erase(0, consumedSize);
        context.currentRequest = HttpRequest{};
    }
}

void HttpServer::HandleRequest(int fileDescriptor, ConnectionContext& context)
{
    HttpResponse response;
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Request Method: " + context.currentRequest.method);
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Request URL: " + context.currentRequest.url);
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Request Version: " + context.currentRequest.version);
    for (const auto& header : context.currentRequest.headers) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Request Header: " + header.first + ": " + header.second);
    }
    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Request Body: " + context.currentRequest.body);
    // Build a minimal RequestContext so the handler signature matches HttpHandler.
    RequestContext requestContext{};
    requestContext.connectionId = static_cast<ConnectionId>(fileDescriptor);

    // Extract session ID from HTTP headers
    auto sessionIdIt = context.currentRequest.headers.find(Http::MCP_SESSION_ID_HEADER);
    if (sessionIdIt != context.currentRequest.headers.end()) {
        requestContext.sessionId = sessionIdIt->second;
    }

    requestContext.httpSendFunc = std::bind(&HttpServer::SendResponseAsync, this, std::placeholders::_1,
        std::placeholders::_2);

    auto iterator = routes_.find(context.currentRequest.url);
    if (iterator != routes_.end()) {
        try {
            iterator->second(context.currentRequest, requestContext);
            // Some handlers may send directly via httpSendFunc;
            return;
        } catch (const std::exception& e) {
            response.statusCode = HTTP_STATUS_INTERNAL_SERVER_ERROR;
            response.statusText = "Internal Server Error";
            response.body = std::string("Error: ") + e.what();
        }
    } else {
        response.statusCode = HTTP_STATUS_NOT_FOUND;
        response.statusText = "Not Found";
        response.body = "Endpoint not found";
    }

    std::string rawResponse = BuildHttpResponse(response);
    SendRawResponse(fileDescriptor, rawResponse);
}

std::string HttpServer::BuildHttpResponse(const HttpResponse& response) const
{
    std::ostringstream output;

    if (response.type == HttpSendType::HTTPRESPONSE || response.type == HttpSendType::HTTPRESPONSESTART) {
        output << "HTTP/1.1 " << response.statusCode << ' ' << response.statusText << "\r\n";
        for (const auto& header : response.headers) {
            output << header.first << ": " << header.second << "\r\n";
        }

        output << "Content-Length: " << response.body.size() << "\r\n";
        output << "\r\n";
    }

    if (response.type == HttpSendType::HTTPRESPONSE || response.type == HttpSendType::HTTPRESPONSEBODY) {
        output << response.body;
    }

    return output.str();
}

std::string HttpServer::BuildchunkedResponse(const HttpResponse& response, bool& chunkedEnabled) const
{
    std::ostringstream output;

    bool hasTransferEncoding = false;
    bool responseChunked = false;
    auto it = response.headers.find(TRANSFER_ENCODING_HEADER);
    if (it != response.headers.end()) {
        hasTransferEncoding = true;
        if (it->second.find(TRANSFER_ENCODING_CHUNKED) != std::string::npos) {
            responseChunked = true;
        }
    }

    if (hasTransferEncoding) {
        chunkedEnabled = responseChunked;
    }

    if (response.type == HttpSendType::HTTPRESPONSE || response.type == HttpSendType::HTTPRESPONSESTART) {
        output << "HTTP/1.1 " << response.statusCode << ' ' << response.statusText << "\r\n";
        for (const auto& header : response.headers) {
            output << header.first << ": " << header.second << "\r\n";
        }
        output << "\r\n";
    }

    if (chunkedEnabled) {
        if (response.type == HttpSendType::HTTPRESPONSEBODY ||
            (response.type == HttpSendType::HTTPRESPONSE && response.body.size() > 0)) {
                output << std::hex << response.body.size() << "\r\n";
                output << response.body << "\r\n";
        }
    }

    return output.str();
}

bool HttpServer::SendRawResponse(int fileDescriptor, const std::string& response)
{
    auto iterator = connections_.find(fileDescriptor);
    if (iterator == connections_.end()) {
        return false;
    }

    ConnectionContext& context = iterator->second;
    if (!context.connection) {
        return false;
    }

    if (context.ssl == nullptr) {
        if (!context.connection->Send(response.data(), response.size())) {
            CleanupConnection(fileDescriptor);
            return false;
        }
        return true;
    }

    int writeResult = SSL_write(context.ssl, response.data(), static_cast<int>(response.size()));
    if (writeResult <= 0) {
        int errorCode = SSL_get_error(context.ssl, writeResult);
        if (errorCode != SSL_ERROR_WANT_READ && errorCode != SSL_ERROR_WANT_WRITE) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "SSL_write failed for HTTPS response, fd=" +
                    std::to_string(fileDescriptor) + " err=" + std::to_string(errorCode));
            CleanupConnection(fileDescriptor);
            return false;
        }
    }

    while (true) {
        char outBuffer[HTTPS_READ_BUFFER_SIZE];
        int pending = BIO_read(context.wbio, outBuffer, sizeof(outBuffer));
        if (pending <= 0) {
            break;
        }

        if (!context.connection->Send(outBuffer, static_cast<size_t>(pending))) {
            CleanupConnection(fileDescriptor);
            return false;
        }
    }

    return true;
}

bool HttpServer::SendResponse(int connectionFd, const HttpResponse& response)
{
    auto iterator = connections_.find(connectionFd);
    if (iterator == connections_.end()) {
        return false;
    }

    ConnectionContext& context = iterator->second;

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Response Status: " + std::to_string(response.statusCode) + " " + response.statusText);
    for (const auto& header : response.headers) {
        MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Response Header: " + header.first + ": " + header.second);
    }

    MCP_LOG(MCP_LOG_LEVEL_DEBUG, "Response Body: " + response.body);

    bool isChunked = false;
    auto it = response.headers.find(TRANSFER_ENCODING_HEADER);
    if (it != response.headers.end() && it->second.find(TRANSFER_ENCODING_CHUNKED) != std::string::npos) {
        isChunked = true;
    } else if (context.sseChunked) {
        isChunked = true;
    }

    std::string rawResponse;
    if (isChunked) {
        rawResponse = BuildchunkedResponse(response, context.sseChunked);
    } else {
        rawResponse = BuildHttpResponse(response);
    }
    return SendRawResponse(connectionFd, rawResponse);
}

void HttpServer::CleanupConnection(int fileDescriptor)
{
    auto iterator = connections_.find(fileDescriptor);
    if (iterator != connections_.end()) {
        ConnectionContext& context = iterator->second;
        if (context.ssl != nullptr) {
            SSL_shutdown(context.ssl);
            SSL_free(context.ssl);
            context.ssl = nullptr;
        }
        // The rbio/wbio BIOs are owned by the SSL object after SSL_set_bio,
        // so they are freed as part of SSL_free above. Just clear pointers.
        context.rbio = nullptr;
        context.wbio = nullptr;
        if (context.connection) {
            context.connection->Close();
        }
        connections_.erase(iterator);
    }
}

void HttpServer::InitializeSslContext()
{
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    const SSL_METHOD* method = TLS_server_method();
    if (method == nullptr) {
        throw std::runtime_error("TLS_server_method failed");
    }

    SSL_CTX* context = SSL_CTX_new(method);
    if (context == nullptr) {
        throw std::runtime_error("SSL_CTX_new failed");
    }

    if (SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION) != 1) {
        SSL_CTX_free(context);
        throw std::runtime_error("SSL_CTX_set_min_proto_version failed");
    }

    if (SSL_CTX_set_cipher_list(context, "HIGH:!aNULL:!MD5") != 1) {
        SSL_CTX_free(context);
        throw std::runtime_error("SSL_CTX_set_cipher_list failed");
    }

    if (SSL_CTX_use_certificate_file(context, tlsConfig_.certFile.c_str(), SSL_FILETYPE_PEM) != 1) {
        SSL_CTX_free(context);
        throw std::runtime_error("SSL_CTX_use_certificate_file failed");
    }

    if (SSL_CTX_use_PrivateKey_file(context, tlsConfig_.keyFile.c_str(), SSL_FILETYPE_PEM) != 1) {
        SSL_CTX_free(context);
        throw std::runtime_error("SSL_CTX_use_PrivateKey_file failed");
    }

    if (!tlsConfig_.caFile.empty()) {
        if (SSL_CTX_load_verify_locations(context, tlsConfig_.caFile.c_str(), nullptr) != 1) {
            SSL_CTX_free(context);
            throw std::runtime_error("SSL_CTX_load_verify_locations failed");
        }
    }

    if (tlsConfig_.verifyPeer) {
        SSL_CTX_set_verify(context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    } else {
        SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);
    }

    sslContext_ = context;
}

} // namespace Mcp::Http
