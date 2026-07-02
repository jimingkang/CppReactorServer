#include "WorldSession.h"

#include <algorithm>
#include <utility>

namespace wow {

WorldSession::WorldSession(int fd)
    : fd_(fd), playerId_(fd) {}

int WorldSession::fd() const noexcept {
    return fd_;
}

int WorldSession::playerId() const noexcept {
    return playerId_;
}

bool WorldSession::closing() const noexcept {
    return closing_;
}

bool WorldSession::authenticated() const noexcept {
    return authenticated_;
}

bool WorldSession::loginPending() const noexcept {
    return loginPending_;
}

bool WorldSession::enteringWorld() const noexcept {
    return enteringWorld_;
}

bool WorldSession::inWorld() const noexcept {
    return inWorld_;
}

bool WorldSession::characterSelected() const noexcept {
    return characterSelected_;
}

int WorldSession::mapId() const noexcept {
    return mapId_;
}

int WorldSession::instanceId() const noexcept {
    return instanceId_;
}

WorldSession::Phase WorldSession::phase() const noexcept {
    return phase_;
}

const std::string& WorldSession::accountName() const noexcept {
    return accountName_;
}

const std::string& WorldSession::characterName() const noexcept {
    return characterName_;
}

const std::string& WorldSession::realmName() const noexcept {
    return realmName_;
}

const std::string& WorldSession::zoneName() const noexcept {
    return zoneName_;
}

const std::vector<WorldSession::CharacterRecord>& WorldSession::characters() const noexcept {
    return characters_;
}

void WorldSession::setPlayerId(int playerId) noexcept {
    playerId_ = playerId;
}

void WorldSession::setClosing(bool closing) noexcept {
    closing_ = closing;
    if (closing_) {
        phase_ = Phase::Closing;
    }
}

void WorldSession::setAuthenticated(bool authenticated) noexcept {
    authenticated_ = authenticated;
    if (authenticated_ && phase_ == Phase::Connected) {
        phase_ = Phase::Authed;
    } else if (!authenticated_) {
        phase_ = Phase::Connected;
    }
}

void WorldSession::setLoginPending(bool loginPending) noexcept {
    loginPending_ = loginPending;
    if (loginPending_) {
        phase_ = Phase::AuthPending;
    } else if (authenticated_) {
        phase_ = inWorld_ ? Phase::InWorld : Phase::Authed;
    } else if (!closing_) {
        phase_ = Phase::Connected;
    }
}

void WorldSession::setEnteringWorld(bool enteringWorld) noexcept {
    enteringWorld_ = enteringWorld;
    if (enteringWorld_) {
        phase_ = Phase::EnteringWorld;
    } else if (inWorld_) {
        phase_ = Phase::InWorld;
    } else if (authenticated_) {
        phase_ = Phase::Authed;
    }
}

void WorldSession::setInWorld(bool inWorld) noexcept {
    inWorld_ = inWorld;
    if (inWorld_) {
        enteringWorld_ = false;
        phase_ = Phase::InWorld;
    } else if (authenticated_ && !closing_) {
        phase_ = Phase::Authed;
    }
}

void WorldSession::setAccountName(std::string accountName) {
    accountName_ = std::move(accountName);
}

void WorldSession::setCharacterName(std::string characterName) {
    characterName_ = std::move(characterName);
    characterSelected_ = !characterName_.empty();
}

void WorldSession::setRealmName(std::string realmName) {
    realmName_ = std::move(realmName);
}

void WorldSession::setZoneName(std::string zoneName) {
    zoneName_ = std::move(zoneName);
}

void WorldSession::setMapContext(int mapId, int instanceId, std::string zoneName) {
    mapId_ = mapId;
    instanceId_ = instanceId;
    zoneName_ = std::move(zoneName);
}

void WorldSession::beginLoginRequest() noexcept {
    loginPending_ = true;
    authenticated_ = false;
    phase_ = Phase::AuthPending;
}

void WorldSession::finishLoginSuccess(std::string accountName) {
    accountName_ = std::move(accountName);
    loginPending_ = false;
    authenticated_ = true;
    phase_ = Phase::Authed;
}

void WorldSession::finishLoginFailure() noexcept {
    loginPending_ = false;
    authenticated_ = false;
    phase_ = Phase::Connected;
}

void WorldSession::selectCharacter(std::string characterName) {
    characterName_ = std::move(characterName);
    characterSelected_ = !characterName_.empty();
}

void WorldSession::beginEnterWorld() noexcept {
    enteringWorld_ = true;
    inWorld_ = false;
    phase_ = Phase::EnteringWorld;
}

void WorldSession::finishEnterWorld(int playerId, int mapId, int instanceId, std::string zoneName) {
    playerId_ = playerId;
    mapId_ = mapId;
    instanceId_ = instanceId;
    zoneName_ = std::move(zoneName);
    enteringWorld_ = false;
    inWorld_ = true;
    phase_ = Phase::InWorld;
}

void WorldSession::finishLeaveWorld() noexcept {
    inWorld_ = false;
    enteringWorld_ = false;
    if (closing_) {
        phase_ = Phase::Closing;
    } else if (authenticated_) {
        phase_ = Phase::Authed;
    } else {
        phase_ = Phase::Connected;
    }
}

void WorldSession::close() noexcept {
    closing_ = true;
    loginPending_ = false;
    enteringWorld_ = false;
    phase_ = Phase::Closing;
}

void WorldSession::addCharacter(CharacterRecord character) {
    characters_.push_back(std::move(character));
}

void WorldSession::clearCharacters() {
    characters_.clear();
}

const WorldSession::CharacterRecord* WorldSession::findCharacter(const std::string& name) const noexcept {
    const auto it = std::find_if(characters_.begin(), characters_.end(), [&](const CharacterRecord& character) {
        return character.name == name;
    });
    return it == characters_.end() ? nullptr : &*it;
}

void WorldSession::queuePendingCommand(std::string line) {
    pendingCommands_.push_back(std::move(line));
}

std::vector<std::string> WorldSession::drainPendingCommands() {
    std::vector<std::string> out;
    out.reserve(pendingCommands_.size());
    while (!pendingCommands_.empty()) {
        out.push_back(std::move(pendingCommands_.front()));
        pendingCommands_.pop_front();
    }
    return out;
}

} // namespace wow
