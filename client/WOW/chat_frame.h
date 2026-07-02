#pragma once

#include "wow_types.h"

#include <vector>

namespace wowclient {

class ChatFrame {
public:
    void draw(const std::vector<ChatMessage>& messages) const;
    void drawRaw(const std::vector<std::string>& messages) const;
};

} // namespace wowclient
