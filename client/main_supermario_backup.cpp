#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct Enemy {
    Rect box;
    float vx = -70.0f;
    bool alive = true;
};

struct Coin {
    Vec2 pos;
    bool taken = false;
};

struct Player {
    Vec2 pos{70.0f, 260.0f};
    Vec2 vel{};
    Vec2 size{28.0f, 38.0f};
    bool onGround = false;
    int lives = 3;
    int score = 0;
};

constexpr float kWorldWidth = 2400.0f;
constexpr float kWorldHeight = 520.0f;
constexpr float kGravity = 1450.0f;
constexpr float kMoveSpeed = 235.0f;
constexpr float kJumpSpeed = -560.0f;
constexpr float kGroundY = 430.0f;

bool intersects(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

Rect playerRect(const Player& player) {
    return {player.pos.x, player.pos.y, player.size.x, player.size.y};
}

class MarioGame {
public:
    MarioGame() {
        reset();
    }

    void reset() {
        player_ = {};
        won_ = false;
        gameOver_ = false;
        message_ = "A/D or Left/Right to move, Space to jump";

        platforms_ = {
            Rect{0.0f, kGroundY, kWorldWidth, 90.0f},
            Rect{260.0f, 340.0f, 140.0f, 28.0f},
            Rect{520.0f, 300.0f, 170.0f, 28.0f},
            Rect{820.0f, 360.0f, 160.0f, 28.0f},
            Rect{1110.0f, 315.0f, 180.0f, 28.0f},
            Rect{1460.0f, 355.0f, 220.0f, 28.0f},
            Rect{1830.0f, 305.0f, 180.0f, 28.0f},
        };

        blocks_ = {
            Rect{350.0f, 235.0f, 34.0f, 34.0f},
            Rect{650.0f, 220.0f, 34.0f, 34.0f},
            Rect{1180.0f, 225.0f, 34.0f, 34.0f},
            Rect{1540.0f, 250.0f, 34.0f, 34.0f},
            Rect{1900.0f, 215.0f, 34.0f, 34.0f},
        };

        coins_ = {
            Coin{{300.0f, 305.0f}}, Coin{{360.0f, 200.0f}}, Coin{{585.0f, 265.0f}},
            Coin{{650.0f, 185.0f}}, Coin{{880.0f, 325.0f}}, Coin{{1185.0f, 190.0f}},
            Coin{{1240.0f, 280.0f}}, Coin{{1545.0f, 215.0f}}, Coin{{1605.0f, 320.0f}},
            Coin{{1905.0f, 180.0f}}, Coin{{1980.0f, 270.0f}},
        };

        enemies_ = {
            Enemy{{610.0f, kGroundY - 28.0f, 32.0f, 28.0f}, -70.0f},
            Enemy{{980.0f, kGroundY - 28.0f, 32.0f, 28.0f}, 80.0f},
            Enemy{{1510.0f, 327.0f, 32.0f, 28.0f}, -65.0f},
            Enemy{{2040.0f, kGroundY - 28.0f, 32.0f, 28.0f}, -90.0f},
        };
    }

    void update(GLFWwindow* window, float dt) {
        dt = std::min(dt, 1.0f / 30.0f);

        if (isPressed(window, GLFW_KEY_R)) {
            reset();
            return;
        }

        if (gameOver_ || won_) {
            return;
        }

        const bool left = isPressed(window, GLFW_KEY_A) || isPressed(window, GLFW_KEY_LEFT);
        const bool right = isPressed(window, GLFW_KEY_D) || isPressed(window, GLFW_KEY_RIGHT);
        const bool jump = isPressed(window, GLFW_KEY_SPACE) || isPressed(window, GLFW_KEY_UP) || isPressed(window, GLFW_KEY_W);

        player_.vel.x = 0.0f;
        if (left) {
            player_.vel.x -= kMoveSpeed;
        }
        if (right) {
            player_.vel.x += kMoveSpeed;
        }
        if (jump && player_.onGround) {
            player_.vel.y = kJumpSpeed;
            player_.onGround = false;
        }

        player_.vel.y += kGravity * dt;
        movePlayer(dt);
        updateEnemies(dt);
        collectCoins();
        handleEnemies();

        if (player_.pos.y > kWorldHeight + 120.0f) {
            hurtPlayer();
        }

        if (player_.pos.x > 2265.0f) {
            won_ = true;
            player_.score += 1000;
            message_ = "Course clear! Press R to play again.";
        }
    }

    void draw(ImDrawList* draw, const ImVec2& canvasPos, const ImVec2& canvasSize) {
        cameraX_ = std::clamp(player_.pos.x - canvasSize.x * 0.38f, 0.0f, kWorldWidth - canvasSize.x);

        drawSky(draw, canvasPos, canvasSize);
        drawScenery(draw, canvasPos, canvasSize);

        for (const Rect& platform : platforms_) {
            drawRect(draw, canvasPos, platform, IM_COL32(126, 78, 36, 255), IM_COL32(91, 52, 25, 255));
        }
        for (const Rect& block : blocks_) {
            drawRect(draw, canvasPos, block, IM_COL32(218, 148, 54, 255), IM_COL32(130, 70, 28, 255));
            drawTextWorld(draw, canvasPos, {block.x + 10.0f, block.y + 7.0f}, "?", IM_COL32(255, 244, 178, 255));
        }
        for (const Coin& coin : coins_) {
            if (!coin.taken) {
                drawCoin(draw, canvasPos, coin.pos);
            }
        }
        for (const Enemy& enemy : enemies_) {
            if (enemy.alive) {
                drawEnemy(draw, canvasPos, enemy.box);
            }
        }
        drawFlag(draw, canvasPos);
        drawPlayer(draw, canvasPos);
        drawHud(draw, canvasPos, canvasSize);
    }

private:
    static bool isPressed(GLFWwindow* window, int key) {
        return glfwGetKey(window, key) == GLFW_PRESS;
    }

    std::vector<Rect> solidRects() const {
        std::vector<Rect> solids = platforms_;
        solids.insert(solids.end(), blocks_.begin(), blocks_.end());
        return solids;
    }

    void movePlayer(float dt) {
        const std::vector<Rect> solids = solidRects();
        player_.onGround = false;

        player_.pos.x += player_.vel.x * dt;
        player_.pos.x = std::clamp(player_.pos.x, 0.0f, kWorldWidth - player_.size.x);
        Rect px = playerRect(player_);
        for (const Rect& solid : solids) {
            if (!intersects(px, solid)) {
                continue;
            }
            if (player_.vel.x > 0.0f) {
                player_.pos.x = solid.x - player_.size.x;
            } else if (player_.vel.x < 0.0f) {
                player_.pos.x = solid.x + solid.w;
            }
            px = playerRect(player_);
        }

        player_.pos.y += player_.vel.y * dt;
        Rect py = playerRect(player_);
        for (const Rect& solid : solids) {
            if (!intersects(py, solid)) {
                continue;
            }
            if (player_.vel.y > 0.0f) {
                player_.pos.y = solid.y - player_.size.y;
                player_.vel.y = 0.0f;
                player_.onGround = true;
            } else if (player_.vel.y < 0.0f) {
                player_.pos.y = solid.y + solid.h;
                player_.vel.y = 80.0f;
                hitBlock(solid);
            }
            py = playerRect(player_);
        }
    }

    void hitBlock(const Rect& solid) {
        for (Coin& coin : coins_) {
            const bool above = std::abs(coin.pos.x - (solid.x + solid.w * 0.5f)) < 30.0f && coin.pos.y < solid.y;
            if (!coin.taken && above) {
                coin.taken = true;
                player_.score += 100;
                message_ = "Hidden coin!";
                return;
            }
        }
    }

    void updateEnemies(float dt) {
        for (Enemy& enemy : enemies_) {
            if (!enemy.alive) {
                continue;
            }
            enemy.box.x += enemy.vx * dt;

            bool standing = false;
            Rect foot{enemy.box.x + 4.0f, enemy.box.y + enemy.box.h + 1.0f, enemy.box.w - 8.0f, 4.0f};
            for (const Rect& platform : platforms_) {
                standing = standing || intersects(foot, platform);
            }
            if (!standing || enemy.box.x < 80.0f || enemy.box.x > 2180.0f) {
                enemy.vx *= -1.0f;
                enemy.box.x += enemy.vx * dt * 2.0f;
            }
        }
    }

    void collectCoins() {
        const Rect pr = playerRect(player_);
        for (Coin& coin : coins_) {
            const Rect coinBox{coin.pos.x - 10.0f, coin.pos.y - 10.0f, 20.0f, 20.0f};
            if (!coin.taken && intersects(pr, coinBox)) {
                coin.taken = true;
                player_.score += 100;
                message_ = "Coin +100";
            }
        }
    }

    void handleEnemies() {
        Rect pr = playerRect(player_);
        for (Enemy& enemy : enemies_) {
            if (!enemy.alive || !intersects(pr, enemy.box)) {
                continue;
            }
            const bool stomp = player_.vel.y > 0.0f && player_.pos.y + player_.size.y - enemy.box.y < 18.0f;
            if (stomp) {
                enemy.alive = false;
                player_.vel.y = -330.0f;
                player_.score += 250;
                message_ = "Stomp +250";
            } else {
                hurtPlayer();
                return;
            }
        }
    }

    void hurtPlayer() {
        player_.lives -= 1;
        if (player_.lives <= 0) {
            gameOver_ = true;
            message_ = "Game over. Press R to restart.";
            return;
        }
        player_.pos = {70.0f, 260.0f};
        player_.vel = {};
        message_ = "Ouch! Try again.";
    }

    ImVec2 worldToScreen(const ImVec2& origin, Vec2 point) const {
        return {origin.x + point.x - cameraX_, origin.y + point.y};
    }

    void drawRect(ImDrawList* draw, const ImVec2& origin, Rect r, ImU32 fill, ImU32 outline) const {
        const ImVec2 a = worldToScreen(origin, {r.x, r.y});
        const ImVec2 b = worldToScreen(origin, {r.x + r.w, r.y + r.h});
        draw->AddRectFilled(a, b, fill, 4.0f);
        draw->AddRect(a, b, outline, 4.0f, 0, 2.0f);
    }

    void drawTextWorld(ImDrawList* draw, const ImVec2& origin, Vec2 point, const char* text, ImU32 color) const {
        const ImVec2 p = worldToScreen(origin, point);
        draw->AddText(p, color, text);
    }

    void drawSky(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        draw->AddRectFilledMultiColor(origin, {origin.x + size.x, origin.y + size.y},
                                      IM_COL32(112, 196, 255, 255), IM_COL32(112, 196, 255, 255),
                                      IM_COL32(205, 239, 255, 255), IM_COL32(205, 239, 255, 255));
    }

    void drawScenery(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        for (int i = 0; i < 8; ++i) {
            const float baseX = i * 360.0f - std::fmod(cameraX_ * 0.35f, 360.0f);
            const ImVec2 cloud{origin.x + baseX + 70.0f, origin.y + 80.0f + (i % 3) * 22.0f};
            draw->AddCircleFilled(cloud, 22.0f, IM_COL32(255, 255, 255, 220));
            draw->AddCircleFilled({cloud.x + 25.0f, cloud.y + 6.0f}, 20.0f, IM_COL32(255, 255, 255, 220));
            draw->AddCircleFilled({cloud.x - 24.0f, cloud.y + 8.0f}, 18.0f, IM_COL32(255, 255, 255, 220));
        }

        const std::array<float, 6> hills{140.0f, 500.0f, 860.0f, 1220.0f, 1580.0f, 1940.0f};
        for (float hillX : hills) {
            const ImVec2 p = worldToScreen(origin, {hillX, kGroundY});
            draw->AddTriangleFilled({p.x - 120.0f, p.y}, {p.x, p.y - 145.0f}, {p.x + 145.0f, p.y},
                                    IM_COL32(71, 176, 76, 150));
        }

        draw->AddRectFilled({origin.x, origin.y + size.y - 28.0f}, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(46, 156, 67, 255));
    }

    void drawCoin(ImDrawList* draw, const ImVec2& origin, Vec2 pos) const {
        const float wobble = std::sin(static_cast<float>(ImGui::GetTime()) * 7.0f + pos.x * 0.03f);
        const ImVec2 p = worldToScreen(origin, pos);
        const ImVec2 radius{10.0f + wobble * 2.0f, 14.0f};
        draw->AddEllipseFilled(p, radius, IM_COL32(255, 210, 52, 255));
        draw->AddEllipse(p, radius, IM_COL32(157, 105, 24, 255), 0.0f, 0, 2.0f);
    }

    void drawEnemy(ImDrawList* draw, const ImVec2& origin, Rect box) const {
        const ImVec2 a = worldToScreen(origin, {box.x, box.y});
        const ImVec2 b = worldToScreen(origin, {box.x + box.w, box.y + box.h});
        draw->AddRectFilled(a, b, IM_COL32(133, 76, 36, 255), 8.0f);
        draw->AddCircleFilled({a.x + 9.0f, a.y + 9.0f}, 3.0f, IM_COL32(20, 20, 20, 255));
        draw->AddCircleFilled({b.x - 9.0f, a.y + 9.0f}, 3.0f, IM_COL32(20, 20, 20, 255));
        draw->AddTriangleFilled({a.x + 4.0f, b.y}, {a.x + 13.0f, b.y}, {a.x + 8.0f, b.y + 8.0f}, IM_COL32(60, 37, 24, 255));
        draw->AddTriangleFilled({b.x - 13.0f, b.y}, {b.x - 4.0f, b.y}, {b.x - 8.0f, b.y + 8.0f}, IM_COL32(60, 37, 24, 255));
    }

    void drawFlag(ImDrawList* draw, const ImVec2& origin) const {
        const ImVec2 poleTop = worldToScreen(origin, {2280.0f, 210.0f});
        const ImVec2 poleBottom = worldToScreen(origin, {2280.0f, kGroundY});
        draw->AddLine(poleTop, poleBottom, IM_COL32(245, 245, 245, 255), 5.0f);
        draw->AddTriangleFilled(poleTop, {poleTop.x + 68.0f, poleTop.y + 22.0f}, {poleTop.x, poleTop.y + 44.0f},
                                IM_COL32(239, 65, 54, 255));
    }

    void drawPlayer(ImDrawList* draw, const ImVec2& origin) const {
        const Rect r = playerRect(player_);
        const ImVec2 a = worldToScreen(origin, {r.x, r.y});
        const ImVec2 b = worldToScreen(origin, {r.x + r.w, r.y + r.h});
        draw->AddRectFilled({a.x + 3.0f, a.y + 13.0f}, b, IM_COL32(42, 93, 203, 255), 5.0f);
        draw->AddRectFilled({a.x + 1.0f, a.y + 4.0f}, {b.x - 1.0f, a.y + 18.0f}, IM_COL32(229, 48, 43, 255), 5.0f);
        draw->AddRectFilled({a.x + 6.0f, a.y}, {b.x - 4.0f, a.y + 8.0f}, IM_COL32(229, 48, 43, 255), 3.0f);
        draw->AddCircleFilled({a.x + 18.0f, a.y + 14.0f}, 7.0f, IM_COL32(255, 198, 132, 255));
        draw->AddCircleFilled({a.x + 20.0f, a.y + 12.0f}, 1.7f, IM_COL32(30, 30, 30, 255));
        draw->AddRectFilled({a.x + 4.0f, b.y - 3.0f}, {a.x + 13.0f, b.y + 4.0f}, IM_COL32(88, 50, 28, 255), 2.0f);
        draw->AddRectFilled({b.x - 13.0f, b.y - 3.0f}, {b.x - 3.0f, b.y + 4.0f}, IM_COL32(88, 50, 28, 255), 2.0f);
    }

    void drawHud(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        const ImVec2 panelA{origin.x + 14.0f, origin.y + 12.0f};
        const ImVec2 panelB{origin.x + size.x - 14.0f, origin.y + 66.0f};
        draw->AddRectFilled(panelA, panelB, IM_COL32(10, 28, 48, 180), 10.0f);
        draw->AddRect(panelA, panelB, IM_COL32(255, 255, 255, 90), 10.0f);

        const std::string hud = "Score " + std::to_string(player_.score) +
                                "    Lives " + std::to_string(player_.lives) +
                                "    Coins " + std::to_string(collectedCoins()) + "/" + std::to_string(coins_.size());
        draw->AddText({panelA.x + 16.0f, panelA.y + 10.0f}, IM_COL32(255, 255, 255, 255), hud.c_str());
        draw->AddText({panelA.x + 16.0f, panelA.y + 31.0f}, IM_COL32(255, 235, 168, 255), message_.c_str());

        if (gameOver_ || won_) {
            const char* text = won_ ? "YOU WIN" : "GAME OVER";
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            draw->AddRectFilled({origin.x, origin.y}, {origin.x + size.x, origin.y + size.y}, IM_COL32(0, 0, 0, 95));
            draw->AddText({origin.x + size.x * 0.5f - textSize.x * 0.5f, origin.y + size.y * 0.45f},
                          won_ ? IM_COL32(255, 235, 110, 255) : IM_COL32(255, 92, 92, 255), text);
        }
    }

    int collectedCoins() const {
        return static_cast<int>(std::count_if(coins_.begin(), coins_.end(), [](const Coin& coin) {
            return coin.taken;
        }));
    }

    Player player_;
    std::vector<Rect> platforms_;
    std::vector<Rect> blocks_;
    std::vector<Coin> coins_;
    std::vector<Enemy> enemies_;
    float cameraX_ = 0.0f;
    bool gameOver_ = false;
    bool won_ = false;
    std::string message_;
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

    GLFWwindow* window = glfwCreateWindow(1280, 720, "ImGui Super Mario", nullptr, nullptr);
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
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 7.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    MarioGame game;
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        game.update(window, dt);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Super Mario", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(canvasPos, {canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y}, true);
        game.draw(draw, canvasPos, canvasSize);
        draw->PopClipRect();
        ImGui::InvisibleButton("game-canvas", canvasSize);
        ImGui::End();

        ImGui::Render();

        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.08f, 0.11f, 0.16f, 1.0f);
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
