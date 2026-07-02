#pragma once

namespace wowclient {

class WorldSession;

class ActionBar {
public:
    void draw(WorldSession& session) const;
};

} // namespace wowclient
