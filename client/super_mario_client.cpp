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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Platform {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct RemotePlayer {
    int id = 0;
    int x = 0;
    int y = 0;
    int hp = 100;
    int score = 0;
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

        if (connect(socketFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            status_ = std::string("connect failed: ") + std::strerror(errno);
            disconnect();
            return false;
        }

        setNonBlocking(socketFd_);
        connected_ = true;
        status_ = "connected";
        sendLine("HELP");
        sendLine("STATE");
        return true;
    }

    void disconnect() {
        if (socketFd_ >= 0) {
            close(socketFd_);
        }
        socketFd_ = -1;
        connected_ = false;
        playerId_ = 0;
        inbox_.clear();
        partial_.clear();
        players_.clear();
    }

    bool connected() const {
        return connected_;
    }

    int playerId() const {
        return playerId_;
    }

    const std::string& status() const {
        return status_;
    }

    const std::vector<std::string>& inbox() const {
        return inbox_;
    }

    const std::unordered_map<int, RemotePlayer>& players() const {
        return players_;
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
                partial_.append(buffer, static_cast<size_t>(n));
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
                outbox_.erase(0, static_cast<size_t>(n));
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
        size_t pos = 0;
        while ((pos = partial_.find('\n')) != std::string::npos) {
            std::string line = partial_.substr(0, pos);
            partial_.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            handleLine(line);
        }
    }

    void handleLine(const std::string& line) {
        inbox_.insert(inbox_.begin(), line);
        if (inbox_.size() > 12) {
            inbox_.pop_back();
        }

        if (line.rfind("WELCOME player=", 0) == 0) {
            playerId_ = std::atoi(line.c_str() + 15);
            return;
        }

        if (line.rfind("PLAYER ", 0) == 0) {
            RemotePlayer player;
            std::istringstream input(line);
            std::string token;
            input >> token;
            while (input >> token) {
                const size_t eq = token.find('=');
                if (eq == std::string::npos) {
                    continue;
                }
                const std::string key = token.substr(0, eq);
                const int value = std::atoi(token.c_str() + eq + 1);
                if (key == "id") player.id = value;
                if (key == "x") player.x = value;
                if (key == "y") player.y = value;
                if (key == "hp") player.hp = value;
                if (key == "score") player.score = value;
            }
            if (player.id > 0) {
                players_[player.id] = player;
            }
        }
    }

    int socketFd_ = -1;
    bool connected_ = false;
    int playerId_ = 0;
    std::string status_ = "disconnected";
    std::string partial_;
    std::string outbox_;
    std::vector<std::string> inbox_;
    std::unordered_map<int, RemotePlayer> players_;
};

class SuperMarioNetGame {
public:
    void update(GLFWwindow* window, float dt, NetworkClient& network) {
        dt = std::min(dt, 1.0f / 30.0f);
        const bool left = key(window, GLFW_KEY_A) || key(window, GLFW_KEY_LEFT);
        const bool right = key(window, GLFW_KEY_D) || key(window, GLFW_KEY_RIGHT);
        const bool jump = key(window, GLFW_KEY_SPACE) || key(window, GLFW_KEY_W) || key(window, GLFW_KEY_UP);

        velocity_.x = 0.0f;
        if (left) {
            velocity_.x -= 240.0f;
        }
        if (right) {
            velocity_.x += 240.0f;
        }
        if (jump && onGround_) {
            velocity_.y = -560.0f;
            onGround_ = false;
            queuedJump_ = true;
        }

        velocity_.y += 1450.0f * dt;
        position_.x = std::clamp(position_.x + velocity_.x * dt, 0.0f, 2360.0f);
        position_.y += velocity_.y * dt;
        collideVertical();

        sendTimer_ += dt;
        stateTimer_ += dt;
        if (network.connected() && sendTimer_ >= 0.12f) {
            network.sendLine("POS " + std::to_string(static_cast<int>(position_.x)) + " " +
                             std::to_string(static_cast<int>(position_.y)));
            queuedJump_ = false;
            sendTimer_ = 0.0f;
        }
        if (network.connected() && stateTimer_ >= 0.15f) {
            network.sendLine("STATE");
            stateTimer_ = 0.0f;
        }
    }

    void draw(ImDrawList* draw, const ImVec2& origin, const ImVec2& size, const NetworkClient& network) {
        cameraX_ = std::clamp(position_.x - size.x * 0.35f, 0.0f, 2400.0f - size.x);
        drawSky(draw, origin, size);
        drawScenery(draw, origin, size);
        for (const Platform& platform : platforms_) {
            drawPlatform(draw, origin, platform);
        }
        drawFlag(draw, origin);
        drawRemotePlayers(draw, origin, network);
        drawMario(draw, origin, position_, IM_COL32(232, 54, 48, 255));
        drawHud(draw, origin, size, network);
    }

private:
    static bool key(GLFWwindow* window, int code) {
        return glfwGetKey(window, code) == GLFW_PRESS;
    }

    void collideVertical() {
        onGround_ = false;
        for (const Platform& p : platforms_) {
            const bool insideX = position_.x + 28.0f > p.x && position_.x < p.x + p.w;
            const bool falling = velocity_.y >= 0.0f;
            const bool crossed = position_.y + 38.0f >= p.y && position_.y + 38.0f <= p.y + p.h + 24.0f;
            if (insideX && falling && crossed) {
                position_.y = p.y - 38.0f;
                velocity_.y = 0.0f;
                onGround_ = true;
            }
        }
        if (position_.y > 620.0f) {
            position_ = {70.0f, 260.0f};
            velocity_ = {};
        }
    }

    ImVec2 world(const ImVec2& origin, float x, float y) const {
        return {origin.x + x - cameraX_, origin.y + y};
    }

    void drawSky(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        draw->AddRectFilledMultiColor(origin, {origin.x + size.x, origin.y + size.y},
                                      IM_COL32(103, 192, 255, 255), IM_COL32(103, 192, 255, 255),
                                      IM_COL32(216, 244, 255, 255), IM_COL32(216, 244, 255, 255));
    }

    void drawScenery(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        for (int i = 0; i < 7; ++i) {
            const float x = origin.x + 120.0f + i * 240.0f - std::fmod(cameraX_ * 0.35f, 240.0f);
            const float y = origin.y + 85.0f + (i % 2) * 34.0f;
            draw->AddCircleFilled({x, y}, 22.0f, IM_COL32(255, 255, 255, 210));
            draw->AddCircleFilled({x + 27.0f, y + 6.0f}, 18.0f, IM_COL32(255, 255, 255, 210));
            draw->AddCircleFilled({x - 24.0f, y + 8.0f}, 17.0f, IM_COL32(255, 255, 255, 210));
        }
        draw->AddRectFilled({origin.x, origin.y + size.y - 34.0f}, {origin.x + size.x, origin.y + size.y}, IM_COL32(48, 155, 72, 255));
    }

    void drawPlatform(ImDrawList* draw, const ImVec2& origin, const Platform& p) const {
        const ImVec2 a = world(origin, p.x, p.y);
        const ImVec2 b = world(origin, p.x + p.w, p.y + p.h);
        draw->AddRectFilled(a, b, IM_COL32(136, 83, 39, 255), 4.0f);
        draw->AddRect(a, b, IM_COL32(87, 49, 23, 255), 4.0f, 0, 2.0f);
    }

    void drawMario(ImDrawList* draw, const ImVec2& origin, Vec2 pos, ImU32 hatColor) const {
        const ImVec2 a = world(origin, pos.x, pos.y);
        const ImVec2 b = world(origin, pos.x + 28.0f, pos.y + 38.0f);
        draw->AddRectFilled({a.x + 3.0f, a.y + 14.0f}, b, IM_COL32(45, 91, 204, 255), 5.0f);
        draw->AddRectFilled({a.x + 1.0f, a.y + 4.0f}, {b.x - 1.0f, a.y + 18.0f}, hatColor, 5.0f);
        draw->AddRectFilled({a.x + 6.0f, a.y}, {b.x - 4.0f, a.y + 8.0f}, hatColor, 3.0f);
        draw->AddCircleFilled({a.x + 18.0f, a.y + 14.0f}, 7.0f, IM_COL32(255, 199, 138, 255));
        draw->AddCircleFilled({a.x + 20.0f, a.y + 12.0f}, 1.7f, IM_COL32(25, 25, 25, 255));
    }

    void drawRemotePlayers(ImDrawList* draw, const ImVec2& origin, const NetworkClient& network) const {
        for (const auto& [id, player] : network.players()) {
            if (id == network.playerId()) {
                continue;
            }
            const Vec2 pos{static_cast<float>(player.x), static_cast<float>(player.y)};
            drawMario(draw, origin, pos, IM_COL32(77, 190, 95, 255));
            const ImVec2 label = world(origin, pos.x - 2.0f, pos.y - 18.0f);
            const std::string text = "P" + std::to_string(id);
            draw->AddText(label, IM_COL32(20, 35, 40, 255), text.c_str());
        }
    }

    void drawFlag(ImDrawList* draw, const ImVec2& origin) const {
        const ImVec2 top = world(origin, 2260.0f, 215.0f);
        const ImVec2 bottom = world(origin, 2260.0f, 430.0f);
        draw->AddLine(top, bottom, IM_COL32(255, 255, 255, 255), 5.0f);
        draw->AddTriangleFilled(top, {top.x + 70.0f, top.y + 25.0f}, {top.x, top.y + 50.0f}, IM_COL32(238, 55, 51, 255));
    }

    void drawHud(ImDrawList* draw, const ImVec2& origin, const ImVec2& size, const NetworkClient& network) const {
        draw->AddRectFilled({origin.x + 16.0f, origin.y + 14.0f}, {origin.x + size.x - 16.0f, origin.y + 76.0f}, IM_COL32(10, 30, 50, 180), 10.0f);
        const std::string status = std::string("Network: ") + network.status() + "   player=" + std::to_string(network.playerId());
        draw->AddText({origin.x + 34.0f, origin.y + 28.0f}, IM_COL32(255, 255, 255, 255), "Super Mario Network Client");
        draw->AddText({origin.x + 34.0f, origin.y + 50.0f}, IM_COL32(255, 235, 170, 255), status.c_str());
    }

    Vec2 position_{70.0f, 260.0f};
    Vec2 velocity_{};
    bool onGround_ = false;
    bool queuedJump_ = false;
    float cameraX_ = 0.0f;
    float sendTimer_ = 0.0f;
    float stateTimer_ = 0.0f;
    std::vector<Platform> platforms_ = {
        {0.0f, 430.0f, 2400.0f, 90.0f}, {260.0f, 340.0f, 150.0f, 28.0f}, {540.0f, 300.0f, 170.0f, 28.0f},
        {850.0f, 360.0f, 150.0f, 28.0f}, {1180.0f, 315.0f, 180.0f, 28.0f}, {1500.0f, 355.0f, 220.0f, 28.0f},
        {1850.0f, 305.0f, 190.0f, 28.0f}
    };
};

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

    GLFWwindow* window = glfwCreateWindow(1320, 760, "Super Mario Network Client", nullptr, nullptr);
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
    SuperMarioNetGame game;
    char host[64] = "127.0.0.1";
    int port = 7777;
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        network.poll();

        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        game.update(window, dt, network);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Super Mario Network", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

        const float panelWidth = 340.0f;
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize{std::max(100.0f, ImGui::GetContentRegionAvail().x - panelWidth - 12.0f),
                                ImGui::GetContentRegionAvail().y};
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(canvasPos, {canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y}, true);
        game.draw(draw, canvasPos, canvasSize, network);
        draw->PopClipRect();
        ImGui::InvisibleButton("mario-canvas", canvasSize);

        ImGui::SameLine();
        ImGui::BeginChild("network-panel", {panelWidth, canvasSize.y}, true);
        ImGui::Text("Server");
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
            if (ImGui::Button("State", {80.0f, 32.0f})) {
                network.sendLine("STATE");
            }
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Status: %s", network.status().c_str());
        ImGui::Text("Local player id: %d", network.playerId());
        ImGui::TextWrapped("Controls: A/D or arrows move, Space/W jumps. Position is sent to server as POS x y.");

        ImGui::Spacing();
        ImGui::Text("Players");
        ImGui::Separator();
        for (const auto& [id, player] : network.players()) {
            ImGui::Text("P%d x=%d y=%d hp=%d score=%d", id, player.x, player.y, player.hp, player.score);
        }

        ImGui::Spacing();
        ImGui::Text("Server Log");
        ImGui::Separator();
        ImGui::BeginChild("server-log", {0.0f, 260.0f}, true);
        for (const std::string& line : network.inbox()) {
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
        glClearColor(0.08f, 0.12f, 0.16f, 1.0f);
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
