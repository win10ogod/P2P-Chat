#pragma once

/**
 * @file chat_window.h
 * @brief Dear ImGui-based cross-platform chat GUI.
 */

#include "core/types.h"
#include "core/session.h"
#include <deque>
#include <map>
#include <string>

struct GLFWwindow;

namespace p2p {

/**
 * @class ChatWindow
 * @brief Manages the GUI lifecycle and renders the chat interface.
 */
class ChatWindow {
public:
    ChatWindow();
    ~ChatWindow();

    /// Initialize GLFW + OpenGL + ImGui. Returns false on failure.
    [[nodiscard]] bool init(int width = 1024, int height = 720);

    /// Run the main render loop (blocking until window close).
    void run(Session& session);

    /// Cleanup all graphics resources.
    void shutdown();

private:
    void render_frame(Session& session);
    void render_sidebar(Session& session);
    void render_chat_area(Session& session);
    void render_input_bar(Session& session);
    void render_status_bar(Session& session);
    void render_connect_dialog(Session& session);
    void process_events(Session& session);
    void apply_theme();

    GLFWwindow* window_{nullptr};

    // Chat message model
    struct ChatMessage {
        std::string sender;
        std::string text;
        uint64_t    timestamp{0};
        bool        is_self{false};
    };

    std::map<std::string, std::deque<ChatMessage>> chat_history_;
    std::string selected_peer_uuid_;

    // Input buffers
    char input_buf_[4096]{};
    char server_ip_buf_[128]{"127.0.0.1"};
    char username_buf_[64]{};
    int  server_port_{9000};

    // UI state
    bool show_connect_dialog_{false};
    bool initialized_{false};
    bool connected_to_server_{false};
    bool scroll_to_bottom_{false};
};

} // namespace p2p
