#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Tile {
    ImVec2 pos{};
    const char* name = "";
};

enum class SceneType {
    Tree,
    Graph,
    Maze,
    CoinChange,
    Traveler,
    DynamicProgramming,
    ExamGate,
    Count
};

struct EventCard {
    SceneType type = SceneType::Tree;
    std::string title;
    std::string character;
    std::string description;
    int scoreDelta = 0;
    int energyDelta = 0;
    int progressDelta = 0;
    int toolDelta = 0;
};

struct PlayerState {
    int tile = 0;
    int score = 120;
    int energy = 30;
    int progress = 0;
    int tools = 1;
    int turns = 0;
    int clears = 0;
    int failures = 0;
};

const char* sceneName(SceneType type) {
    switch (type) {
        case SceneType::Tree: return "Tree";
        case SceneType::Graph: return "Graph";
        case SceneType::Maze: return "Maze BFS/DFS";
        case SceneType::CoinChange: return "Coin Change";
        case SceneType::Traveler: return "Traveler Plan";
        case SceneType::DynamicProgramming: return "DP";
        case SceneType::ExamGate: return "Exam Gate";
        case SceneType::Count: break;
    }
    return "Unknown";
}

ImU32 sceneColor(SceneType type) {
    switch (type) {
        case SceneType::Tree: return IM_COL32(126, 211, 120, 255);
        case SceneType::Graph: return IM_COL32(89, 178, 238, 255);
        case SceneType::Maze: return IM_COL32(238, 184, 75, 255);
        case SceneType::CoinChange: return IM_COL32(80, 214, 188, 255);
        case SceneType::Traveler: return IM_COL32(235, 126, 184, 255);
        case SceneType::DynamicProgramming: return IM_COL32(180, 138, 255, 255);
        case SceneType::ExamGate: return IM_COL32(255, 101, 79, 255);
        case SceneType::Count: break;
    }
    return IM_COL32_WHITE;
}

class MarkovDirector {
public:
    MarkovDirector() : rng_(std::random_device{}()) {
        // Rows: current topic, columns: next topic. These base weights form a learning path:
        // Tree -> Graph -> Maze -> Coin Change -> Traveler -> DP -> Exam Gate.
        base_ = {{
            {0.14f, 0.24f, 0.18f, 0.10f, 0.08f, 0.18f, 0.08f},
            {0.14f, 0.12f, 0.24f, 0.08f, 0.14f, 0.20f, 0.08f},
            {0.10f, 0.20f, 0.10f, 0.12f, 0.12f, 0.24f, 0.12f},
            {0.08f, 0.10f, 0.10f, 0.14f, 0.18f, 0.28f, 0.12f},
            {0.08f, 0.18f, 0.10f, 0.18f, 0.10f, 0.26f, 0.10f},
            {0.12f, 0.16f, 0.12f, 0.18f, 0.18f, 0.10f, 0.14f},
            {0.18f, 0.18f, 0.14f, 0.14f, 0.12f, 0.18f, 0.06f},
        }};
        probs_.fill(1.0f / static_cast<float>(probs_.size()));
    }

    EventCard generate(const PlayerState& state, const std::vector<SceneType>& history) {
        const SceneType previous = history.empty() ? SceneType::Tree : history.back();
        probs_ = adjustedProbabilities(previous, state, history);
        const SceneType next = sample(probs_);
        return buildCard(next, state, history);
    }

    const std::array<float, 7>& lastProbabilities() const {
        return probs_;
    }

private:
    std::array<float, 7> adjustedProbabilities(SceneType previous, const PlayerState& state,
                                               const std::vector<SceneType>& history) const {
        std::array<float, 7> weights = base_[static_cast<int>(previous)];

        // Score controls difficulty: weak players review fundamentals, strong players see harder topics.
        if (state.score < 80) {
            weights[static_cast<int>(SceneType::Tree)] *= 1.80f;
            weights[static_cast<int>(SceneType::Graph)] *= 1.45f;
            weights[static_cast<int>(SceneType::ExamGate)] *= 0.45f;
            weights[static_cast<int>(SceneType::DynamicProgramming)] *= 0.70f;
        } else if (state.score > 260) {
            weights[static_cast<int>(SceneType::DynamicProgramming)] *= 1.45f;
            weights[static_cast<int>(SceneType::Traveler)] *= 1.35f;
            weights[static_cast<int>(SceneType::ExamGate)] *= 1.35f;
            weights[static_cast<int>(SceneType::Tree)] *= 0.70f;
        }

        // Tools represent hints/templates. Low tools produce practice tasks; many tools unlock integrated problems.
        if (state.tools <= 0) {
            weights[static_cast<int>(SceneType::Maze)] *= 1.50f;
            weights[static_cast<int>(SceneType::CoinChange)] *= 1.40f;
            weights[static_cast<int>(SceneType::ExamGate)] *= 0.55f;
        } else if (state.tools >= 3) {
            weights[static_cast<int>(SceneType::DynamicProgramming)] *= 1.35f;
            weights[static_cast<int>(SceneType::Traveler)] *= 1.30f;
            weights[static_cast<int>(SceneType::ExamGate)] *= 1.45f;
        }

        // Historical clear/fail record shifts difficulty.
        if (state.clears > state.failures) {
            weights[static_cast<int>(SceneType::Traveler)] *= 1.25f;
            weights[static_cast<int>(SceneType::ExamGate)] *= 1.30f;
        } else if (state.failures > state.clears) {
            weights[static_cast<int>(SceneType::Tree)] *= 1.35f;
            weights[static_cast<int>(SceneType::Graph)] *= 1.25f;
        }

        if (state.progress >= 70) {
            weights[static_cast<int>(SceneType::ExamGate)] *= 2.35f;
            weights[static_cast<int>(SceneType::DynamicProgramming)] *= 1.20f;
        }

        // Avoid repetitive scenes by penalizing the last two types.
        if (!history.empty()) {
            weights[static_cast<int>(history.back())] *= 0.42f;
        }
        if (history.size() >= 2) {
            weights[static_cast<int>(history[history.size() - 2])] *= 0.70f;
        }

        normalize(weights);
        return weights;
    }

    static void normalize(std::array<float, 7>& values) {
        for (float& v : values) {
            v = std::max(v, 0.001f);
        }
        const float total = std::accumulate(values.begin(), values.end(), 0.0f);
        for (float& v : values) {
            v /= total;
        }
    }

    SceneType sample(const std::array<float, 7>& probs) {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        float r = dist(rng_);
        for (int i = 0; i < static_cast<int>(probs.size()); ++i) {
            r -= probs[i];
            if (r <= 0.0f) {
                return static_cast<SceneType>(i);
            }
        }
        return SceneType::ExamGate;
    }

    EventCard buildCard(SceneType type, const PlayerState& state, const std::vector<SceneType>& history) {
        static const std::array<const char*, 7> people = {
            "Tree Tutor Lin", "Graph Ranger Ada", "Maze Cartographer Bo", "Coin Master Chen",
            "Traveler Nora", "DP Archivist Ren", "Exam Keeper Io"
        };

        EventCard card;
        card.type = type;
        card.character = people[static_cast<int>(type)];

        const int tempo = static_cast<int>(history.size() % 4);
        switch (type) {
            case SceneType::Tree:
                card.title = "Binary Tree Checkpoint";
                card.description = "Read a tree, identify root/leaf/depth, then choose preorder, inorder, or postorder traversal.";
                card.energyDelta = 6;
                card.scoreDelta = 16 + tempo * 3;
                card.toolDelta = 1;
                break;
            case SceneType::Graph:
                card.title = "Graph Map Station";
                card.description = "Build an adjacency list and decide whether the route needs DFS, BFS, or visited-state pruning.";
                card.toolDelta = state.tools < 3 ? 1 : 0;
                card.scoreDelta = 18;
                card.progressDelta = 6;
                break;
            case SceneType::Maze:
                card.title = "Maze Search Trial";
                card.description = "Find the shortest exit path. BFS gives distance; DFS explores structure but may not be shortest.";
                card.scoreDelta = state.tools > 0 ? 24 : 12;
                card.progressDelta = 10 + tempo * 2;
                if (state.tools > 0) {
                    card.toolDelta = -1;
                }
                break;
            case SceneType::CoinChange:
                card.title = "Coin Change Workshop";
                card.description = "Given coin values and a target, compare greedy failure cases with DP minimum-coin transitions.";
                card.scoreDelta = 20 + state.tools * 2;
                card.energyDelta = -std::min(state.energy, 5 + tempo);
                card.progressDelta = 11;
                break;
            case SceneType::Traveler:
                card.title = "Traveler Planning Problem";
                card.description = "Choose a path through cities. Model states, costs, and transitions before optimizing the route.";
                card.scoreDelta = state.tools > 0 ? 28 : 10;
                card.progressDelta = state.tools > 0 ? 14 : 7;
                if (state.tools > 0) {
                    card.toolDelta = -1;
                }
                break;
            case SceneType::DynamicProgramming:
                card.title = "DP Recurrence Lab";
                card.description = "Define dp[i], write the recurrence, choose iteration order, then check base cases.";
                card.scoreDelta = 26 + state.tools * 4;
                card.energyDelta = 8;
                card.progressDelta = 16;
                break;
            case SceneType::ExamGate:
                card.title = "Integrated Algorithm Gate";
                card.description = state.progress >= 70
                    ? "Solve a mixed challenge: parse graph data, choose BFS/DFS, and finish with a DP optimization."
                    : "The final gate appears early. Review more fundamentals before attempting the integrated task.";
                card.scoreDelta = state.progress >= 70 ? 55 : -18;
                card.progressDelta = state.progress >= 70 ? 22 : 4;
                break;
            case SceneType::Count:
                break;
        }
        return card;
    }

    std::mt19937 rng_;
    std::array<std::array<float, 7>, 7> base_{};
    std::array<float, 7> probs_{};
};

class StrategyGame {
public:
    StrategyGame() : rng_(std::random_device{}()) {
        buildBoard();
        reset();
    }

    void reset() {
        state_ = {};
        lastRoll_ = 0;
        gameOver_ = false;
        won_ = false;
        history_.clear();
        log_.clear();
        lastCard_ = director_.generate(state_, history_);
        pushLog("New learning campaign started. Roll to receive an algorithm challenge.");
    }

    void update() {
        if (gameOver_) {
            if (ImGui::Button("Restart Course", {180.0f, 34.0f})) {
                reset();
            }
            return;
        }

        if (ImGui::Button("Roll Dice / Advance", {190.0f, 38.0f})) {
            takeTurn();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset", {90.0f, 38.0f})) {
            reset();
        }
    }

    void draw(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        drawBackground(draw, origin, size);
        drawBoard(draw, origin, size);
        drawPath(draw, origin);
    }

    void drawSidePanel() {
        ImGui::Text("Learning State");
        ImGui::Separator();
        ImGui::Text("Turn: %d", state_.turns);
        ImGui::Text("Tile: %d / %zu", state_.tile, board_.size() - 1);
        ImGui::Text("Mastery Score: %d", state_.score);
        ImGui::Text("Energy: %d", state_.energy);
        ImGui::Text("Hints: %d", state_.tools);
        ImGui::Text("Progress: %d%%", state_.progress);
        ImGui::Text("Clears: %d   Failures: %d", state_.clears, state_.failures);
        ImGui::Text("Last dice: %d", lastRoll_);

        ImGui::Spacing();
        update();

        ImGui::Spacing();
        ImGui::Text("Generated Scene");
        ImGui::Separator();
        ImGui::TextColored(ImColor(sceneColor(lastCard_.type)), "%s", sceneName(lastCard_.type));
        ImGui::TextWrapped("%s", lastCard_.title.c_str());
        ImGui::Text("Character: %s", lastCard_.character.c_str());
        ImGui::TextWrapped("%s", lastCard_.description.c_str());
        ImGui::Text("Delta: score %+d, energy %+d, progress %+d, hints %+d",
                    lastCard_.scoreDelta, lastCard_.energyDelta, lastCard_.progressDelta, lastCard_.toolDelta);

        ImGui::Spacing();
        ImGui::Text("Next Scene Probabilities");
        ImGui::Separator();
        const auto& probs = director_.lastProbabilities();
        for (int i = 0; i < static_cast<int>(probs.size()); ++i) {
            const SceneType type = static_cast<SceneType>(i);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, sceneColor(type));
            ImGui::ProgressBar(probs[i], {150.0f, 0.0f}, sceneName(type));
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Text("History Log");
        ImGui::Separator();
        ImGui::BeginChild("history", {0.0f, 180.0f}, true);
        for (const std::string& line : log_) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::EndChild();
    }

private:
    void buildBoard() {
        board_.clear();
        constexpr float left = 70.0f;
        constexpr float top = 70.0f;
        constexpr float right = 720.0f;
        constexpr float bottom = 470.0f;
        constexpr int perSide = 7;

        for (int i = 0; i < perSide; ++i) {
            board_.push_back({{left + i * ((right - left) / (perSide - 1)), bottom}, "Start"});
        }
        for (int i = 1; i < perSide; ++i) {
            board_.push_back({{right, bottom - i * ((bottom - top) / (perSide - 1))}, "North"});
        }
        for (int i = 1; i < perSide; ++i) {
            board_.push_back({{right - i * ((right - left) / (perSide - 1)), top}, "West"});
        }
        for (int i = 1; i < perSide - 1; ++i) {
            board_.push_back({{left, top + i * ((bottom - top) / (perSide - 1))}, "South"});
        }
    }

    void takeTurn() {
        std::uniform_int_distribution<int> dice(1, 6);
        lastRoll_ = dice(rng_);
        state_.turns += 1;
        state_.tile = (state_.tile + lastRoll_) % static_cast<int>(board_.size());

        lastCard_ = director_.generate(state_, history_);
        applyCard(lastCard_);
        history_.push_back(lastCard_.type);
        if (history_.size() > 18) {
            history_.erase(history_.begin());
        }

        std::ostringstream oss;
        oss << "Turn " << state_.turns << ": rolled " << lastRoll_ << ", landed on tile "
            << state_.tile << ", generated " << sceneName(lastCard_.type) << ".";
        pushLog(oss.str());

        if (state_.progress >= 100) {
            won_ = true;
            gameOver_ = true;
            state_.clears += 1;
            pushLog("Course clear: mastery reached 100%. The next run will bias toward integrated problems.");
        } else if (state_.score <= 0) {
            gameOver_ = true;
            state_.failures += 1;
            pushLog("Study run failed: score reached zero. The next run will bias toward fundamentals.");
        }
    }

    void applyCard(const EventCard& card) {
        state_.score = std::max(0, state_.score + card.scoreDelta);
        state_.energy = std::max(0, state_.energy + card.energyDelta);
        state_.tools = std::max(0, state_.tools + card.toolDelta);
        state_.progress = std::clamp(state_.progress + card.progressDelta, 0, 100);

        if (card.type == SceneType::ExamGate && state_.progress >= 70) {
            state_.progress = std::min(100, state_.progress + 8);
        }
    }

    void pushLog(const std::string& message) {
        log_.insert(log_.begin(), message);
        if (log_.size() > 10) {
            log_.pop_back();
        }
    }

    void drawBackground(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        draw->AddRectFilledMultiColor(origin, {origin.x + size.x, origin.y + size.y},
                                      IM_COL32(26, 42, 58, 255), IM_COL32(29, 62, 74, 255),
                                      IM_COL32(55, 45, 74, 255), IM_COL32(33, 31, 52, 255));
        for (int i = 0; i < 18; ++i) {
            const float x = origin.x + 40.0f + static_cast<float>((i * 83) % 820);
            const float y = origin.y + 35.0f + static_cast<float>((i * 47) % 500);
            draw->AddCircleFilled({x, y}, 2.0f + static_cast<float>(i % 3), IM_COL32(255, 255, 255, 55));
        }
    }

    void drawPath(ImDrawList* draw, const ImVec2& origin) const {
        for (size_t i = 0; i < board_.size(); ++i) {
            const ImVec2 a = toScreen(origin, board_[i].pos);
            const ImVec2 b = toScreen(origin, board_[(i + 1) % board_.size()].pos);
            draw->AddLine(a, b, IM_COL32(255, 255, 255, 70), 4.0f);
        }
    }

    void drawBoard(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        const ImVec2 center{origin.x + size.x * 0.46f, origin.y + size.y * 0.50f};
        draw->AddCircleFilled(center, 130.0f, IM_COL32(15, 23, 33, 135));
        draw->AddText({center.x - 118.0f, center.y - 24.0f}, IM_COL32(233, 241, 255, 255), "ALGORITHM QUEST");
        draw->AddText({center.x - 154.0f, center.y + 4.0f}, IM_COL32(198, 218, 234, 255), "Markov-generated practice path for data structures");
        drawLearningPreview(draw, center);

        for (size_t i = 0; i < board_.size(); ++i) {
            const ImVec2 p = toScreen(origin, board_[i].pos);
            const bool active = static_cast<int>(i) == state_.tile;
            const ImU32 tileColor = active ? IM_COL32(255, 236, 135, 255) : IM_COL32(233, 241, 255, 230);
            draw->AddRectFilled({p.x - 32.0f, p.y - 24.0f}, {p.x + 32.0f, p.y + 24.0f}, tileColor, 8.0f);
            draw->AddRect({p.x - 32.0f, p.y - 24.0f}, {p.x + 32.0f, p.y + 24.0f}, IM_COL32(15, 20, 28, 180), 8.0f, 0, 2.0f);
            const std::string label = std::to_string(i);
            draw->AddText({p.x - 6.0f, p.y - 8.0f}, IM_COL32(25, 33, 42, 255), label.c_str());
        }

        const ImVec2 player = toScreen(origin, board_[state_.tile].pos);
        draw->AddCircleFilled({player.x, player.y - 42.0f}, 18.0f, IM_COL32(255, 101, 79, 255));
        draw->AddCircleFilled({player.x + 6.0f, player.y - 48.0f}, 4.0f, IM_COL32(255, 230, 205, 255));
        draw->AddTriangleFilled({player.x - 18.0f, player.y - 25.0f}, {player.x + 18.0f, player.y - 25.0f},
                                {player.x, player.y - 4.0f}, IM_COL32(89, 178, 238, 255));

        const ImU32 cardColor = sceneColor(lastCard_.type);
        draw->AddRectFilled({origin.x + 42.0f, origin.y + 34.0f}, {origin.x + 310.0f, origin.y + 174.0f},
                            IM_COL32(12, 19, 28, 215), 14.0f);
        draw->AddRect({origin.x + 42.0f, origin.y + 34.0f}, {origin.x + 310.0f, origin.y + 174.0f}, cardColor, 14.0f, 0, 3.0f);
        draw->AddText({origin.x + 62.0f, origin.y + 56.0f}, cardColor, sceneName(lastCard_.type));
        draw->AddText({origin.x + 62.0f, origin.y + 86.0f}, IM_COL32(238, 244, 255, 255), lastCard_.character.c_str());
        draw->AddText({origin.x + 62.0f, origin.y + 116.0f}, IM_COL32(198, 218, 234, 255), lastCard_.title.c_str());

        if (gameOver_) {
            const char* text = won_ ? "COURSE CLEAR" : "STUDY RUN FAILED";
            const ImVec2 ts = ImGui::CalcTextSize(text);
            draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(0, 0, 0, 95));
            draw->AddText({origin.x + size.x * 0.5f - ts.x * 0.5f, origin.y + size.y * 0.48f},
                          won_ ? IM_COL32(255, 238, 130, 255) : IM_COL32(255, 105, 105, 255), text);
        }
    }

    static ImVec2 toScreen(const ImVec2& origin, const ImVec2& local) {
        return {origin.x + local.x, origin.y + local.y};
    }

    void drawLearningPreview(ImDrawList* draw, const ImVec2& center) const {
        const ImU32 color = sceneColor(lastCard_.type);
        const ImU32 soft = IM_COL32(233, 241, 255, 160);
        const ImVec2 base{center.x, center.y + 52.0f};

        switch (lastCard_.type) {
            case SceneType::Tree: {
                const std::array<ImVec2, 7> nodes = {
                    ImVec2{base.x, base.y - 74.0f}, ImVec2{base.x - 54.0f, base.y - 30.0f},
                    ImVec2{base.x + 54.0f, base.y - 30.0f}, ImVec2{base.x - 82.0f, base.y + 18.0f},
                    ImVec2{base.x - 24.0f, base.y + 18.0f}, ImVec2{base.x + 28.0f, base.y + 18.0f},
                    ImVec2{base.x + 84.0f, base.y + 18.0f}
                };
                for (int i = 1; i < 7; ++i) {
                    draw->AddLine(nodes[(i - 1) / 2], nodes[i], soft, 2.0f);
                }
                for (const ImVec2& n : nodes) {
                    draw->AddCircleFilled(n, 9.0f, color);
                }
                break;
            }
            case SceneType::Graph:
            case SceneType::Traveler: {
                const std::array<ImVec2, 6> nodes = {
                    ImVec2{base.x - 82.0f, base.y - 52.0f}, ImVec2{base.x - 18.0f, base.y - 72.0f},
                    ImVec2{base.x + 68.0f, base.y - 44.0f}, ImVec2{base.x - 58.0f, base.y + 18.0f},
                    ImVec2{base.x + 22.0f, base.y + 26.0f}, ImVec2{base.x + 84.0f, base.y + 8.0f}
                };
                const std::array<std::pair<int, int>, 7> edges = {{{0, 1}, {1, 2}, {0, 3}, {1, 4}, {2, 5}, {3, 4}, {4, 5}}};
                for (auto [a, b] : edges) {
                    draw->AddLine(nodes[a], nodes[b], soft, 2.0f);
                }
                for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
                    draw->AddCircleFilled(nodes[i], 10.0f, i == 0 || i == 5 ? color : IM_COL32(238, 244, 255, 230));
                }
                break;
            }
            case SceneType::Maze: {
                constexpr float cell = 19.0f;
                const ImVec2 start{base.x - 76.0f, base.y - 74.0f};
                for (int y = 0; y < 7; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        const bool wall = (x == 2 && y < 5) || (x == 5 && y > 1) || (y == 3 && x > 2 && x < 7);
                        const ImVec2 a{start.x + x * cell, start.y + y * cell};
                        draw->AddRectFilled(a, {a.x + cell - 2.0f, a.y + cell - 2.0f}, wall ? IM_COL32(40, 50, 65, 255) : IM_COL32(238, 244, 255, 110), 3.0f);
                    }
                }
                draw->AddCircleFilled({start.x + 8.0f, start.y + 8.0f}, 6.0f, color);
                draw->AddCircleFilled({start.x + 7 * cell + 8.0f, start.y + 6 * cell + 8.0f}, 6.0f, IM_COL32(126, 211, 120, 255));
                break;
            }
            case SceneType::CoinChange:
            case SceneType::DynamicProgramming: {
                const ImVec2 table{base.x - 86.0f, base.y - 72.0f};
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 6; ++x) {
                        const ImVec2 a{table.x + x * 30.0f, table.y + y * 26.0f};
                        draw->AddRectFilled(a, {a.x + 26.0f, a.y + 22.0f}, (x + y) % 3 == 0 ? color : IM_COL32(238, 244, 255, 115), 4.0f);
                    }
                }
                draw->AddText({table.x + 16.0f, table.y + 116.0f}, soft, lastCard_.type == SceneType::CoinChange ? "dp[amount]" : "state -> recurrence");
                break;
            }
            case SceneType::ExamGate: {
                draw->AddRectFilled({base.x - 72.0f, base.y - 76.0f}, {base.x + 72.0f, base.y + 38.0f}, IM_COL32(30, 38, 52, 255), 10.0f);
                draw->AddRect({base.x - 72.0f, base.y - 76.0f}, {base.x + 72.0f, base.y + 38.0f}, color, 10.0f, 0, 3.0f);
                draw->AddLine({base.x - 36.0f, base.y + 38.0f}, {base.x - 36.0f, base.y - 24.0f}, soft, 3.0f);
                draw->AddLine({base.x + 36.0f, base.y + 38.0f}, {base.x + 36.0f, base.y - 24.0f}, soft, 3.0f);
                draw->AddText({base.x - 48.0f, base.y - 54.0f}, color, "FINAL");
                break;
            }
            case SceneType::Count:
                break;
        }
    }

    std::mt19937 rng_;
    MarkovDirector director_;
    PlayerState state_;
    std::vector<Tile> board_;
    std::vector<SceneType> history_;
    std::vector<std::string> log_;
    EventCard lastCard_;
    int lastRoll_ = 0;
    bool gameOver_ = false;
    bool won_ = false;
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

    GLFWwindow* window = glfwCreateWindow(1320, 780, "Algorithm Quest - Markov Strategy", nullptr, nullptr);
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
    style.WindowRounding = 10.0f;
    style.FrameRounding = 7.0f;
    style.GrabRounding = 7.0f;
    style.WindowBorderSize = 0.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    StrategyGame game;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Algorithm Quest - Markov Strategy", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

        const float sideWidth = 385.0f;
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize{std::max(100.0f, ImGui::GetContentRegionAvail().x - sideWidth - 12.0f),
                                ImGui::GetContentRegionAvail().y};
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(canvasPos, {canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y}, true);
        game.draw(draw, canvasPos, canvasSize);
        draw->PopClipRect();
        ImGui::InvisibleButton("board-canvas", canvasSize);

        ImGui::SameLine();
        ImGui::BeginChild("side-panel", {sideWidth, canvasSize.y}, true);
        game.drawSidePanel();
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.08f, 0.10f, 0.13f, 1.0f);
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
