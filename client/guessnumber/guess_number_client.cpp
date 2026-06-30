#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::unordered_map<std::string, std::string> parseKeyValues(const std::string& line) {
    std::unordered_map<std::string, std::string> fields;
    std::istringstream in(line);
    std::string token;
    in >> token;
    while (in >> token) {
        const std::size_t pos = token.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        fields[token.substr(0, pos)] = token.substr(pos + 1);
    }
    return fields;
}

int parseIntField(const std::unordered_map<std::string, std::string>& fields, const char* key, int fallback = 0) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return fallback;
    }
    return std::atoi(it->second.c_str());
}

std::string parseStringField(const std::unordered_map<std::string, std::string>& fields, const char* key, std::string fallback = {}) {
    const auto it = fields.find(key);
    return it == fields.end() ? std::move(fallback) : it->second;
}

void setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

struct NetworkClient {
    struct GuessEvent {
        int playerId = 0;
        int value = 0;
        std::string hint;
        bool correct = false;
    };

    int fd = -1;
    bool connected = false;
    std::string host = "127.0.0.1";
    int port = 7799;
    std::string partial;
    std::string writeBuffer;
    std::vector<std::string> log;
    std::map<int, int> scores;
    std::map<int, int> wins;
    std::map<int, int> gamesPlayed;
    std::vector<GuessEvent> guesses;
    int roomId = 0;
    int playerId = 0;
    bool gameOver = false;
    bool matched = false;
    bool joinedRoom = false;
    int matchedPlayers = 0;
    int winnerPlayerId = 0;
    int secretValue = 0;
    int attempts = 0;
    std::optional<GuessEvent> lastGuessEvent;
    std::string roomState = "idle";
    std::string matchStatus = "Disconnected";
    std::string hintMessage = "Connect to start matching.";
    std::string resultMessage = "No guesses yet.";
    std::string status = "disconnected";

    ~NetworkClient() {
        disconnect();
    }

    bool connectNow() {
        disconnect();
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            status = "socket failed";
            return false;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            status = "bad host";
            close(fd);
            fd = -1;
            return false;
        }
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 && errno != EINPROGRESS) {
            status = std::string("connect failed: ") + std::strerror(errno);
            close(fd);
            fd = -1;
            return false;
        }
        setNonBlocking(fd);
        connected = true;
        gameOver = false;
        matched = false;
        joinedRoom = false;
        matchedPlayers = 0;
        winnerPlayerId = 0;
        secretValue = 0;
        attempts = 0;
        scores.clear();
        wins.clear();
        gamesPlayed.clear();
        guesses.clear();
        log.clear();
        roomState = "matching";
        matchStatus = "Connected";
        hintMessage = "Waiting for other players to join.";
        resultMessage = "No guesses yet.";
        status = "connected";
        return true;
    }

    void disconnect() {
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
        connected = false;
        partial.clear();
        writeBuffer.clear();
        roomId = 0;
        playerId = 0;
        scores.clear();
        wins.clear();
        gamesPlayed.clear();
        guesses.clear();
        lastGuessEvent.reset();
        matched = false;
        joinedRoom = false;
        matchedPlayers = 0;
        gameOver = false;
        winnerPlayerId = 0;
        secretValue = 0;
        attempts = 0;
        roomState = "idle";
        matchStatus = "Disconnected";
        hintMessage = "Connect to start matching.";
        resultMessage = "No guesses yet.";
    }

    void sendLine(const std::string& line) {
        if (!connected) {
            return;
        }
        writeBuffer += line;
        writeBuffer.push_back('\n');
    }

    void poll() {
        if (!connected) {
            return;
        }

        while (!writeBuffer.empty()) {
            const ssize_t n = send(fd, writeBuffer.data(), writeBuffer.size(), 0);
            if (n > 0) {
                writeBuffer.erase(0, static_cast<std::size_t>(n));
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            status = "send failed";
            disconnect();
            return;
        }

        char buffer[4096];
        while (true) {
            const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
            if (n > 0) {
                partial.append(buffer, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) {
                status = "server closed";
                disconnect();
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            status = "recv failed";
            disconnect();
            return;
        }

        std::size_t pos = 0;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos);
            partial.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                consumeLine(line);
            }
        }
    }

    void consumeLine(const std::string& line) {
        log.push_back(line);
        if (log.size() > 200) {
            log.erase(log.begin(), log.begin() + static_cast<long>(log.size() - 200));
        }

        if (line.rfind("MATCHED room=", 0) == 0) {
            const auto fields = parseKeyValues(line);
            roomId = parseIntField(fields, "room");
            matchedPlayers = parseIntField(fields, "players");
            matched = true;
            joinedRoom = false;
            matchStatus = "Matched";
            roomState = "matched";
            hintMessage = "Match found. Joining room...";
            resultMessage = "Room formed with " + std::to_string(std::max(1, matchedPlayers)) + " players.";
            status = line;
            return;
        }
        if (line.rfind("ROOM_JOINED room=", 0) == 0) {
            const auto fields = parseKeyValues(line);
            roomId = parseIntField(fields, "room");
            playerId = parseIntField(fields, "player");
            joinedRoom = true;
            matched = true;
            gameOver = false;
            winnerPlayerId = 0;
            secretValue = 0;
            attempts = 0;
            roomState = "playing";
            matchStatus = "In room";
            hintMessage = "Make a guess between 1 and 100.";
            resultMessage = "Joined room " + std::to_string(roomId) + ".";
            status = line;
            return;
        }
        if (line.rfind("PLAYER id=", 0) == 0) {
            const auto fields = parseKeyValues(line);
            const int pid = parseIntField(fields, "id");
            scores[pid] = parseIntField(fields, "score");
            wins[pid] = parseIntField(fields, "gamesWon");
            gamesPlayed[pid] = parseIntField(fields, "gamesPlayed");
            return;
        }
        if (line.rfind("GUESS room=", 0) == 0) {
            const auto fields = parseKeyValues(line);
            GuessEvent event;
            event.playerId = parseIntField(fields, "player");
            event.value = parseIntField(fields, "value");
            event.hint = parseStringField(fields, "hint");
            guesses.push_back(event);
            if (guesses.size() > 100) {
                guesses.erase(guesses.begin(), guesses.begin() + static_cast<long>(guesses.size() - 100));
            }
            lastGuessEvent = event;
            if (event.playerId == playerId) {
                hintMessage = event.hint == "higher" ? "Too small. Try a higher number."
                             : event.hint == "lower" ? "Too large. Try a lower number."
                             : "You guessed it.";
                resultMessage = "Your guess " + std::to_string(event.value) + " was " + event.hint + ".";
            } else {
                hintMessage = "Player " + std::to_string(event.playerId) + " guessed " + std::to_string(event.value) + ".";
                resultMessage = "Player " + std::to_string(event.playerId) + " should go " + event.hint + ".";
            }
            status = line;
            return;
        }
        if (line.rfind("ROOM room=", 0) == 0) {
            const auto fields = parseKeyValues(line);
            roomState = parseStringField(fields, "state", roomState);
            gameOver = parseIntField(fields, "finished", gameOver ? 1 : 0) != 0;
            return;
        }
        if (line.rfind("GAMEOVER room=", 0) == 0) {
            const auto fields = parseKeyValues(line);
            gameOver = true;
            roomState = "finished";
            winnerPlayerId = parseIntField(fields, "loser");
            secretValue = parseIntField(fields, "secret");
            attempts = parseIntField(fields, "attempts");
            if (winnerPlayerId == playerId) {
                hintMessage = "You guessed the number.";
                resultMessage = "Correct. Secret was " + std::to_string(secretValue) +
                                " after " + std::to_string(attempts) + " attempts.";
            } else {
                hintMessage = "Player " + std::to_string(winnerPlayerId) + " guessed the number.";
                resultMessage = "Secret was " + std::to_string(secretValue) +
                                ". Winner: player " + std::to_string(winnerPlayerId) + ".";
            }
            status = line;
            return;
        }
        if (line.rfind("WAITING", 0) == 0 || line.rfind("HALL status=", 0) == 0) {
            matched = false;
            joinedRoom = false;
            roomState = "matching";
            matchStatus = "Matching";
            hintMessage = "Waiting for enough players to start.";
            resultMessage = "You will join automatically when the room is full.";
            status = line;
            return;
        }
        if (line.rfind("BYE", 0) == 0) {
            status = line;
            disconnect();
            return;
        }
        status = line;
    }
};

} // namespace

int main() {
    if (!glfwInit()) {
        return 1;
    }

    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1100, 760, "Guess Number Client", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    NetworkClient client;
    char hostBuf[64] = "127.0.0.1";
    int port = 7799;
    int guess = 50;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        client.poll();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Guess Number", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        ImGui::BeginChild("top", {0, 160}, false);
        ImGui::InputText("Host", hostBuf, sizeof(hostBuf));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("Port", &port);
        ImGui::SameLine();
        if (!client.connected) {
            if (ImGui::Button("Connect", {120, 34})) {
                client.host = hostBuf;
                client.port = port;
                client.connectNow();
            }
        } else {
            if (ImGui::Button("Quit", {120, 34})) {
                client.sendLine("QUIT");
            }
        }

        ImGui::Separator();
        ImGui::Text("Match: %s", client.matchStatus.c_str());
        ImGui::SameLine();
        ImGui::Text("Room: %d", client.roomId);
        ImGui::SameLine();
        ImGui::Text("Player: %d", client.playerId);

        if (client.gameOver) {
            const bool iWon = client.winnerPlayerId == client.playerId;
            ImGui::TextColored(iWon ? ImVec4(0.35f, 0.85f, 0.35f, 1.0f) : ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                               "%s", iWon ? "You guessed the number." : "Another player guessed the number.");
        } else if (client.joinedRoom) {
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.95f, 1.0f), "Game in progress");
        } else {
            ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.30f, 1.0f), "Waiting for room");
        }
        ImGui::TextWrapped("%s", client.hintMessage.c_str());
        ImGui::TextWrapped("%s", client.resultMessage.c_str());
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Columns(2, nullptr, true);

        ImGui::BeginChild("left", {0, 0}, false);
        ImGui::Text("Make a guess");
        ImGui::SliderInt("Number", &guess, 1, 100);
        const bool canGuess = client.connected && client.joinedRoom && !client.gameOver;
        if (!canGuess) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Send Guess", {140, 34})) {
            client.sendLine("GUESS " + std::to_string(guess));
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", {140, 34})) {
            client.sendLine("STATE");
        }
        if (!canGuess) {
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::Text("Recent guesses");
        if (ImGui::BeginTable("guesses", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Player");
            ImGui::TableSetupColumn("Guess");
            ImGui::TableSetupColumn("Result");
            ImGui::TableHeadersRow();
            const std::size_t start = client.guesses.size() > 12 ? client.guesses.size() - 12 : 0;
            for (std::size_t i = start; i < client.guesses.size(); ++i) {
                const auto& event = client.guesses[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d%s", event.playerId, event.playerId == client.playerId ? " (You)" : "");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", event.value);
                ImGui::TableSetColumnIndex(2);
                if (event.hint == "higher") {
                    ImGui::Text("Too small");
                } else if (event.hint == "lower") {
                    ImGui::Text("Too large");
                } else {
                    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "Correct");
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::NextColumn();
        ImGui::BeginChild("right", {0, 0}, false);
        ImGui::Text("Players");
        if (ImGui::BeginTable("scores", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Player");
            ImGui::TableSetupColumn("Score");
            ImGui::TableSetupColumn("Wins");
            ImGui::TableSetupColumn("Games");
            ImGui::TableHeadersRow();
            for (const auto& [pid, score] : client.scores) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d%s", pid, pid == client.playerId ? " (You)" : "");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", score);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", client.wins[pid]);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", client.gamesPlayed[pid]);
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Text("Event log");
        ImGui::BeginChild("log", {0, 0}, true);
        for (const std::string& line : client.log) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::End();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
