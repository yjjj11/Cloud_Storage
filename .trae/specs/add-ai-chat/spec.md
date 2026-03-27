# AI Chat Feature Spec

## Why
用户希望在系统前端的主页面上增加一个小型的 AI 聊天框，方便直接与 AI（基于 Qwen 模型）进行交互。为了提供良好的用户体验，AI 的回复需要像打字机一样流式显示在前端。

## What Changes
- 重构 `qwen_stream_client.cpp`：将其中的 API 请求与流式接收逻辑封装到一个可复用的头文件 `qwen_stream_client.hpp` 中，提供一个 `run(prompt, callback)` 的接口。
- 更新 `storage_router.hpp`：新增一个处理聊天的 HTTP 路由接口 `/api/chat`。该接口将接收前端的提问，调用 `QwenClient::run` 并在回调中将接收到的文本分块（Chunked / SSE）流式地发送回前端。
- 更新前端 `include/resource/index.html`：增加聊天框 UI，并通过 JavaScript (`EventSource` 或 Fetch API 的 `getReader()`) 接入 `/api/chat`，实现流式响应的解析与展示。

## Impact
- Affected specs: 无
- Affected code:
  - `include/resource/index.html`
  - `include/service/storage_router.hpp`
  - `include/service/qwen_stream_client.hpp` (新增)
  - `qwen_stream_client.cpp` (如果不再作为独立测试程序，可删除或重构)

## ADDED Requirements
### Requirement: AI Chat UI and Stream Integration
The system SHALL provide an AI chat interface on the main page.

#### Scenario: Success case
- **WHEN** user inputs a message and clicks send
- **THEN** the message is displayed, and the AI's response streams into the chat box incrementally.
