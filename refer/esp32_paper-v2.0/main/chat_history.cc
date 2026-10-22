#include "chat_history.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace {

std::mutex g_chat_history_mutex;
std::vector<ChatHistoryMessage> g_chat_messages;
constexpr size_t kMaxChatMessages = 40;

std::string Trim(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }

    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }
    return input.substr(start, end - start);
}

}  // namespace

namespace ChatHistory {

void AddMessage(const char* role, const char* content) {
    if (role == nullptr || content == nullptr) {
        return;
    }

    std::string role_str(role);
    if (role_str != "user" && role_str != "assistant") {
        return;
    }

    std::string content_str = Trim(content);
    if (content_str.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_chat_history_mutex);
    g_chat_messages.push_back({role_str, content_str});
    if (g_chat_messages.size() > kMaxChatMessages) {
        g_chat_messages.erase(g_chat_messages.begin(),
                              g_chat_messages.begin() + (g_chat_messages.size() - kMaxChatMessages));
    }
}

std::vector<ChatHistoryMessage> GetMessages() {
    std::lock_guard<std::mutex> lock(g_chat_history_mutex);
    return g_chat_messages;
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_chat_history_mutex);
    g_chat_messages.clear();
}

}  // namespace ChatHistory
