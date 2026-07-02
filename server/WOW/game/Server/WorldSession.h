#pragma once

#include <deque>
#include <string>
#include <vector>

namespace wow {

class WorldSession {
public:
    struct CharacterRecord {
        std::string name;
        std::string race;
        std::string klass;
        std::string gender;
        int level = 1;
        std::string zone = "Northshire";
    };

    enum class Phase {
        Connected,
        AuthPending,
        Authed,
        EnteringWorld,
        InWorld,
        Closing,
    };

    explicit WorldSession(int fd);

    int fd() const noexcept;
    int playerId() const noexcept;
    bool closing() const noexcept;
    bool authenticated() const noexcept;
    bool loginPending() const noexcept;
    bool enteringWorld() const noexcept;
    bool inWorld() const noexcept;
    bool characterSelected() const noexcept;
    int mapId() const noexcept;
    int instanceId() const noexcept;
    Phase phase() const noexcept;

    const std::string& accountName() const noexcept;
    const std::string& characterName() const noexcept;
    const std::string& realmName() const noexcept;
    const std::string& zoneName() const noexcept;
    const std::vector<CharacterRecord>& characters() const noexcept;

    void setPlayerId(int playerId) noexcept;
    void setClosing(bool closing) noexcept;
    void setAuthenticated(bool authenticated) noexcept;
    void setLoginPending(bool loginPending) noexcept;
    void setEnteringWorld(bool enteringWorld) noexcept;
    void setInWorld(bool inWorld) noexcept;
    void setAccountName(std::string accountName);
    void setCharacterName(std::string characterName);
    void setRealmName(std::string realmName);
    void setZoneName(std::string zoneName);
    void setMapContext(int mapId, int instanceId, std::string zoneName);

    void beginLoginRequest() noexcept;
    void finishLoginSuccess(std::string accountName);
    void finishLoginFailure() noexcept;
    void selectCharacter(std::string characterName);
    void beginEnterWorld() noexcept;
    void finishEnterWorld(int playerId, int mapId, int instanceId, std::string zoneName);
    void finishLeaveWorld() noexcept;
    void close() noexcept;

    void addCharacter(CharacterRecord character);
    void clearCharacters();
    const CharacterRecord* findCharacter(const std::string& name) const noexcept;

    void queuePendingCommand(std::string line);
    std::vector<std::string> drainPendingCommands();

private:
    int fd_ = -1;
    int playerId_ = 0;
    int mapId_ = 0;
    int instanceId_ = 1;
    bool closing_ = false;
    bool authenticated_ = false;
    bool loginPending_ = false;
    bool enteringWorld_ = false;
    bool inWorld_ = false;
    bool characterSelected_ = false;
    std::string accountName_;
    std::string characterName_ = "ShellAdventurer";
    std::string realmName_ = "LocalDev";
    std::string zoneName_ = "Northshire";
    Phase phase_ = Phase::Connected;
    std::vector<CharacterRecord> characters_;
    std::deque<std::string> pendingCommands_;
};

} // namespace wow
