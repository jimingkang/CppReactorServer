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
#include <sstream>
#include <string>
#include <vector>

namespace {

void setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

struct NetworkClient {
    int fd = -1;
    bool connected = false;
    std::string host = "127.0.0.1";
    int port = 7799;
    std::string partial;
    std::string writeBuffer;
    std::vector<std::string> log;
    std::map<int, int> scores;
    int roomId = 0;
    int playerId = 0;
    bool gameOver = false;
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
        scores.clear();
        log.clear();
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
            roomId = std::atoi(line.c_str() + 13);
            status = line;
            return;
        }
        if (line.rfind("ROOM_JOINED room=", 0) == 0) {
            std::istringstream in(line);
            std::string label;
            std::string roomToken;
            std::string playerToken;
            in >> label >> roomToken >> playerToken;
            roomId = std::atoi(roomToken.c_str() + 5);
            playerId = std::atoi(playerToken.c_str() + 7);
            status = line;
            return;
        }
        if (line.rfind("PLAYER id=", 0) == 0) {
            const std::size_t idPos = line.find("id=");
            const std::size_t scorePos = line.find("score=");
            if (idPos != std::string::npos && scorePos != std::string::npos) {
                const int pid = std::atoi(line.c_str() + static_cast<int>(idPos + 3));
                const int score = std::atoi(line.c_str() + static_cast<int>(scorePos + 6));
                scores[pid] = score;
            }
            return;
        }
        if (line.rfind("GAMEOVER room=", 0) == 0) {
            gameOver = true;
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

        ImGui::Columns(2, nullptr, true);

        ImGui::InputText("Host", hostBuf, sizeof(hostBuf));
        ImGui::InputInt("Port", &port);
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
        ImGui::TextWrapped("Status: %s", client.status.c_str());
        ImGui::Text("Room: %d", client.roomId);
        ImGui::Text("Player: %d", client.playerId);
        ImGui::Text("Game over: %s", client.gameOver ? "yes" : "no");

        ImGui::Separator();
        ImGui::Text("Guess");
        ImGui::SliderInt("Number", &guess, 1, 100);
        const bool canGuess = client.connected && client.roomId > 0;
        if (!canGuess) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Send Guess", {140, 34})) {
            client.sendLine("GUESS " + std::to_string(guess));
        }
        if (ImGui::Button("State", {140, 34})) {
            client.sendLine("STATE");
        }
        if (!canGuess) {
            ImGui::EndDisabled();
        }

        ImGui::NextColumn();
        ImGui::Text("Scores");
        if (ImGui::BeginTable("scores", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Player");
            ImGui::TableSetupColumn("Score");
            ImGui::TableHeadersRow();
            for (const auto& [pid, score] : client.scores) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", pid);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", score);
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("Events");
        ImGui::BeginChild("log", {0, 0}, true);
        for (const std::string& line : client.log) {
            ImGui::TextWrapped("%s", line.c_str());
        }
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
