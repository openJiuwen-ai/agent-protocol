/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_COMMON_TYPES_H
#define A2A_COMMON_TYPES_H

#include <string>

namespace A2A {

constexpr const char* METHOD_MESSAGE_SEND = "message/send";
constexpr const char* METHOD_MESSAGE_STREAM = "message/stream";
constexpr const char* METHOD_TASK_GET = "tasks/get";
constexpr const char* METHOD_TASK_CANCEL = "tasks/cancel";
constexpr const char* METHOD_TASK_RESUBSCRIBE = "tasks/resubscribe";
constexpr const char* METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET = "tasks/pushNotificationConfig/set";
constexpr const char* METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET = "tasks/pushNotificationConfig/get";
constexpr const char* METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST = "tasks/pushNotificationConfig/list";
constexpr const char* METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE = "tasks/pushNotificationConfig/delete";

/*
 * METHOD_AGENT_CARD_GET is just to identify get agent card request.
 * It is not standard A2A protocol field and will not be used in request payload
*/
constexpr const char* METHOD_AGENT_CARD_GET = "agentCard/get";

constexpr const char* JSON_VERSION = "2.0";
constexpr const char* JSON_FIELD_RESULT = "result";
constexpr const char* JSON_FIELD_KIND = "kind";
constexpr const char* JSON_FIELD_JSONRPC = "jsonrpc";
constexpr const char* JSON_FIELD_ID = "id";
constexpr const char* JSON_FIELD_METHOD = "method";
constexpr const char* JSON_FIELD_PARAMS = "params";

constexpr const char* STREAM_RESPONSE_TYPE_TASK = "task";
constexpr const char* STREAM_RESPONSE_TYPE_STATUS_UPDATE = "status-update";
constexpr const char* STREAM_RESPONSE_TYPE_ARTIFACT_UPDATE = "artifact-update";

struct UserData {
    uint64_t requestId = 0;
    std::string method;
};

}

#endif