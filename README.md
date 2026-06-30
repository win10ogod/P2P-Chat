# P2P Chat (v2.0)

這是一個基於 C++17 開發的跨平台 P2P 聊天平台。
此版本全面重構了核心架構，採用了現代 C++ 慣用法（RAII、智能指標等），並整合了 **Dear ImGui** 作為跨平台的圖形化使用者介面（GUI）。

## 功能特色

*   **跨平台圖形介面**：使用 Dear ImGui 配合 GLFW 與 OpenGL，提供現代且流暢的聊天體驗。
*   **純 P2P 網路架構**：文字與音訊資料皆透過 UDP 直接傳輸，無須伺服器中繼。
*   **NAT 穿透技術**：內建 UDP Hole Punching，可有效穿透多數家用路由器，實現直連。
*   **現代 C++ 實踐**：全面使用 C++17 特性，包含 `std::optional`、`std::variant`、`std::shared_mutex` 與 RAII 資源管理。
*   **輕量級信令伺服器**：提供一個極簡的信令伺服器（Signaling Server），僅用於初始的節點發現與位址交換。

## 系統需求

*   **編譯器**：支援 C++17 的編譯器 (GCC 8+, Clang 9+, MSVC 19.14+)
*   **建置工具**：CMake 3.15 或以上版本
*   **相依套件**：
    *   GLFW 3.3+ (GUI 視窗與輸入)
    *   OpenGL 3.3+ (圖形渲染)
    *   *Dear ImGui 已經內建於 `third_party` 目錄中。*

### Ubuntu / Debian 環境準備

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libglfw3-dev libgl1-mesa-dev
```

## 編譯指南

專案採用標準的 CMake 建置流程：

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

編譯完成後，將在 `build` 目錄下產生兩個執行檔：
*   `p2pchat`：客戶端主程式（包含 GUI）
*   `signaling_server`：信令伺服器

## 執行與使用

### 1. 啟動信令伺服器
首先需要在具有公網 IP 的機器（或本機測試環境）上啟動信令伺服器：
```bash
./signaling_server 9000
```

### 2. 啟動客戶端
啟動客戶端 GUI：
```bash
./p2pchat
```
或者使用命令列參數預先帶入設定：
```bash
./p2pchat -u "YourName" -s "127.0.0.1:9000"
```

### 3. 操作流程
1.  在 GUI 視窗中，點擊左上角的 `File -> Connect to Server...`。
2.  輸入您的使用者名稱以及信令伺服器的 IP 與 Port（預設 9000），點擊 `Connect`。
3.  連線成功後，左側側邊欄會顯示目前在線的其他使用者。
4.  在左側名單中 **右鍵點擊** 目標使用者，選擇 `Connect` 發起 P2P 打洞連線。
5.  連線成功（狀態轉為綠色）後，點擊該使用者即可在右側視窗開始聊天。

## 模組架構說明

*   `src/core/`：核心資料結構、型別定義、執行緒安全的佇列與會話管理。
*   `src/network/`：跨平台的非同步 UDP Socket 封裝、P2P 連線管理（含打洞邏輯）與信令客戶端。
*   `src/protocol/`：二進位通訊協定的序列化與反序列化實作。
*   `src/ui/`：基於 Dear ImGui 的跨平台圖形介面實作。
*   `src/audio/`：音訊引擎介面（目前為架構保留，未來可整合 PortAudio + Opus）。

## 授權條款

本專案採用 MIT License。
Dear ImGui 採用 MIT License。
