/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_COMMON_TYPES_H
#define A2A_COMMON_TYPES_H

#include <string>
#include <variant>

#include "types.h"

namespace A2A {

constexpr const char* METHOD_MESSAGE_SEND = "SendMessage";
constexpr const char* METHOD_MESSAGE_STREAM = "SendStreamingMessage";
constexpr const char* METHOD_TASK_GET = "GetTask";
constexpr const char* METHOD_TASK_CANCEL = "CancelTask";
constexpr const char* METHOD_TASK_RESUBSCRIBE = "SubscribeToTask";
constexpr const char* METHOD_TASK_PUSH_NOTIFICATION_CONFIG_SET = "CreateTaskPushNotificationConfig";
constexpr const char* METHOD_TASK_PUSH_NOTIFICATION_CONFIG_GET = "GetTaskPushNotificationConfig";
constexpr const char* METHOD_TASK_PUSH_NOTIFICATION_CONFIG_LIST = "ListTaskPushNotificationConfigs";
constexpr const char* METHOD_TASK_PUSH_NOTIFICATION_CONFIG_DELETE = "DeleteTaskPushNotificationConfig";

/*
 * METHOD_AGENT_CARD_GET is just to identify get agent card request.
 * It is not standard A2A protocol field and will not be used in request payload
*/
constexpr const char* METHOD_AGENT_CARD_GET = "GetAgentCard";

constexpr const char* JSON_VERSION = "2.0";
constexpr const char* JSON_FIELD_RESULT = "result";
constexpr const char* JSON_FIELD_JSONRPC = "jsonrpc";
constexpr const char* JSON_FIELD_ID = "id";
constexpr const char* JSON_FIELD_METHOD = "method";
constexpr const char* JSON_FIELD_PARAMS = "params";
constexpr const char* JSON_FIELD_ERROR = "error";
constexpr const char* JSON_FIELD_MESSAGE = "message";
constexpr const char* JSON_FIELD_METADATA = "metadata";

constexpr const char* STREAM_RESPONSE_TYPE_TASK = "task";
constexpr const char* STREAM_RESPONSE_TYPE_STATUS_UPDATE = "status-update";
constexpr const char* STREAM_RESPONSE_TYPE_ARTIFACT_UPDATE = "artifact-update";

constexpr const char* DEFAULT_PROTOCOL_VERSION = "1.0";

constexpr size_t DEFAULT_MPSC_QUEUE_SIZE = 1024;
constexpr size_t HTTP_QUEUE_MAX_BATCH_SIZE = 64;

constexpr int HTTPS_READ_BUFFER_SIZE = 4096;
constexpr int HTTP_LISTEN_BACKLOG = 128;

struct UserData {
    std::string requestId = "";
    std::string method;
    bool isStream = false;
    bool valid = true;
    int timerId = -1;
    int timeout = 0;
};

struct BaseResponse {
    std::optional<std::string> id;
    std::string jsonrpc = JSON_VERSION;
};

struct SendMessageSuccessResponse : public BaseResponse {
    std::variant<Task, Message> result;
};

struct SendStreamingMessageSuccessResponse : public BaseResponse {
    std::variant<Task, Message, TaskArtifactUpdateEvent, TaskStatusUpdateEvent> result;
};

struct GetTaskSuccessResponse : public BaseResponse {
    Task result;
};

struct CancelTaskSuccessResponse : public BaseResponse {
    Task result;
};

struct SetTaskPushNotificationConfigSuccessResponse : public BaseResponse {
    TaskPushNotificationConfig result;
};

struct GetTaskPushNotificationConfigSuccessResponse : public BaseResponse {
    TaskPushNotificationConfig result;
};

struct ListTaskPushNotificationConfigSuccessResponse : public BaseResponse {
    std::vector<TaskPushNotificationConfig> result;
};

struct GetAgentCardSuccessResponse : public BaseResponse {
    AgentCard result;
};

}

#endif