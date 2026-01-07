/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <cstdio>

#include "gtest/gtest.h"
#include "mcp_client.h"
#include "mcp_log.h"
#include "mcp_server.h"
#include "mcp_type.h"

const char* const ECHO_TOOL_NAME = "echo";
const char* const ECHO_TOOL_TITLE = "Echo Tool";
const char* const ECHO_TOOL_DESCRIPTION = "Echoes back the input message";
static constexpr int TEST_TIMEOUT_MS = 5000;
static constexpr int TEST_WAIT_TIME_MS = 500;

const char* const CA_CERT_PEM = R"(-----BEGIN CERTIFICATE-----
MIID0zCCArugAwIBAgIUCmvretZW6g0b7HijNnotsgmAb0wwDQYJKoZIhvcNAQEL
BQAweTELMAkGA1UEBhMCY24xCzAJBgNVBAgMAnpqMQswCQYDVQQHDAJoejELMAkG
A1UECgwCdHQxCzAJBgNVBAsMAnR0MRQwEgYDVQQDDAtNQ1AgVGVzdCBDQTEgMB4G
CSqGSIb3DQEJARYRMjk0MjUxMjQxN0BxcS5jb20wHhcNMjUxMTI2MDcyNzE0WhcN
MzUxMTI0MDcyNzE0WjB5MQswCQYDVQQGEwJjbjELMAkGA1UECAwCemoxCzAJBgNV
BAcMAmh6MQswCQYDVQQKDAJ0dDELMAkGA1UECwwCdHQxFDASBgNVBAMMC01DUCBU
ZXN0IENBMSAwHgYJKoZIhvcNAQkBFhEyOTQyNTEyNDE3QHFxLmNvbTCCASIwDQYJ
KoZIhvcNAQEBBQADggEPADCCAQoCggEBANguNmN0kVvgyIp4ms3XThdDd1M/sZEc
wPzrzz9FVhf5NjYocZPDX4QHOCcSaxB1medoz1vDnMPuK00NSPm7e3xWuNHJ6dBP
udlRwiwM3f905eWEO5A6fc+Z7uzQpq4H5xuujJOduVVEkRYaXWNqC88mhHOnaOv2
6wyf3KqE7T3yLS9RE03OQdDX/HXICc+YsfMqz74qNp+SWvqIr6lEeyARN3niQgec
BQRHIMw1XGwbiusVQzaQaUm4h5lxXRGJqPHfUbeINChKFd+zWjvXjeg48RoJOkXP
EWeXCjCJWg5SfYMJKDMqggLuKboS8tDFDIHF4dR8pffA0pczd7KoJ60CAwEAAaNT
MFEwHQYDVR0OBBYEFKgOYipEmIgiemhoXWfGNsh8ZJEJMB8GA1UdIwQYMBaAFKgO
YipEmIgiemhoXWfGNsh8ZJEJMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEL
BQADggEBAD5tc2addys5We0lpA3efIULMbGc5qZNfTD1VinkylhUMBK9SP/tf/4Z
cOX0eGgz7a5iWeIjN52dfCi3vPUP7862G7B3sjIirCNKM/i1pDE/kPnQUEz3sv1Y
GvWDOnKJmckHPVLKe2Jm5vgfIe4YSCWlnSVyERebIZT97sVec3vjYWGHEQ6FnMh3
oZumD5FqtWr+EDsSDtyHEmZvu5mmWwKu9TktP8J1wvNIuNBYueNEFWEQYsEE7nf/
ysmTEpY/R0DH1W48qxBj8zoVk/piHHhFl67xKiB44NwWgK4JNj6bG2bOri/2RUO4
TCPZF2aLZCikc255XlpzXBPPJbrxGUQ=
-----END CERTIFICATE-----)";

const char* const SERVER_CERT_PEM = R"(-----BEGIN CERTIFICATE-----
MIIDwTCCAqmgAwIBAgIUfmr8vCnDIrRN9hrxlnGWMTLko4AwDQYJKoZIhvcNAQEL
BQAweTELMAkGA1UEBhMCY24xCzAJBgNVBAgMAnpqMQswCQYDVQQHDAJoejELMAkG
A1UECgwCdHQxCzAJBgNVBAsMAnR0MRQwEgYDVQQDDAtNQ1AgVGVzdCBDQTEgMB4G
CSqGSIb3DQEJARYRMjk0MjUxMjQxN0BxcS5jb20wHhcNMjUxMTI2MDczNzM0WhcN
MjYxMTI2MDczNzM0WjBcMQswCQYDVQQGEwJDTjENMAsGA1UECAwEVGVzdDENMAsG
A1UEBwwEVGVzdDEMMAoGA1UECgwDTUNQMQwwCgYDVQQLDANEZXYxEzARBgNVBAMM
Cm1jcC1zZXJ2ZXIwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCkLtY0
ahDgNjGcKoSY5QQUzPcLILlSdA9SMcoYtcKabFPwhZxhWFsjLBDa91CT0PYqLx4D
fiF9fqVtQpxZpVxg7rQ8prIhjoePBgwnUZjJzZo3GjTaLRWdqZN0ENNQ9vWTQIjn
TeIbsOHsCHNToXIkss9wLrmtuAYBl4lU18eDsV3ygH+IyCaj/HRPBFkryZh3InUP
bAHPZAJEqvLVky5DRIZGXVoQop7GkfdlRthH8pClvBpRGM3RIxpJpFF1osjrFOgf
lyu/y68I+Wr7P/7hZa3v4XEBDZhl8X2EltuOh55NGyXWNDrm0tZQ20EnThbX/nWA
X4eCZjS5FYcrojQ9AgMBAAGjXjBcMBoGA1UdEQQTMBGCCWxvY2FsaG9zdIcEfwAA
ATAdBgNVHQ4EFgQUIVeX1ikIODzwXI1InGk3rPWzivQwHwYDVR0jBBgwFoAUqA5i
KkSYiCJ6aGhdZ8Y2yHxkkQkwDQYJKoZIhvcNAQELBQADggEBAMwF/rsW5BoRcHkR
+fDH0pa35mdkMJb1M29D3Q4Lzh+D5j9tbG+f1osbeNgoDLUxAQ7b6FeAuiAdDOOZ
qQYuNWjybfwHr5txLt46unrFRM4c+gaGF4+aF/K8n2V3p7CLNDUhIYQlBsJd7EER
FZtYgEIlDqWa8LZGDn7xqn5VPBqAFAecvqkftmTgujVe5of3FKd1ztuvIGAWmy7N
cqBCr+7d8iUx4WdpEmXMr70MYBLikTpun9W9dqmdVUpyEYyD0gmu2mCN9FC1G3xx
DAlB1biGEm4nseL4a9dfeEx+uQiTZLzRnXVRkrayUSw/a0Z+uCSiidhwGHyeAHqg
kFoYg50=
-----END CERTIFICATE-----)";

const char* const SERVER_KEY_PEM = R"(-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCkLtY0ahDgNjGc
KoSY5QQUzPcLILlSdA9SMcoYtcKabFPwhZxhWFsjLBDa91CT0PYqLx4DfiF9fqVt
QpxZpVxg7rQ8prIhjoePBgwnUZjJzZo3GjTaLRWdqZN0ENNQ9vWTQIjnTeIbsOHs
CHNToXIkss9wLrmtuAYBl4lU18eDsV3ygH+IyCaj/HRPBFkryZh3InUPbAHPZAJE
qvLVky5DRIZGXVoQop7GkfdlRthH8pClvBpRGM3RIxpJpFF1osjrFOgflyu/y68I
+Wr7P/7hZa3v4XEBDZhl8X2EltuOh55NGyXWNDrm0tZQ20EnThbX/nWAX4eCZjS5
FYcrojQ9AgMBAAECggEAEmxXCQal4iLyqi1zN5zrOlJNnmgLo572YSjJHsC2l5F0
UdcMVzgM8gNt2MRLvGqEVPN4kLfElkpx0bLZzI1rskzW2L9z2aQevgPVtUvIs0xo
86nzKFBKzqhKrYl9zOYJZH6nXh01NBpDH7NLDaVeI5QVBueXWlD+C4uvk1+rObGt
THlSdvqTdtFxhsFwIOPCVBmZg7bLD0PG2ANiJmMCRnW3wMJelgw7Xe0TR8nYx+8Q
E59/dY5rZcdMoyTDDu4anVw6qu8JTk+5MbTtlyg3Eo3U68r5ypRESfxGlMMKFETC
j38t6lFy1h4bMnHWwhP51IYE/jht7HtTqRayUKQIgQKBgQDdqc/EjEW2e9dDIIXN
68Jbs3HdNeXFuDRW42nUMnxzE0bv2DaxjqztWgBIi0wRPufAR7cHbBqbFWmuDyG6
e01BsMkklC6Ye97RYYqvbfytyZLp6QX2Ro3fCABk+q84Nc6777/eTDDtOKv2qxyT
SajqlgEoa3j6Dt0vSi+VQhrrcQKBgQC9nZuIXFe+kn7+EnkDViwGZQGlhP4Yyktg
DmV06B0LM5x1ImXl1IC/nxlnY6cScYzeDxVT4n2kju7gwqRZcVjizJyp4hkg1Fri
wZstCFyDDgt0z19JzLzkPs0U4dnBB5VZIX2foN79famCID5S8UDH/BgfVOc+Zg3+
hrJOZK13jQKBgQCNRFbkdV1Mclp7LnP4C4Ofe5pal6nrO30nzuE4WkCT+jogSxR4
TmyJC+3YTeZZq1R5Z3sPIOdH5Pqn8n7VYRWYgvl99W5IwdD8cgS3zwnVG/0kU9z3
2nk6JdhSgq0+zpQFlEMcgkRAOvjlSKH6gFxFvPnZ77x26RgvtkDjB/EuEQKBgQCU
cFK3AFtshDDIY9u6YfZbFy2fzypAa/VwADUqvR8nisLb7A7LSn10BXAUwS2kwaHe
oP44SiHD9mVJLPcOUi6cdkPatnanfbact6XsLhSGQDN1wXW+R3pEUGmqBFi8xVwE
h7RZh82psntFtEo2Ekl9hXu443WYIrqn2/iwaB5obQKBgALn/UC6vIKWYX1HAEJ3
LKdumM/dBl2vPYkk8PxxvkeWA9AUSfTaISeVnVdkeBTf5PhINXm/aAVttoDC6tdZ
CwGjHxnByfxiL6gI1l2MAI3w7KDRyKiR23Rlfk/6J58b5qDbQvDS5AJwr2UM5SHm
Fffx+AdygF/f9nG0N1BHC/Xd
-----END PRIVATE KEY-----)";

const char* const CLIENT_CERT_PEM = R"(-----BEGIN CERTIFICATE-----
MIIDeDCCAmACFH5q/LwpwyK0TfYa8ZZxljEy5KN/MA0GCSqGSIb3DQEBCwUAMHkx
CzAJBgNVBAYTAmNuMQswCQYDVQQIDAJ6ajELMAkGA1UEBwwCaHoxCzAJBgNVBAoM
AnR0MQswCQYDVQQLDAJ0dDEUMBIGA1UEAwwLTUNQIFRlc3QgQ0ExIDAeBgkqhkiG
9w0BCQEWETI5NDI1MTI0MTdAcXEuY29tMB4XDTI1MTEyNjA3MjkyMFoXDTI2MTEy
NjA3MjkyMFoweDELMAkGA1UEBhMCY24xCzAJBgNVBAgMAnpqMQswCQYDVQQHDAJo
ejELMAkGA1UECgwCdHQxCzAJBgNVBAsMAnR0MRMwEQYDVQQDDAptY3AtY2xpZW50
MSAwHgYJKoZIhvcNAQkBFhEyOTQyNTEyNDE3QHFxLmNvbTCCASIwDQYJKoZIhvcN
AQEBBQADggEPADCCAQoCggEBAK3N1mBg1UJAI2V41q1Cm09p9+/7iAfRaNXBTqvV
6gcDwVgrf1RIcVZyXbvWK0JEonZJVsYFh4pN9COrN3SWSLKkEzHk9weWA+HWfYas
xDyxPOBMH/wfNDGsj2Z7+VaWoS9kffb6+w1E4YEct7euCzKH6Xm1g2JuX0aEW1Ol
u+vmNC4WPX3fWH/jelHk/lMYhP4Kxcafgf3cZL1Ul/rMoCNV5EEdk8grB5hw9hk7
BjO/345WQXaZ4w/6QZ2A4kAUbwrLgW1aX4AJ9BeEv1tjLK06LJ+EGIECRWVXj45d
J34LvwVkqVCkFKsxZUhNnCW/p5HfhTUEVFCiSd37Sf9jYCECAwEAATANBgkqhkiG
9w0BAQsFAAOCAQEAoXSA8RMjtSNpMRNhDkTEQpZ8kJvJBsjsDkCMvk7vyaJXoVIN
lmWfRZ9+0vY7+Q/4dqE94QZi2EPHMHarku0L9RogHkz5SZKAj69rk31S+DWUX0RL
PJGacAc9qm7g+F9UeuLY99gNvag2HF0b6GNkdE7kC9qRy5vhbOvAkPBxa+HMFnUD
RICvAyKXBi5UGcUM2dqg3T1zTPI+ckkVxe+LiGjlM8bQc0ovCssaLFqZPrke+Rfn
DjRXAMc0il+RCaa6ggyfBQ3swAqF/2rvqXyORGpBZQRrIhsYSlrRcAnWFk1281W8
M9tto/lRCLSkxOq3IDEsXIzL59ZVEAiICZ2s2w==
-----END CERTIFICATE-----)";

const char* const CLIENT_KEY_PEM = R"(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCtzdZgYNVCQCNl
eNatQptPaffv+4gH0WjVwU6r1eoHA8FYK39USHFWcl271itCRKJ2SVbGBYeKTfQj
qzd0lkiypBMx5PcHlgPh1n2GrMQ8sTzgTB/8HzQxrI9me/lWlqEvZH32+vsNROGB
HLe3rgsyh+l5tYNibl9GhFtTpbvr5jQuFj1931h/43pR5P5TGIT+CsXGn4H93GS9
VJf6zKAjVeRBHZPIKweYcPYZOwYzv9+OVkF2meMP+kGdgOJAFG8Ky4FtWl+ACfQX
hL9bYyytOiyfhBiBAkVlV4+OXSd+C78FZKlQpBSrMWVITZwlv6eR34U1BFRQoknd
+0n/Y2AhAgMBAAECggEAMlnRYzZqUbkXCAyL1MHZEJaYd3OdIvatsk9AJVPhaorR
9mMTji02eF7FfOc8vQP5NN9mPhPYAl82/SZa+HGZOfeCgA/e6Nmy4jgrQVrHtvV5
t1kWYwn/Kuzc1knQIB+fLoWkYGqxVFTCJ49UQegv4kGxh8rn9xoWi+1IL+FxoZMi
DCm5TtLWR+5h9WXu7HRrjeiIt9RYOMb2aJa6+JucOpFPUXKiO+9V8ld+wgRbP6K6
UEX8dkN4OOyib0yUIZNTBE1K6Crc5kzB618AXLGdJHypRF1qtlXijdTaXRXDhktX
lyBy6iY6A7FsCIFDHxcmVC4pIsXrH312DEHS+RnpSQKBgQDv34Vgv4waAczW3rLt
tK2Tm9CG1GzuLwtlyZpNgD46aLee7NE55uWXXG1cp/EVyfsr2DtVwyJSTQafmvgf
YRfYgiYqICrji5hrp2kVLQNZpBt/GGQO55qdHfVkwRlcd0pIqxCkgz3XrQqAL73I
dYH8izEmwLAc45VoYF3qrxuPowKBgQC5fTH/qJ1Vzw/l7VvOTRfpIJaL2gws4sGU
5V2jLtMLcLUPRtedPIjAD9KBEhs94+BYFxNHcLJ0k1Dk6qCpcFjxXU4qpqp3psFo
LvFbVULq7V+JkREcLtlrjWzlxDqkmOgN0k4O9f8j6hGl/lvybSSK3PuxeVSmPgnd
4YDMJBG9awKBgCjbnuFi3x9S7SwwVLPukZ5R7Qv6RO+xLbTRySmoTXEvgw9b8St2
q+bDRJGCvarjc5f9ReYOzdrM6fLnt5nItQIY8R09y3Bkx7qWkmyb9TUgodpdMjXj
t09J2hGmXoaEfy+vi68p96+z9tTszNvHQDPfFakvKxBQp5NWzsh5uS0XAoGAZE74
5+nWU94rGctXd2QnhxHxd+XN6cQirZOwJJwg0zH7KCzGUyi2Ry8HRnd1Ne3yl5C7
A0pFp1c3SuQ7SAYSg0xTBiulShlqxB2Gtj6Qdp0xqiJCH3ySZWBQwJxynip2a2sM
fXi4pkJhkuPP83TkaxgAIMSda11nVo8paLwkv8sCgYEAlBwqNol4JvANAZbS/kJn
eecy0zJiK8LekjebEBD3kOM0bLI6DAT6UCyDmNLF9jecPUEoU1IQXrrecX/BD9N2
jp6/7O3tAy6P4r7iFhM5CnPl9iHHi1h+/JwQCw92VewMvrg3KOYX2RudOCH4YyGw
7w9JsbnTU2VpLK8Huvs02wY=
-----END PRIVATE KEY-----)";


bool WriteStringToFile(const std::string& filename, const char* const str)
{
    if (!str) {
        std::cerr << "错误：传入的字符串为空指针" << std::endl;
        return false;
    }

    FILE* file = fopen(filename.c_str(), "w");
    if (!file) {
        std::cerr << "错误：无法创建或打开文件 " << filename << std::endl;
        return false;
    }

    int result = fprintf(file, "%s", str);
    fclose(file);

    if (result < 0) {
        std::cerr << "错误：写入文件时发生错误" << std::endl;
        return false;
    }

    return true;
}


namespace Mcp {
namespace Test {

class TlsTest : public ::testing::Test {
public:
    ~TlsTest() {}
protected:
    static constexpr uint16_t oneWayTlsPort = 8443;
    static constexpr uint16_t mutualTlsPort = 8444;

    static volatile sig_atomic_t gStop;

    static std::string host;

    // 用于异步服务器线程的变量
    std::unique_ptr<McpServer> server;
    std::thread serverThread;
    bool serverRunning = false;

    static void SignalHandler(int sig)
    {
        if (sig == SIGINT || sig == SIGTERM) {
            gStop = 1;
        }
    }

    void SetUp() override
    {
        // 初始化日志
        SetLogLevel(MCP_LOG_LEVEL_INFO);

        // 设置信号处理器
        std::signal(SIGINT, SignalHandler);
        std::signal(SIGTERM, SignalHandler);

        host = "127.0.0.1";
        gStop = 0;
    }

    void TearDown() override
    {
        StopServer();
        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_TIME_MS));
    }

    // 辅助函数：写入证书文件
    void WriteCertificateFiles()
    {
        WriteStringToFile("ca.cert.pem", CA_CERT_PEM);
        WriteStringToFile("server.cert.pem", SERVER_CERT_PEM);
        WriteStringToFile("server.key.pem", SERVER_KEY_PEM);
        WriteStringToFile("client.cert.pem", CLIENT_CERT_PEM);
        WriteStringToFile("client.key.pem", CLIENT_KEY_PEM);
    }

    // 辅助函数：创建服务器配置
    StreamableHttpServerConfig CreateHttpServerConfig(uint16_t port, bool mutualTls)
    {
        StreamableHttpServerConfig httpConfig;
        httpConfig.ioThreads = 1;
        httpConfig.isJsonResponseEnabled = true;
        httpConfig.endpoint = "https://" + host + ":" + std::to_string(port) + "/mcp";
        httpConfig.tlsConfig.enabled = true;

        httpConfig.tlsConfig.certFile = "./server.cert.pem";
        httpConfig.tlsConfig.keyFile = "./server.key.pem";

        if (mutualTls) {
            httpConfig.tlsConfig.caFile = "./ca.cert.pem";
            httpConfig.tlsConfig.verifyPeer = true;
        } else {
            httpConfig.tlsConfig.verifyPeer = false;
        }

        return httpConfig;
    }

    // 辅助函数：创建服务器基本配置
    ServerConfig CreateServerConfig()
    {
        ServerConfig serverConfig;
        serverConfig.name = "TlsDemoServer";
        serverConfig.version = "1.0.0";
        serverConfig.workerThreads = 1;
        return serverConfig;
    }

    // 辅助函数：创建echo工具的回调函数
    std::function<Mcp::CallToolResult(const std::string&, const Mcp::JsonValue&, const std::optional<Mcp::JsonValue>&)>
    CreateEchoToolFunction()
    {
        return [](const std::string &name, const Mcp::JsonValue &arguments,
                  const std::optional<Mcp::JsonValue> &ctx) -> Mcp::CallToolResult {
            Mcp::CallToolResult result;
            result.isError = false;
            try {
                std::string userQuery = "";
                if (arguments.contains("user_query") && arguments.at("user_query").is_string()) {
                    userQuery = arguments.at("user_query").get<std::string>();
                }
                Mcp::TextContent textContent;
                textContent.text = "Echo: " + userQuery;
                result.content.push_back(textContent);
            } catch (const std::exception &e) {
                result.isError = true;
                Mcp::TextContent errorContent;
                errorContent.text = std::string("Error: ") + e.what();
                result.content.push_back(errorContent);
            }
            return result;
        };
    }

    // 辅助函数：添加echo工具到服务器
    void AddEchoToolToServer()
    {
        auto echoFunc = CreateEchoToolFunction();
        std::string echoInputSchema = R"({"type": "object", "properties": {"user_query": {"type": "string",
            "description": "The user query."}}, "required": ["user_query"]})";
        std::string echoOutputSchema = R"({"type": "object", "properties": {"result": {"type": "string",
            "description": "The echoed message"}}})";
        std::string echoTitle = ECHO_TOOL_TITLE;
        std::string echoDescription = ECHO_TOOL_DESCRIPTION;

        try {
            Mcp::AddToolOptionalParams toolParams;
            toolParams.title = std::cref(echoTitle);
            toolParams.description = std::cref(echoDescription);
            toolParams.inputSchema = std::cref(echoInputSchema);
            toolParams.outputSchema = std::cref(echoOutputSchema);
            server->AddTool(ECHO_TOOL_NAME, echoFunc, toolParams);
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add tool success: %s", ECHO_TOOL_NAME);
        } catch (const std::exception &e) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "add tool failed: %s", e.what());
        } catch (...) {
            MCP_LOG(MCP_LOG_LEVEL_INFO, "add tool failed as expected");
        }
    }

    // 辅助函数：在独立线程中启动服务器
    void StartServerThread()
    {
        serverRunning = true;
        serverThread = std::thread([this]() {
            server->Run();
            serverRunning = false;
        });

        // 等待服务器启动
        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_TIME_MS));
    }

    // 辅助函数：等待服务器完全停止
    void WaitForServerShutdown(int maxWaitMs = TEST_TIMEOUT_MS)
    {
        auto start = std::chrono::steady_clock::now();
        while (serverRunning &&
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start).count() < maxWaitMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_TIME_MS));
        }
    }

    // 启动服务器的辅助函数 - 与server_example.cpp中的逻辑相同
    bool StartServer(uint16_t port, bool mutualTls = false)
    {
        try {
            // 步骤1: 写入证书文件
            WriteCertificateFiles();

            // 步骤2: 创建服务器配置
            ServerConfig serverConfig = CreateServerConfig();
            StreamableHttpServerConfig httpConfig = CreateHttpServerConfig(port, mutualTls);

            // 步骤3: 创建服务器实例
            server = McpServerFactory::CreateStreamableHttpServer(serverConfig, httpConfig);
            if (!server) {
                return false;
            }

            // 步骤4: 添加echo工具
            AddEchoToolToServer();

            // 步骤5: 在独立线程中启动服务器
            StartServerThread();

            // 步骤6: 检查服务器是否正在运行
            return server->IsRunning();
        } catch (const std::exception& e) {
            std::cerr << "Failed to start server: " << e.what() << std::endl;
            serverRunning = false;
            return false;
        }
    }

    // 停止服务器的辅助函数
    void StopServer()
    {
        if (server) {
            server->Stop();
            serverRunning = false;
        }

        if (serverThread.joinable()) {
            serverThread.join();
        }

        server.reset();
    }

    // 创建客户端并执行测试的辅助函数 - 与client_example.cpp中的逻辑相同
    bool TestClientConnection(uint16_t port, bool mutualTls = false)
    {
        try {
            ClientConfig clientConfig;
            clientConfig.name = "TlsDemoClient";
            clientConfig.version = "1.0.0";

            StreamableHttpClientConfig transportConfig;
            transportConfig.endpoint = "https://" + host + ":" + std::to_string(port) + "/mcp";
            transportConfig.tlsConfig.verifyPeer = true;

            transportConfig.tlsConfig.caFile = "./ca.cert.pem";
            if (mutualTls) {
                transportConfig.tlsConfig.certFile = "./client.cert.pem";
                transportConfig.tlsConfig.keyFile = "./client.key.pem";
            }

            auto client = McpClientFactory::CreateStreamableHttpClient(clientConfig, transportConfig);
            if (!client) {
                std::cerr << "Failed to create client" << std::endl;
                return false;
            }

            // 初始化握手
            auto initFuture = client->Initialize();
            auto initResult = initFuture.get();
            if (!initResult) {
                std::cerr << "Initialize returned null" << std::endl;
                return false;
            }
            std::cout << "Initialized. protocolVersion=" << initResult->protocolVersion << std::endl;

            // 调用echo工具
            JsonValue args = JsonValue::object();
            args["text"] = "hello tls";
            auto callFuture = client->CallTool("echo", args);
            auto callResult = callFuture.get();
            if (!callResult) {
                std::cerr << "CallTool returned null" << std::endl;
                return false;
            }

            std::cout << "CallTool done. isError=" << (callResult->isError ? "true" : "false") << std::endl;
            return !callResult->isError;
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
            return false;
        }
    }
};

// 静态成员初始化
volatile sig_atomic_t TlsTest::gStop = 0;
std::string TlsTest::host = "127.0.0.1";

// 测试用例：测试单向TLS连接 - 对应server_example.cpp中的单向TLS模式
TEST_F(TlsTest, TestOneWayTlsConnection)
{
    std::cout << "Testing one-way TLS connection..." << std::endl;
    std::cout << "Port: " << oneWayTlsPort << std::endl;

    // 启动单向TLS服务器
    ASSERT_TRUE(StartServer(oneWayTlsPort, false)) << "Failed to start one-way TLS server";

    // 等待服务器完全启动
    std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_TIME_MS));

    // 创建客户端并测试连接
    bool success = TestClientConnection(oneWayTlsPort, false);

    // 停止服务器
    StopServer();
    WaitForServerShutdown();

    EXPECT_TRUE(success) << "One-way TLS client connection failed";
}

// 测试用例：测试双向TLS连接 - 对应server_example.cpp中的双向TLS模式
TEST_F(TlsTest, TestMutualTlsConnection)
{
    std::cout << "Testing mutual TLS connection..." << std::endl;
    std::cout << "Port: " << mutualTlsPort << std::endl;

    // 启动双向TLS服务器
    ASSERT_TRUE(StartServer(mutualTlsPort, true)) << "Failed to start mutual TLS server";

    // 等待服务器完全启动
    std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_TIME_MS));

    // 创建客户端并测试连接
    bool success = TestClientConnection(mutualTlsPort, true);

    // 停止服务器
    StopServer();
    WaitForServerShutdown();

    EXPECT_TRUE(success) << "Mutual TLS client connection failed";
}

} // namespace Test
} // namespace Mcp