#pragma once

#include <string>
#include <vector>

struct ChatHistoryMessage {
    std::string role;
    std::string content;
};

namespace ChatHistory {

void AddMessage(const char* role, const char* content);
std::vector<ChatHistoryMessage> GetMessages();
void Clear();

}  // namespace ChatHistory
