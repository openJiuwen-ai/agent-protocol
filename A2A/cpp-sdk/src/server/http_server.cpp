/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 */

#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

extern "C" {
#include <http_parser.h>
}
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "a2a_log.h"
#include "shared/http_common.h"
#include "shared/thread_utils.h"
#include "http_server.h"

namespace A2A::Server {

namespace {

struct ParserContext {
    Http::HttpRequest* request {nullptr};
    std::string currentField {};
    std::string currentValue {};
    bool lastWasValue {false};
    bool messageCompleted {false};
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
        A2A::Http::TrimInPlace(ctx->currentField);
        A2A::Http::TrimInPlace(ctx->currentValue);
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
        A2A::Http::TrimInPlace(ctx->currentField);
        A2A::Http::TrimInPlace(ctx->currentValue);
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
    size_t ioThreadIndex, const ConnectionTimeoutConfig& timeoutConfig)
    : eventSystem_(true, static_cast<int>(ioThreadIndex)),
    listener_(std::make_unique<TcpListener>(eventSystem_)),
    routes_(routes),
    host_(host),
    port_(port),
    tlsConfig_(tlsConfig),
    timeoutConfig_(timeoutConfig),
    ioThreadIndex_(ioThreadIndex)
{
}

HttpServer::~HttpServer()
{
    Stop();
    if (sslContext_ != nullptr) {
        SSL_CTX_free(sslContext_);
        sslContext_ = nullptr;
    }
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
        taskQueue_ =
            std::make_unique<A2A::MPSCNotifyQueue<std::function<void()>>>(DEFAULT_MPSC_QUEUE_SIZE,
                                                                            HTTP_QUEUE_MAX_BATCH_SIZE);
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
    listener_->OnError([](const SocketPtr& socket, int errorCode, const std::string& message) {
        int fileDescriptor = socket ? socket->Fd() : -1;
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "listener error fd=" + std::to_string(fileDescriptor) +
                " err=" + std::to_string(errorCode) + " msg=" + message);
    });

    listener_->OnNewConnection(
        [this](const TcpSocketPtr& connection) { this->HandleNewConnection(connection); });

    if (!listener_->Listen(host_, port_, HTTP_LISTEN_BACKLOG, true)) {
        if (sslContext_ != nullptr) {
            SSL_CTX_free(sslContext_);
            sslContext_ = nullptr;
        }
        running_ = false;
        throw std::runtime_error("listen failed");
    }

    if (!listener_->Start()) {
        if (sslContext_ != nullptr) {
            SSL_CTX_free(sslContext_);
            sslContext_ = nullptr;
        }
        running_ = false;
        throw std::runtime_error("start listener failed");
    }

    RegisterIdleConnectionSweep();

    eventThread_ = std::thread([this]() {
        SetCurrentThreadName("A2A-IO-" + std::to_string(ioThreadIndex_));
        eventSystem_.Start(false);
        running_ = false;
    });
}

void HttpServer::Stop()
{
    const bool wasRunning = running_.exchange(false);

    if (listener_ != nullptr) {
        listener_->Stop();
    }

    // Stop the event loop and join the IO thread before touching connections_. Everything that
    // mutates connections_ - the read/close handlers and the periodic sweep - runs on that
    // thread, and removing the sweep timer would not wait for a sweep that is already
    // executing, so tearing connections_ down from this thread first would race with it.
    if (wasRunning) {
        if (taskQueue_ != nullptr) {
            taskQueue_->Cleanup();
        }
        eventSystem_.Stop();
        if (eventThread_.joinable()) {
            eventThread_.join();
        }
    }

    // The IO thread is gone, so the sweep can no longer fire and connections_ is ours alone.
    if (sweepTimerId_ >= 0) {
        eventSystem_.RemoveEvent(sweepTimerId_);
        sweepTimerId_ = -1;
    }

    std::vector<int> activeFds;
    activeFds.reserve(connections_.size());
    for (const auto& entry : connections_) {
        activeFds.push_back(entry.first);
    }
    for (int fd : activeFds) {
        CleanupConnection(fd);
    }
}

void HttpServer::SendResponseAsync(const Http::HttpResponse& response, const Http::HttpRequestContext& ctx)
{
    if (!running_.load()) {
        return;
    }
    if (taskQueue_ == nullptr) {
        return;
    }
    Http::HttpResponse responseCopy = response;
    int connectionId = ctx.connectionId;
    uint64_t connectionSequence = ctx.connectionSequence;
    taskQueue_->Send([this, connectionId, connectionSequence, responseCopy]() {
        this->SendResponse(connectionId, connectionSequence, responseCopy);
    });
}

void HttpServer::HandleNewConnection(const TcpSocketPtr& connection)
{
    if (!connection) {
        return;
    }
    int fileDescriptor = connection->Fd();
    ConnectionContext context;
    context.connection = connection;
    context.lastActivity = std::chrono::steady_clock::now();
    context.sequence = nextConnectionSequence_++;

    if (tlsConfig_.enabled) {
        SSL* ssl = SSL_new(sslContext_);
        if (ssl == nullptr) {
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "SSL_new failed for HTTPS connection");
            connection->Close();
            return;
        }

        BIO* rbio = BIO_new(BIO_s_mem());
        BIO* wbio = BIO_new(BIO_s_mem());
        if (rbio == nullptr || wbio == nullptr) {
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "BIO_new failed for HTTPS connection");
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

    connection->OnError([this](const SocketPtr& socket, int errorCode, const std::string& message) {
        this->HandleError(socket, errorCode, message);
    });

    connection->OnClose([this](const SocketPtr& socket) { this->HandleClose(socket); });

    // The socket owns this callback, so capturing the connection here would make it reference
    // itself and leak: CleanupConnection releases every other reference but cannot clear the
    // callback while it is running. The socket is handed in as the argument instead.
    connection->OnRead([this](const SocketPtr& socket) {
        this->HandleRead(std::static_pointer_cast<TcpSocket>(socket));
    });
}

void HttpServer::HandleRead(const TcpSocketPtr& connection)
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

    Buffer& buffer = connection->InputBuffer();
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
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "BIO_write failed for HTTPS connection, fd=" + std::to_string(fileDescriptor));
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
                A2A_LOG(A2A_LOG_LEVEL::ERROR, "failed to send TLS handshake data, fd=" +
                        std::to_string(fileDescriptor));
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
            A2A_LOG(A2A_LOG_LEVEL::INFO, "SSL_accept succeeded for HTTPS connection, fd=" +
                    std::to_string(fileDescriptor));

            if (!flushTlsPendingData()) {
                return;
            }
        } else {
            int errorCode = SSL_get_error(context.ssl, result);
            if (errorCode == SSL_ERROR_WANT_READ || errorCode == SSL_ERROR_WANT_WRITE) {
                flushTlsPendingData();
                return;
            }
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "SSL_accept failed for HTTPS connection, fd=" +
                std::to_string(fileDescriptor) + " err=" + std::to_string(errorCode));
            CleanupConnection(fileDescriptor);
            return;
        }
    }

    while (true) {
        // OnRead below can clean the connection up (unparsable request, failed write), which
        // erases this entry and frees the SSL object along with it, so the context has to be
        // re-resolved before every read instead of held across the call.
        auto readIterator = connections_.find(fileDescriptor);
        if (readIterator == connections_.end()) {
            return;
        }
        ConnectionContext& readContext = readIterator->second;

        char plainBuffer[HTTPS_READ_BUFFER_SIZE];
        int bytesRead = SSL_read(readContext.ssl, plainBuffer, sizeof(plainBuffer));
        if (bytesRead <= 0) {
            int errorCode = SSL_get_error(readContext.ssl, bytesRead);
            if (errorCode == SSL_ERROR_WANT_READ || errorCode == SSL_ERROR_WANT_WRITE) {
                break;
            }
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "SSL_read returned " + std::to_string(bytesRead) +
                    " (err=" + std::to_string(errorCode) + "), closing HTTPS connection fd=" +
                    std::to_string(fileDescriptor));
            CleanupConnection(fileDescriptor);
            return;
        }

        OnRead(fileDescriptor, std::string(plainBuffer, static_cast<size_t>(bytesRead)));
    }
}

void HttpServer::HandleClose(const SocketPtr& socket)
{
    if (!socket) {
        return;
    }
    const int fileDescriptor = socket->Fd();
    A2A_LOG(A2A_LOG_LEVEL::INFO, "connection closed, fd=" + std::to_string(fileDescriptor));
    if (fileDescriptor >= 0) {
        CleanupConnection(fileDescriptor);
        return;
    }
    // Socket::Close() clears fd before OnClose; match by connection pointer.
    for (auto it = connections_.begin(); it != connections_.end(); ++it) {
        if (it->second.connection == socket) {
            CleanupConnection(it->first);
            return;
        }
    }
}

void HttpServer::HandleError(const SocketPtr& socket, int errorCode, const std::string& message)
{
    if (!socket) {
        return;
    }
    const int fileDescriptor = socket->Fd();
    A2A_LOG(A2A_LOG_LEVEL::ERROR, "conn error fd=" + std::to_string(fileDescriptor) +
            " err=" + std::to_string(errorCode) + " msg=" + message);
    if (fileDescriptor >= 0) {
        CleanupConnection(fileDescriptor);
        return;
    }
    for (auto it = connections_.begin(); it != connections_.end(); ++it) {
        if (it->second.connection == socket) {
            CleanupConnection(it->first);
            return;
        }
    }
}

int HttpServer::ParseRequest(const std::string& buffer, A2A::Http::HttpRequest& outRequest,
    std::size_t& consumedBytes) const
{
    outRequest = A2A::Http::HttpRequest{};

    if (buffer.empty()) {
        return Http::HTTP_PARSE_NEED_MORE;
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
        return Http::HTTP_PARSE_ERROR;
    }

    if (!context.messageCompleted) {
        return Http::HTTP_PARSE_NEED_MORE;
    }

    consumedBytes = parsed;
    return Http::HTTP_PARSE_OK;
}

void HttpServer::OnRead(int connectionFd, const std::string& data)
{
    auto iterator = connections_.find(connectionFd);
    if (iterator == connections_.end()) {
        return;
    }
    iterator->second.requestBuffer.append(data);

    while (true) {
        // Re-resolve the context on every round instead of holding a reference across the
        // calls below: writing a response can fail, and the failure path cleans the connection
        // up and erases this entry, leaving any reference to it dangling.
        iterator = connections_.find(connectionFd);
        if (iterator == connections_.end()) {
            return;
        }

        Http::HttpRequest request;
        std::size_t consumedSize = 0;
        int parseResult = ParseRequest(iterator->second.requestBuffer, request, consumedSize);
        if (parseResult == Http::HTTP_PARSE_NEED_MORE) {
            break; // need more data
        } else if (parseResult != Http::HTTP_PARSE_OK) {
            Http::HttpResponse response;
            response.statusCode = Http::HTTP_STATUS_BAD_REQUEST;
            response.statusText = "Bad Request";
            response.body = "Failed to parse HTTP request";
            // Same reason as the fallbacks in HandleRequest: the response must be framed for
            // this connection, not written raw into a stream that may already be open.
            SendGeneratedResponse(connectionFd, response);

            iterator = connections_.find(connectionFd);
            if (iterator != connections_.end()) {
                iterator->second.requestBuffer.clear();
            }
            break;
        }

        iterator->second.currentRequest = std::move(request);
        HandleRequest(connectionFd, iterator->second);

        iterator = connections_.find(connectionFd);
        if (iterator == connections_.end()) {
            return;
        }
        iterator->second.requestBuffer.erase(0, consumedSize);
        iterator->second.currentRequest = Http::HttpRequest{};
    }
}

void HttpServer::HandleRequest(int fileDescriptor, ConnectionContext& context)
{
    Http::HttpResponse response;
    // Build a minimal Http::HttpRequestContext so the handler signature matches HttpHandler.
    Http::HttpRequestContext requestContext{};
    requestContext.connectionId = static_cast<int>(fileDescriptor);
    requestContext.connectionSequence = context.sequence;

    requestContext.httpSendFunc = std::bind(&HttpServer::SendResponseAsync, this, std::placeholders::_1,
        std::placeholders::_2);

    auto iterator = routes_.find(context.currentRequest.url);
    if (iterator != routes_.end()) {
        // The handler may hand the request to a worker thread and answer much later, leaving the
        // connection silent in the meantime. Mark it as serving a request so the sweep applies the
        // request budget instead of the idle timeout. Cleared once the response is complete.
        context.inFlight = true;
        try {
            iterator->second(context.currentRequest, requestContext);
            // Some handlers may send directly via httpSendFunc;
            return;
        } catch (const std::exception& e) {
            response.statusCode = Http::HTTP_STATUS_INTERNAL_SERVER_ERROR;
            response.statusText = "Internal Server Error";
            response.body = std::string("Error: ") + e.what();
        }
    } else {
        response.statusCode = Http::HTTP_STATUS_NOT_FOUND;
        response.statusText = "Not Found";
        response.body = "Endpoint not found";
    }

    // Answer through the shared path for server-generated responses so an already-started
    // chunked stream is not corrupted by a second status line.
    SendGeneratedResponse(fileDescriptor, response);
}

void HttpServer::SendGeneratedResponse(int fileDescriptor, const Http::HttpResponse& response)
{
    auto iterator = connections_.find(fileDescriptor);
    if (iterator == connections_.end()) {
        return;
    }

    if (!iterator->second.sseChunked) {
        SendResponse(fileDescriptor, response);
        return;
    }

    // A chunked response is already on the wire, so its status line and headers have been sent
    // and must not be repeated: BuildchunkedResponse would emit a second header block in the
    // middle of the body. The body cannot be delivered either - this layer does not know the
    // payload format the stream is carrying, and inventing one here would hand the client
    // something its parser does not accept. So only close the stream out cleanly and leave the
    // detail in the log: a terminated stream is something the client can act on, a corrupted
    // one is not.
    A2A_LOG(A2A_LOG_LEVEL::ERROR, "cannot deliver generated response on a started stream, "
            "terminating it instead, status=" + std::to_string(response.statusCode) + " body=" + response.body);

    Http::HttpResponse done;
    done.type = Http::HttpSendType::HTTPRESPONSEBODY; // empty body = terminating chunk
    SendResponse(fileDescriptor, done);
}

std::string HttpServer::BuildHttpResponse(const Http::HttpResponse& response) const
{
    std::ostringstream output;

    if (response.type == Http::HttpSendType::HTTPRESPONSE || response.type == Http::HttpSendType::HTTPRESPONSESTART) {
        output << "HTTP/1.1 " << response.statusCode << ' ' << response.statusText << "\r\n";
        for (const auto& header : response.headers) {
            output << header.first << ": " << header.second << "\r\n";
        }

        output << "Content-Length: " << response.body.size() << "\r\n";
        output << "\r\n";
    }

    if (response.type == Http::HttpSendType::HTTPRESPONSE || response.type == Http::HttpSendType::HTTPRESPONSEBODY) {
        output << response.body;
    }

    return output.str();
}

std::string HttpServer::BuildchunkedResponse(const Http::HttpResponse& response, bool& chunkedEnabled) const
{
    std::ostringstream output;

    bool hasTransferEncoding = false;
    bool responseChunked = false;

    std::string transferEncoding = GetHeaderValue(response, Http::TRANSFER_ENCODING_HEADER);
    if (!transferEncoding.empty()) {
        hasTransferEncoding = true;
        if (transferEncoding.find(Http::TRANSFER_ENCODING_CHUNKED) != std::string::npos) {
            responseChunked = true;
        }
    }

    if (hasTransferEncoding) {
        chunkedEnabled = responseChunked;
    }

    if (response.type == Http::HttpSendType::HTTPRESPONSE || response.type == Http::HttpSendType::HTTPRESPONSESTART) {
        output << "HTTP/1.1 " << response.statusCode << ' ' << response.statusText << "\r\n";
        for (const auto& header : response.headers) {
            output << header.first << ": " << header.second << "\r\n";
        }
        output << "\r\n";
    }

    if (chunkedEnabled) {
        if (response.type == Http::HttpSendType::HTTPRESPONSEBODY ||
            (response.type == Http::HttpSendType::HTTPRESPONSE && response.body.size() > 0)) {
            output << std::hex << response.body.size() << "\r\n";
            output << response.body << "\r\n";
            }
        if (response.type == Http::HttpSendType::HTTPRESPONSE) {
            // A one-shot response carries the whole body, so the stream ends here. Without the
            // terminating chunk the client cannot tell the body is complete and would wait for
            // more chunks until its own timeout fires.
            output << "0\r\n\r\n";
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
    // Sending data counts as activity so that connections with an ongoing response
    // stream are not reclaimed by the idle-connection sweep.
    context.lastActivity = std::chrono::steady_clock::now();
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
            A2A_LOG(A2A_LOG_LEVEL::ERROR, "SSL_write failed for HTTPS response, fd=" +
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

bool HttpServer::SendResponse(int connectionFd, uint64_t connectionSequence, const Http::HttpResponse& response)
{
    auto iterator = connections_.find(connectionFd);
    if (iterator == connections_.end()) {
        // Routine: the client may disconnect, or the connection may be reclaimed, while a
        // handler is still emitting. Nothing can be done about it, so keep it out of the logs.
        A2A_LOG(A2A_LOG_LEVEL::DEBUG, "dropping response, connection already closed, conn_seq=" +
                std::to_string(connectionSequence));
        return false;
    }

    // A sequence of 0 means the caller did not come through HandleRequest and cannot be checked.
    if (connectionSequence != 0 && connectionSequence != iterator->second.sequence) {
        A2A_LOG(A2A_LOG_LEVEL::WARN, "dropping response, connection was reclaimed and its slot reused, conn_seq=" +
                std::to_string(connectionSequence));
        return false;
    }

    return SendResponse(connectionFd, response);
}

bool HttpServer::SendResponse(int connectionFd, const Http::HttpResponse& response)
{
    auto iterator = connections_.find(connectionFd);
    if (iterator == connections_.end()) {
        return false;
    }

    ConnectionContext& context = iterator->second;

    A2A_LOG(A2A_LOG_LEVEL::DEBUG, "Response Status: " + std::to_string(response.statusCode) + " " +
            response.statusText);

    bool isChunked = false;
    std::string transferEncoding = GetHeaderValue(response, Http::TRANSFER_ENCODING_HEADER);
    if (!transferEncoding.empty() && transferEncoding.find(Http::TRANSFER_ENCODING_CHUNKED) != std::string::npos) {
        isChunked = true;
    } else if (response.streaming && context.sseChunked) {
        isChunked = true;
    }

    std::string rawResponse;
    if (isChunked) {
        rawResponse = BuildchunkedResponse(response, context.sseChunked);
    } else {
        rawResponse = BuildHttpResponse(response);
    }

    const bool completesResponse = CompletesResponse(response);
    if (!SendRawResponse(connectionFd, rawResponse)) {
        return false;
    }

    if (completesResponse) {
        // SendRawResponse may have dropped the connection, so look it up again instead of
        // reusing the reference above.
        auto sentIterator = connections_.find(connectionFd);
        if (sentIterator != connections_.end()) {
            sentIterator->second.inFlight = false;
            // The exchange is over: a kept-alive connection must negotiate chunking again for
            // the next request instead of inheriting this one's state.
            sentIterator->second.sseChunked = false;
        }
    }
    return true;
}

bool HttpServer::CompletesResponse(const Http::HttpResponse& response)
{
    if (response.type == Http::HttpSendType::HTTPRESPONSESTART) {
        return false; // Headers only, the body is still to come.
    }
    if (response.type == Http::HttpSendType::HTTPRESPONSEBODY) {
        // An empty body is how a handler signals the end of a stream: on a chunked connection
        // it is written out as the terminating zero-length chunk, elsewhere it writes nothing
        // at all. A non-empty one is just another event and more may follow.
        return response.body.empty();
    }
    // A one-shot full response ends the exchange: it carries the whole body, and on a chunked
    // connection BuildchunkedResponse terminates the stream as part of writing it.
    return true;
}

void HttpServer::CleanupConnection(int fileDescriptor)
{
    auto iterator = connections_.find(fileDescriptor);
    if (iterator == connections_.end()) {
        return;
    }

    ConnectionContext context = std::move(iterator->second);
    connections_.erase(iterator);

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
        context.connection->OnClose(nullptr);
        context.connection->OnError(nullptr);
        context.connection->Close();
    }
}

long HttpServer::ResolveSweepIntervalMs(const ConnectionTimeoutConfig& config)
{
    unsigned int intervalSeconds = config.sweepIntervalSeconds;
    if (intervalSeconds == 0) {
        intervalSeconds = DEFAULT_CONNECTION_SWEEP_INTERVAL_SECONDS;
    }
    // Never sweep less often than the shortest enabled timeout, otherwise connections would
    // outlive their budget by up to a whole sweep interval.
    if (config.timeoutSeconds != 0 && intervalSeconds > config.timeoutSeconds) {
        intervalSeconds = config.timeoutSeconds;
    }
    if (config.requestTimeoutSeconds != 0 && intervalSeconds > config.requestTimeoutSeconds) {
        intervalSeconds = config.requestTimeoutSeconds;
    }
    return static_cast<long>(intervalSeconds) * 1000L;
}

void HttpServer::RegisterIdleConnectionSweep()
{
    if (timeoutConfig_.timeoutSeconds == 0 && timeoutConfig_.requestTimeoutSeconds == 0) {
        return; // Reclamation disabled by configuration.
    }

    const long intervalMs = ResolveSweepIntervalMs(timeoutConfig_);
    sweepTimerId_ = eventSystem_.AddTimer(intervalMs,
        [this](int /* fd */, short /* events */, void* /* arg */) {
            CloseIdleConnections(std::chrono::steady_clock::now());
        },
        this, true);
    if (sweepTimerId_ < 0) {
        A2A_LOG(A2A_LOG_LEVEL::ERROR, "failed to register idle connection sweep timer");
    }
}

bool HttpServer::IsConnectionExpired(const ConnectionContext& context, std::chrono::steady_clock::time_point now) const
{
    // A connection serving a request is silent by design while the handler works, so it gets the
    // request budget; only a connection the server is waiting on is held to the idle timeout.
    const unsigned int timeoutSeconds =
        context.inFlight ? timeoutConfig_.requestTimeoutSeconds : timeoutConfig_.timeoutSeconds;
    if (timeoutSeconds == 0) {
        return false; // This budget is disabled.
    }
    // lastActivity tracks the last progress in either direction, so for a streaming response this
    // measures the gap between chunks rather than the total duration of the stream.
    return now - context.lastActivity > std::chrono::seconds(timeoutSeconds);
}

void HttpServer::CloseIdleConnections(std::chrono::steady_clock::time_point now)
{
    if (timeoutConfig_.timeoutSeconds == 0 && timeoutConfig_.requestTimeoutSeconds == 0) {
        return; // Reclamation disabled by configuration.
    }

    std::vector<int> expiredFds;
    for (const auto& entry : connections_) {
        if (IsConnectionExpired(entry.second, now)) {
            expiredFds.push_back(entry.first);
            A2A_LOG(A2A_LOG_LEVEL::INFO, std::string("closing connection with no progress, in_flight=") +
                    (entry.second.inFlight ? "true" : "false") + " timeout=" +
                    std::to_string(entry.second.inFlight ? timeoutConfig_.requestTimeoutSeconds
                                                         : timeoutConfig_.timeoutSeconds) +
                    "s conn_seq=" + std::to_string(entry.second.sequence));
        }
    }

    for (int fileDescriptor : expiredFds) {
        CleanupConnection(fileDescriptor);
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

} // namespace A2A::Server