/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

/*
 * 超时功能打桩验证（无需真实 server）。
 *
 * 进程内启动一个"只 accept、永不回包"的 TCP 桩服务：连接建立成功但响应
 * 永不到达，从而逐一触发各阶段超时。覆盖五个场景：
 *
 *   [1] AgentCard 发现超时        GetAgentCard(path, timeout) 形参
 *   [2] 非流式 per-call 超时      SendMessage(..., timeout) 形参覆盖 config
 *   [3] 非流式 config 级超时      ClientConfig::timeoutSeconds
 *   [4] 流式空闲超时（config）    ClientConfig::streamingIdleTimeoutSeconds
 *   [5] 流式空闲超时（per-call）  SendMessage timeout 形参覆盖 config
 *
 * 每个场景校验四点：实际耗时 ≈ 配置值、异常类型为 A2AClientTimeoutError、
 * 错误码为 A2A_REQUEST_TIMEOUT(-32101)、消息携带 stage 与 elapsed。
 *
 * 用法：./timeout_client（退出码 = 失败场景数，0 = 全部通过）
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "a2a_log.h"
#include "error.h"
#include "types.h"
#include "client/client_factory.h"
#include "client/http_card_resolver_builder.h"

namespace {

using Clock = std::chrono::steady_clock;

// 兜底余量：若超时机制失效，等待 guard = 预期超时 + 15s 后判定 FAIL，
// 避免用例挂死。
constexpr int GUARD_MARGIN_SECS = 15;
// 耗时下限容差（毫秒）：容忍超时定时器略早于配置值触发。
constexpr long EARLY_TOLERANCE_MS = 200;
// 耗时上限容差（毫秒）：容忍调度延迟导致的超时晚触发。
constexpr long ELAPSED_TOLERANCE_MS = 3000;
// 桩服务 listen backlog。
constexpr int STUB_LISTEN_BACKLOG = 16;

int g_failures = 0;

// ===========================================================================
// 打桩服务：accept 连接后既不读也不写，模拟"服务端静默"。
// 对非流式请求触发 CURLOPT_TIMEOUT_MS 总超时；对流式请求触发空闲超时。
// ===========================================================================
class StallStub {
public:
    StallStub() = default;
    ~StallStub()
    {
        Stop();
    }

    bool Start()
    {
        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            return false;
        }
        int reuse = 1;
        setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = 0; // 由内核分配临时端口，避免冲突
        if (::bind(listenFd_, static_cast<const sockaddr*>(static_cast<void*>(&addr)), sizeof(addr)) != 0 ||
            listen(listenFd_, STUB_LISTEN_BACKLOG) != 0) {
            std::cerr << "StallStub bind/listen failed: " << std::strerror(errno) << "\n";
            close(listenFd_);
            listenFd_ = -1;
            return false;
        }

        sockaddr_in actual{};
        socklen_t len = sizeof(actual);
        if (getsockname(listenFd_, static_cast<sockaddr*>(static_cast<void*>(&actual)), &len) != 0) {
            close(listenFd_);
            listenFd_ = -1;
            return false;
        }
        port_ = ntohs(actual.sin_port);

        acceptThread_ = std::thread([this] { AcceptLoop(); });
        return true;
    }

    uint16_t Port() const
    {
        return port_;
    }

    std::string BaseUrl() const
    {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    void Stop()
    {
        int fd = listenFd_;
        listenFd_ = -1;
        if (fd >= 0) {
            shutdown(fd, SHUT_RDWR); // 唤醒阻塞中的 accept
            close(fd);
        }
        if (acceptThread_.joinable()) {
            acceptThread_.join();
        }
        std::lock_guard<std::mutex> lock(connMutex_);
        for (int connFd : connFds_) {
            close(connFd);
        }
        connFds_.clear();
    }

private:
    void AcceptLoop()
    {
        while (listenFd_ >= 0) {
            int connFd = accept(listenFd_, nullptr, nullptr);
            if (connFd < 0) {
                break;
            }
            // 桩行为：握住连接，永不发送任何字节。
            std::lock_guard<std::mutex> lock(connMutex_);
            connFds_.push_back(connFd);
        }
    }

    // 跨线程访问（主线程写、accept 线程读），用原子量避免数据竞争
    std::atomic<int> listenFd_{-1};
    uint16_t port_ = 0;
    std::thread acceptThread_;
    std::mutex connMutex_;
    std::vector<int> connFds_;
};

// ===========================================================================
// 公共构造
// ===========================================================================
A2A::AgentCard MakeCard(const std::string& baseUrl, bool streaming)
{
    A2A::AgentCard card;
    card.name = "TimeoutStubAgent";
    card.description = "stall stub for timeout verification";
    card.version = "1.0.0";
    card.defaultInputModes = {"text"};
    card.defaultOutputModes = {"text"};
    card.capabilities.streaming = streaming;
    A2A::AgentInterface primaryInterface;
    primaryInterface.url = baseUrl + "/jsonrpc";
    primaryInterface.protocolBinding = "JSONRPC";
    primaryInterface.protocolVersion = "1.0";
    card.supportedInterfaces = {primaryInterface};
    return card;
}

A2A::Message MakeMessage()
{
    A2A::Message msg;
    msg.role = A2A::Role::USER;
    msg.messageId = "timeout-verify";
    A2A::Part part;
    part.text = "hello";
    part.mediaType = "text/plain";
    msg.parts.push_back(part);
    return msg;
}

// 记录 ResponseHandler 收到的错误（超时应同时经 handler 与 future 两条路送达）
struct HandlerObs {
    std::atomic<bool> sawError{false};
    std::atomic<int> code{0};
    std::atomic<bool> sawOther{false};
};

// handler 由 SDK 持有、可能在局部对象析构后才执行：按值捕获 shared_ptr，
// 避免 lambda 按引用捕获局部变量后逃逸出本函数（悬挂引用）。
A2A::Client::ResponseHandler MakeHandler(const std::shared_ptr<HandlerObs>& obs)
{
    return [obs](const A2A::Client::ClientEvent& ev, const A2A::AgentCard&) {
        if (const auto* err = std::get_if<A2A::A2AError>(&ev)) {
            obs->sawError = true;
            obs->code = err->code;
        } else {
            obs->sawOther = true;
        }
    };
}

// ===========================================================================
// 等待 future（带 guard）并归类结果
// ===========================================================================
struct TimeoutObs {
    bool guardTripped = false;       // guard 窗口内未完成（超时未生效）
    bool threwTimeout = false;       // future 抛出 A2AClientTimeoutError
    int code = 0;                    // 异常错误码
    std::string message;             // 异常消息
    std::string otherException;      // 其他异常的描述
    long elapsedMs = -1;             // 请求发出 → future 就绪的耗时
};

template <typename Fut>
TimeoutObs AwaitTimeout(Fut& fut, int guardSecs, Clock::time_point t0)
{
    TimeoutObs obs;
    if (fut.wait_for(std::chrono::seconds(guardSecs)) != std::future_status::ready) {
        obs.guardTripped = true;
        return obs;
    }
    obs.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    try {
        fut.get();
        return obs; // 正常完成，未抛异常
    } catch (const A2A::A2AClientTimeoutError& e) {
        obs.threwTimeout = true;
        obs.code = e.errorCode;
        obs.message = e.message;
        return obs;
    } catch (const std::exception& e) {
        obs.otherException = e.what();
        return obs;
    }
}

// ===========================================================================
// 判定并打印
// ===========================================================================
bool Report(const std::string& title, int expectedSecs, const std::string& expectedStage,
    const TimeoutObs& obs, bool expectHandlerError, bool handlerSawError)
{
    const long expectedMs = static_cast<long>(expectedSecs) * 1000;
    std::vector<std::string> problems;

    if (obs.guardTripped) {
        problems.push_back("超时未生效：guard 窗口（" + std::to_string(expectedSecs + GUARD_MARGIN_SECS) +
            "s）内请求未结束");
    } else if (!obs.threwTimeout) {
        problems.push_back(obs.otherException.empty() ? "请求正常完成，未抛超时异常" :
            "抛出了非超时异常: " + obs.otherException);
    }
    if (obs.threwTimeout) {
        if (obs.code != static_cast<int>(A2A::A2AErrorCode::A2A_REQUEST_TIMEOUT)) {
            problems.push_back("错误码 " + std::to_string(obs.code) + " != A2A_REQUEST_TIMEOUT(-32101)");
        }
        if (obs.message.find("stage=" + expectedStage) == std::string::npos) {
            problems.push_back("消息缺少 stage=" + expectedStage + "，实际: " + obs.message);
        }
        if (obs.elapsedMs < expectedMs - EARLY_TOLERANCE_MS || obs.elapsedMs > expectedMs + ELAPSED_TOLERANCE_MS) {
            problems.push_back("耗时 " + std::to_string(obs.elapsedMs) + "ms 偏离期望 " +
                std::to_string(expectedMs) + "ms");
        }
    }
    if (expectHandlerError && !handlerSawError) {
        problems.push_back("handler 未收到 A2AError（错误应同时经 handler 与 future 送达）");
    }

    std::cout << "    期望: " << expectedSecs << "s 超时, A2AClientTimeoutError(-32101), stage=" << expectedStage <<
        "\n";
    if (!obs.guardTripped) {
        std::cout << "    实测: elapsed=" << obs.elapsedMs << "ms, code=" << obs.code << "\n";
        if (!obs.message.empty()) {
            std::cout << "    消息: " << obs.message << "\n";
        }
    }
    for (const auto& problem : problems) {
        std::cout << "    问题: " << problem << "\n";
    }
    const bool pass = problems.empty();
    std::cout << "    => " << (pass ? "PASS" : "FAIL") << " [" << title << "]\n";
    return pass;
}

void RunOrFail(const std::string& title, int expectedSecs, const std::string& expectedStage,
    const TimeoutObs& obs, bool expectHandlerError, bool handlerSawError)
{
    if (!Report(title, expectedSecs, expectedStage, obs, expectHandlerError, handlerSawError)) {
        ++g_failures;
    }
}

} // namespace

// ===========================================================================
// [1] AgentCard 发现超时：GetAgentCard(path, timeout) 形参
// ===========================================================================
static void ScenarioDiscovery(const StallStub& stub)
{
    constexpr int timeoutSecs = 2;
    std::cout << "\n[1] AgentCard 发现超时 —— GetAgentCard(path, timeout=" << timeoutSecs << "s)\n";

    auto resolver = A2A::Client::HttpCardResolverBuilder::Build(stub.BaseUrl(), "/.well-known/agent-card.json");
    if (resolver == nullptr) {
        std::cout << "    => FAIL [resolver 创建失败]\n";
        ++g_failures;
        return;
    }

    auto t0 = Clock::now();
    auto fut = resolver->GetAgentCard(std::nullopt, timeoutSecs);
    TimeoutObs obs = AwaitTimeout(fut, timeoutSecs + GUARD_MARGIN_SECS, t0);
    RunOrFail("AgentCard 发现超时", timeoutSecs, "connect-or-waiting-response-headers", obs, false, false);
}

// ===========================================================================
// [2] 非流式 per-call 超时：SendMessage timeout 形参覆盖 config
// ===========================================================================
static void ScenarioSendPerCall(const StallStub& stub)
{
    constexpr int timeoutSecs = 2;
    std::cout << "\n[2] 非流式 per-call 超时 —— SendMessage timeout=" << timeoutSecs
              << "s 覆盖 config.timeoutSeconds=300\n";

    A2A::Client::ClientConfig cfg;
    cfg.streaming = false;
    cfg.polling = false;
    cfg.supportedTransports = {"JSONRPC"};
    // 显式保持 SDK 默认大值（300s），验证 per-call 形参覆盖
    constexpr int defaultTimeoutSecs = 300;
    cfg.timeoutSeconds = defaultTimeoutSecs;
    auto client = A2A::Client::ClientFactory::Create(MakeCard(stub.BaseUrl(), false), cfg);
    if (client == nullptr) {
        std::cout << "    => FAIL [client 创建失败]\n";
        ++g_failures;
        return;
    }

    auto handlerObs = std::make_shared<HandlerObs>();
    auto t0 = Clock::now();
    auto fut = client->SendMessage(MakeMessage(), nullptr, MakeHandler(handlerObs), timeoutSecs);
    TimeoutObs obs = AwaitTimeout(fut, timeoutSecs + GUARD_MARGIN_SECS, t0);
    RunOrFail("非流式 per-call 超时", timeoutSecs, "connect-or-waiting-response-headers", obs, true,
        handlerObs->sawError);
    client->Close();
}

// ===========================================================================
// [3] 非流式 config 级超时：ClientConfig::timeoutSeconds（per-call 传 0）
// ===========================================================================
static void ScenarioSendConfig(const StallStub& stub)
{
    constexpr int timeoutSecs = 3;
    std::cout << "\n[3] 非流式 config 级超时 —— config.timeoutSeconds=" << timeoutSecs << "s, timeout=0\n";

    A2A::Client::ClientConfig cfg;
    cfg.streaming = false;
    cfg.polling = false;
    cfg.supportedTransports = {"JSONRPC"};
    cfg.timeoutSeconds = timeoutSecs;
    auto client = A2A::Client::ClientFactory::Create(MakeCard(stub.BaseUrl(), false), cfg);
    if (client == nullptr) {
        std::cout << "    => FAIL [client 创建失败]\n";
        ++g_failures;
        return;
    }

    auto handlerObs = std::make_shared<HandlerObs>();
    auto t0 = Clock::now();
    auto fut = client->SendMessage(MakeMessage(), nullptr, MakeHandler(handlerObs), 0);
    TimeoutObs obs = AwaitTimeout(fut, timeoutSecs + GUARD_MARGIN_SECS, t0);
    RunOrFail("非流式 config 级超时", timeoutSecs, "connect-or-waiting-response-headers", obs, true,
        handlerObs->sawError);
    client->Close();
}

// ===========================================================================
// [4] 流式空闲超时（config）：ClientConfig::streamingIdleTimeoutSeconds
// ===========================================================================
static void ScenarioStreamIdleConfig(const StallStub& stub)
{
    constexpr int timeoutSecs = 3;
    std::cout << "\n[4] 流式空闲超时(config) —— config.streamingIdleTimeoutSeconds=" << timeoutSecs
              << "s, timeout=0\n";

    A2A::Client::ClientConfig cfg;
    cfg.streaming = true;
    cfg.polling = false;
    cfg.supportedTransports = {"JSONRPC"};
    cfg.streamingIdleTimeoutSeconds = timeoutSecs;
    auto client = A2A::Client::ClientFactory::Create(MakeCard(stub.BaseUrl(), true), cfg);
    if (client == nullptr) {
        std::cout << "    => FAIL [client 创建失败]\n";
        ++g_failures;
        return;
    }

    auto handlerObs = std::make_shared<HandlerObs>();
    auto t0 = Clock::now();
    auto fut = client->SendMessage(MakeMessage(), nullptr, MakeHandler(handlerObs), 0);
    TimeoutObs obs = AwaitTimeout(fut, timeoutSecs + GUARD_MARGIN_SECS, t0);
    RunOrFail("流式空闲超时(config)", timeoutSecs, "stream-idle", obs, true, handlerObs->sawError);
    client->Close();
}

// ===========================================================================
// [5] 流式空闲超时（per-call）：SendMessage timeout 形参覆盖 config
// ===========================================================================
static void ScenarioStreamIdlePerCall(const StallStub& stub)
{
    constexpr int timeoutSecs = 2;
    std::cout << "\n[5] 流式空闲超时(per-call) —— SendMessage timeout=" << timeoutSecs
              << "s 覆盖 config.streamingIdleTimeoutSeconds=60\n";

    A2A::Client::ClientConfig cfg;
    cfg.streaming = true;
    cfg.polling = false;
    cfg.supportedTransports = {"JSONRPC"};
    // 显式保持 SDK 默认大值（60s），验证 per-call 形参覆盖
    constexpr int defaultIdleTimeoutSecs = 60;
    cfg.streamingIdleTimeoutSeconds = defaultIdleTimeoutSecs;
    auto client = A2A::Client::ClientFactory::Create(MakeCard(stub.BaseUrl(), true), cfg);
    if (client == nullptr) {
        std::cout << "    => FAIL [client 创建失败]\n";
        ++g_failures;
        return;
    }

    auto handlerObs = std::make_shared<HandlerObs>();
    auto t0 = Clock::now();
    auto fut = client->SendMessage(MakeMessage(), nullptr, MakeHandler(handlerObs), timeoutSecs);
    TimeoutObs obs = AwaitTimeout(fut, timeoutSecs + GUARD_MARGIN_SECS, t0);
    RunOrFail("流式空闲超时(per-call)", timeoutSecs, "stream-idle", obs, true, handlerObs->sawError);
    client->Close();
}

int main()
{
    // 打印 SDK 内部日志（OnIdleCheck 的 WARN、SendError 的 ERROR）作为超时生效的佐证
    A2A::Log::SetLogLevel(A2A::Log::A2A_LOG_LEVEL::INFO);

    std::cout << "A2A C++ SDK 超时功能打桩验证\n";

    StallStub stub;
    if (!stub.Start()) {
        std::cerr << "打桩服务启动失败\n";
        return 1;
    }
    std::cout << "打桩服务: " << stub.BaseUrl() << "（只 accept、永不回包）\n";

    ScenarioDiscovery(stub);
    ScenarioSendPerCall(stub);
    ScenarioSendConfig(stub);
    ScenarioStreamIdleConfig(stub);
    ScenarioStreamIdlePerCall(stub);

    stub.Stop();

    constexpr int totalScenarios = 5;
    std::cout << "\n======== 超时验证结果: " << (totalScenarios - g_failures) << "/" << totalScenarios
              << " 通过 ========\n";
    return g_failures;
}
