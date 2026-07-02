#include "chat_frame.h"

#include "imgui.h"

namespace wowclient {

namespace {

ImVec4 chatColor(const std::string& channel) {
    if (channel == "SYSTEM") {
        return ImVec4(0.90f, 0.77f, 0.35f, 1.0f);
    }
    if (channel == "SAY") {
        return ImVec4(0.82f, 0.82f, 0.84f, 1.0f);
    }
    return ImVec4(0.66f, 0.78f, 0.95f, 1.0f);
}

} // namespace

void ChatFrame::draw(const std::vector<ChatMessage>& messages) const {
    ImGui::Text("Chat");
    ImGui::BeginChild("chat_log", {0, 0}, true);
    for (const ChatMessage& message : messages) {
        ImGui::PushStyleColor(ImGuiCol_Text, chatColor(message.channel));
        ImGui::TextWrapped("[%s] %s: %s",
                           message.channel.c_str(),
                           message.from.c_str(),
                           message.text.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void ChatFrame::drawRaw(const std::vector<std::string>& messages) const {
    ImGui::Text("Protocol");
    ImGui::BeginChild("protocol_log", {0, 0}, true);
    for (const std::string& message : messages) {
        ImGui::TextWrapped("%s", message.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

} // namespace wowclient
