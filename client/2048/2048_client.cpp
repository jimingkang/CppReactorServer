#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct BoardState {
    std::array<int, 16> cells{};
    int player = 0;
    int score = 0;
    int best = 0;
    bool over = false;
    bool won = false;
    bool valid = false;
};

int setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

class NetworkClient {
public:
    ~NetworkClient() {
        disconnect();
    }

    bool connectTo(const std::string& host, int port) {
        disconnect();
        socketFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socketFd_ < 0) {
            status_ = std::string("socket failed: ") + std::strerror(errno);
            return false;
        }

#if defined(SO_NOSIGPIPE)
        int yes = 1;
        setsockopt(socketFd_, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            status_ = "invalid host";
            disconnect();
            return false;
        }
        if (connect(socketFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            status_ = std::string("connect failed: ") + std::strerror(errno);
            disconnect();
            return false;
        }

        setNonBlocking(socketFd_);
        connected_ = true;
        status_ = "connected";
        sendLine("STATE");
        return true;
    }

    void disconnect() {
        if (socketFd_ >= 0) {
            close(socketFd_);
        }
        socketFd_ = -1;
        connected_ = false;
        partial_.clear();
        outbox_.clear();
        log_.clear();
        board_ = {};
        status_ = "disconnected";
    }

    bool connected() const {
        return connected_;
    }

    const std::string& status() const {
        return status_;
    }

    const BoardState& board() const {
        return board_;
    }

    const std::vector<std::string>& log() const {
        return log_;
    }

    void sendLine(const std::string& line) {
        if (!connected_) {
            return;
        }
        outbox_ += line;
        if (outbox_.empty() || outbox_.back() != '\n') {
            outbox_.push_back('\n');
        }
        flushWrites();
    }

    void poll() {
        if (!connected_) {
            return;
        }
        flushWrites();

        char buffer[2048];
        while (true) {
            const ssize_t n = recv(socketFd_, buffer, sizeof(buffer), 0);
            if (n > 0) {
                partial_.append(buffer, static_cast<std::size_t>(n));
                consumeLines();
                continue;
            }
            if (n == 0) {
                status_ = "server closed";
                disconnect();
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }
            status_ = std::string("recv failed: ") + std::strerror(errno);
            disconnect();
            return;
        }
    }

private:
    void flushWrites() {
        while (connected_ && !outbox_.empty()) {
#if defined(MSG_NOSIGNAL)
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const ssize_t n = send(socketFd_, outbox_.data(), outbox_.size(), flags);
            if (n > 0) {
                outbox_.erase(0, static_cast<std::size_t>(n));
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }
            status_ = std::string("send failed: ") + std::strerror(errno);
            disconnect();
            return;
        }
    }

    void consumeLines() {
        std::size_t pos = 0;
        while ((pos = partial_.find('\n')) != std::string::npos) {
            std::string line = partial_.substr(0, pos);
            partial_.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                handleLine(line);
            }
        }
    }

    void handleLine(const std::string& line) {
        log_.insert(log_.begin(), line);
        if (log_.size() > 18) {
            log_.pop_back();
        }

        if (line.rfind("STATE ", 0) != 0) {
            return;
        }

        BoardState board;
        board.valid = true;
        std::istringstream input(line);
        std::string token;
        input >> token;
        while (input >> token) {
            const std::size_t eq = token.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            const std::string key = token.substr(0, eq);
            const char* value = token.c_str() + eq + 1;
            if (key == "player") board.player = std::atoi(value);
            if (key == "score") board.score = std::atoi(value);
            if (key == "best") board.best = std::atoi(value);
            if (key == "over") board.over = std::atoi(value) != 0;
            if (key == "won") board.won = std::atoi(value) != 0;
            if (key == "cells") {
                std::string cells = value;
                std::replace(cells.begin(), cells.end(), ',', ' ');
                std::istringstream cellInput(cells);
                for (int& cell : board.cells) {
                    cellInput >> cell;
                }
            }
        }
        board_ = board;
    }

    int socketFd_ = -1;
    bool connected_ = false;
    std::string status_ = "disconnected";
    std::string partial_;
    std::string outbox_;
    std::vector<std::string> log_;
    BoardState board_;
};

ImU32 tileColor(int value) {
    switch (value) {
    case 0: return IM_COL32(190, 182, 171, 255);
    case 2: return IM_COL32(238, 228, 218, 255);
    case 4: return IM_COL32(237, 224, 200, 255);
    case 8: return IM_COL32(242, 177, 121, 255);
    case 16: return IM_COL32(245, 149, 99, 255);
    case 32: return IM_COL32(246, 124, 95, 255);
    case 64: return IM_COL32(246, 94, 59, 255);
    case 128: return IM_COL32(237, 207, 114, 255);
    case 256: return IM_COL32(237, 204, 97, 255);
    case 512: return IM_COL32(237, 200, 80, 255);
    case 1024: return IM_COL32(237, 197, 63, 255);
    default: return IM_COL32(237, 194, 46, 255);
    }
}

void drawBoard(const BoardState& board, const ImVec2& origin, float size) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float gap = 10.0f;
    const float tile = (size - gap * 5.0f) / 4.0f;

    draw->AddRectFilled(origin, {origin.x + size, origin.y + size}, IM_COL32(133, 121, 107, 255), 8.0f);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int value = board.cells[static_cast<std::size_t>(y * 4 + x)];
            const ImVec2 a{origin.x + gap + x * (tile + gap), origin.y + gap + y * (tile + gap)};
            const ImVec2 b{a.x + tile, a.y + tile};
            draw->AddRectFilled(a, b, tileColor(value), 6.0f);
            if (value != 0) {
                const std::string text = std::to_string(value);
                const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
                const ImU32 textColor = value <= 4 ? IM_COL32(84, 73, 63, 255) : IM_COL32(255, 255, 255, 255);
                draw->AddText({a.x + (tile - textSize.x) * 0.5f, a.y + (tile - textSize.y) * 0.5f}, textColor, text.c_str());
            }
        }
    }
}

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

} // namespace

int main() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        return 1;
    }

#if defined(__APPLE__)
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(920, 680, "2048 Network Client", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    NetworkClient network;
    char host[64] = "127.0.0.1";
    int port = 7788;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        network.poll();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("2048 Network", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

        const float panelWidth = 290.0f;
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float boardSize = std::min(avail.y - 24.0f, avail.x - panelWidth - 28.0f);
        const ImVec2 boardPos{canvasPos.x + 20.0f, canvasPos.y + 20.0f};
        drawBoard(network.board(), boardPos, std::max(260.0f, boardSize));
        ImGui::Dummy({std::max(280.0f, boardSize + 40.0f), avail.y});

        ImGui::SameLine();
        ImGui::BeginChild("side-panel", {panelWidth, avail.y}, true);
        ImGui::Text("2048 Server");
        ImGui::Separator();
        ImGui::InputText("Host", host, sizeof(host));
        ImGui::InputInt("Port", &port);
        if (!network.connected()) {
            if (ImGui::Button("Connect", {110.0f, 32.0f})) {
                network.connectTo(host, port);
            }
        } else {
            if (ImGui::Button("Disconnect", {110.0f, 32.0f})) {
                network.sendLine("QUIT");
                network.disconnect();
            }
            ImGui::SameLine();
            if (ImGui::Button("State", {72.0f, 32.0f})) {
                network.sendLine("STATE");
            }
        }

        const BoardState& board = network.board();
        ImGui::Spacing();
        ImGui::TextWrapped("Status: %s", network.status().c_str());
        ImGui::Text("Player: %d", board.player);
        ImGui::Text("Score: %d", board.score);
        ImGui::Text("Best: %d", board.best);
        if (board.won) {
            ImGui::TextColored({1.0f, 0.86f, 0.22f, 1.0f}, "2048 reached");
        }
        if (board.over) {
            ImGui::TextColored({1.0f, 0.28f, 0.24f, 1.0f}, "No moves left");
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(!network.connected());
        const float buttonW = 76.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonW + 8.0f);
        if (ImGui::Button("Up", {buttonW, 30.0f})) {
            network.sendLine("MOVE UP");
        }
        if (ImGui::Button("Left", {buttonW, 30.0f})) {
            network.sendLine("MOVE LEFT");
        }
        ImGui::SameLine();
        if (ImGui::Button("Down", {buttonW, 30.0f})) {
            network.sendLine("MOVE DOWN");
        }
        ImGui::SameLine();
        if (ImGui::Button("Right", {buttonW, 30.0f})) {
            network.sendLine("MOVE RIGHT");
        }
        if (ImGui::Button("Reset", {160.0f, 30.0f})) {
            network.sendLine("RESET");
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Text("Server Log");
        ImGui::Separator();
        ImGui::BeginChild("server-log", {0.0f, 220.0f}, true);
        for (const std::string& line : network.log()) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.10f, 0.11f, 0.12f, 1.0f);
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

