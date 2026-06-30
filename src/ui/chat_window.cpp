#include "ui/chat_window.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>

namespace p2p {

ChatWindow::ChatWindow() = default;
ChatWindow::~ChatWindow() { shutdown(); }

bool ChatWindow::init(int width, int height) {
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window_ = glfwCreateWindow(width, height, "P2P Chat", nullptr, nullptr);
    if (!window_) { glfwTerminate(); return false; }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    apply_theme();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    initialized_ = true;
    return true;
}

void ChatWindow::run(Session& session) {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        process_events(session);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render_frame(session);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }
}

void ChatWindow::shutdown() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window_) { glfwDestroyWindow(window_); window_ = nullptr; }
    glfwTerminate();
    initialized_ = false;
}

void ChatWindow::render_frame(Session& session) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_MenuBar;

    ImGui::Begin("##MainWindow", nullptr, flags);

    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Connect to Server...", "Ctrl+S"))
                show_connect_dialog_ = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Layout: sidebar (250px) | chat area
    constexpr float kSidebarWidth = 250.0f;

    ImGui::BeginChild("##Sidebar", ImVec2(kSidebarWidth, -ImGui::GetFrameHeightWithSpacing() - 4), true);
    render_sidebar(session);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginGroup();
    float chat_height = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 8;
    ImGui::BeginChild("##ChatArea", ImVec2(0, chat_height), true);
    render_chat_area(session);
    ImGui::EndChild();

    render_input_bar(session);
    ImGui::EndGroup();

    render_status_bar(session);
    ImGui::End();

    if (show_connect_dialog_) render_connect_dialog(session);
}

void ChatWindow::render_sidebar(Session& session) {
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Online Peers");
    ImGui::Separator();

    auto peers = session.known_peers();
    for (const auto& peer : peers) {
        if (peer.uuid == session.local_id().uuid) continue;

        bool is_connected = session.is_connected(peer.uuid);
        bool is_selected = (selected_peer_uuid_ == peer.uuid);

        if (is_connected)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));

        if (ImGui::Selectable(peer.name.c_str(), is_selected))
            selected_peer_uuid_ = peer.uuid;

        if (is_connected)
            ImGui::PopStyleColor();

        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            if (!is_connected) {
                if (ImGui::MenuItem("Connect")) session.connect_peer(peer.uuid);
            } else {
                if (ImGui::MenuItem("Disconnect")) session.disconnect_peer(peer.uuid);
            }
            ImGui::EndPopup();
        }

        // Tooltip
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("UUID: %s", peer.uuid.c_str());
            ImGui::Text("Endpoint: %s", peer.pub_ep.to_string().c_str());
            ImGui::Text("Status: %s", is_connected ? "Connected" : "Available");
            ImGui::EndTooltip();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Refresh", ImVec2(-1, 0)))
        session.refresh_peers();
}

void ChatWindow::render_chat_area(Session& /*session*/) {
    if (selected_peer_uuid_.empty()) {
        ImGui::TextWrapped("Select a peer from the sidebar to start chatting.");
        return;
    }

    auto& history = chat_history_[selected_peer_uuid_];

    for (const auto& msg : history) {
        if (msg.is_self) {
            float text_width = ImGui::CalcTextSize(msg.text.c_str()).x;
            float avail = ImGui::GetContentRegionAvail().x;
            float offset = std::max(avail - text_width - 80.0f, 0.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.35f, 0.55f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
        }

        ImGui::BeginChild(ImGui::GetID(&msg), ImVec2(0, 0), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "%s", msg.sender.c_str());
        ImGui::TextWrapped("%s", msg.text.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    if (scroll_to_bottom_) {
        ImGui::SetScrollHereY(1.0f);
        scroll_to_bottom_ = false;
    }
}

void ChatWindow::render_input_bar(Session& session) {
    bool send = false;
    ImGui::PushItemWidth(-80);
    if (ImGui::InputText("##Input", input_buf_, sizeof(input_buf_),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        send = true;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Send", ImVec2(70, 0))) send = true;

    if (send && input_buf_[0] != '\0' && !selected_peer_uuid_.empty()) {
        std::string text(input_buf_);
        if (session.send_text(selected_peer_uuid_, text)) {
            ChatMessage msg;
            msg.sender = session.local_id().name;
            msg.text = text;
            msg.timestamp = now_ms();
            msg.is_self = true;
            chat_history_[selected_peer_uuid_].push_back(msg);
            scroll_to_bottom_ = true;
        }
        std::memset(input_buf_, 0, sizeof(input_buf_));
        ImGui::SetKeyboardFocusHere(-1);
    }
}

void ChatWindow::render_status_bar(Session& session) {
    ImGui::Separator();
    const auto& lid = session.local_id();
    const auto& lep = session.local_ep();
    const auto& pep = session.public_ep();

    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
        "User: %s | UUID: %.8s... | Local: %s | Public: %s",
        lid.name.c_str(),
        lid.uuid.c_str(),
        lep.is_valid() ? lep.to_string().c_str() : "N/A",
        pep.is_valid() ? pep.to_string().c_str() : "N/A");
}

void ChatWindow::render_connect_dialog(Session& session) {
    ImGui::OpenPopup("Connect to Server");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Connect to Server", &show_connect_dialog_,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Username", username_buf_, sizeof(username_buf_));
        ImGui::InputText("Server IP", server_ip_buf_, sizeof(server_ip_buf_));
        ImGui::InputInt("Port", &server_port_);

        ImGui::Spacing();
        if (ImGui::Button("Connect", ImVec2(120, 0))) {
            if (username_buf_[0] != '\0') {
                if (!session.is_initialized()) {
                    if (!session.init(username_buf_)) {
                        // Could not bind socket
                        ImGui::EndPopup();
                        return;
                    }
                }
                session.connect_server(server_ip_buf_, static_cast<uint16_t>(server_port_));
                connected_to_server_ = true;
                show_connect_dialog_ = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            show_connect_dialog_ = false;
        }
        ImGui::EndPopup();
    }
}

void ChatWindow::process_events(Session& session) {
    while (auto ev = session.poll_event()) {
        switch (ev->type) {
        case ChatEvent::TextReceived: {
            ChatMessage msg;
            msg.sender = ev->peer_name;
            msg.text = ev->text;
            msg.timestamp = now_ms();
            msg.is_self = false;
            chat_history_[ev->peer_uuid].push_back(msg);
            if (ev->peer_uuid == selected_peer_uuid_)
                scroll_to_bottom_ = true;
            break;
        }
        case ChatEvent::ServerConnected:
            connected_to_server_ = true;
            break;
        case ChatEvent::PeerConnected:
        case ChatEvent::PeerDisconnected:
        case ChatEvent::PeerListUpdated:
        case ChatEvent::ServerDisconnected:
        case ChatEvent::Error:
            break;
        }
    }
}

void ChatWindow::apply_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding      = 3.0f;
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(8, 4);
    style.ItemSpacing       = ImVec2(8, 6);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg]        = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_Border]         = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
    colors[ImGuiCol_TitleBg]        = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    colors[ImGuiCol_MenuBarBg]      = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]    = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.22f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.28f, 0.55f, 0.82f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.18f, 0.38f, 0.60f, 1.00f);
    colors[ImGuiCol_Header]         = ImVec4(0.22f, 0.45f, 0.70f, 0.50f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.28f, 0.55f, 0.82f, 0.60f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.18f, 0.38f, 0.60f, 1.00f);
    colors[ImGuiCol_Separator]      = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_Text]           = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]   = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
}

} // namespace p2p
