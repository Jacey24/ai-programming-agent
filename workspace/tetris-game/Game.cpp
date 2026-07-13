#include "Game.hpp"
++ b/tetris-game/Game.cpp
#include <sstream>
#include <string>
#include <algorithm>
// 窗口尺寸和布局常量
constexpr int WINDOW_WIDTH = 400;
constexpr int WINDOW_HEIGHT = 600;
constexpr int BOARD_OFFSET_X = 20;
constexpr int BOARD_OFFSET_Y = 20;
HWND Game::getWindowHandle() const {
constexpr int CELL_SIZE = 25;
    return m_hwnd;
constexpr int PREVIEW_OFFSET_X = 290;
}
constexpr int PREVIEW_OFFSET_Y = 100;
Game::Game()
    : m_currentPiece(TetrominoType::I)
    , m_nextPiece(TetrominoType::I)
    , m_currentX(3)
    , m_currentY(0)
    , m_score(0)
    , m_level(1)
    , m_linesCleared(0)
    , m_state(GameState::PLAYING)
    , m_lastDropTime(0)
    , m_hwnd(nullptr)
    , m_hInstance(nullptr)
{
}
Game::~Game() {
}
bool Game::initialize(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;
    
    if (!registerWindowClass()) {
        return false;
    }
    
    if (!createWindow()) {
        return false;
    }
    
    // 初始化游戏
    m_board.reset();
    m_nextPiece = Tetromino::createRandom();
    spawnNewPiece();
    m_lastDropTime = GetTickCount();
    
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    
    return true;
}
int Game::run() {
    MSG msg = {0};
    
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return static_cast<int>(msg.wParam);
}
void Game::handleInput(WPARAM wParam) {
    switch (wParam) {
        case VK_LEFT:
            movePiece(-1, 0);
            break;
        case VK_RIGHT:
            movePiece(1, 0);
            break;
        case VK_DOWN:
            movePiece(0, 1);
            break;
        case VK_UP:
            rotatePiece(true);
            break;
        case VK_SPACE:
            hardDrop();
            break;
        case 'P':
        case 'p':
            if (m_state == GameState::PLAYING) {
                m_state = GameState::PAUSED;
            } else if (m_state == GameState::PAUSED) {
                m_state = GameState::PLAYING;
                m_lastDropTime = GetTickCount();
            }
            break;
        case VK_RETURN:
            if (m_state == GameState::GAME_OVER) {
                // 重新开始
                m_board.reset();
                m_score = 0;
                m_level = 1;
                m_linesCleared = 0;
                m_state = GameState::PLAYING;
                spawnNewPiece();
                m_lastDropTime = GetTickCount();
            }
            break;
    }
    
    // 强制重绘
    InvalidateRect(m_hwnd, nullptr, TRUE);
}
void Game::update() {
    if (m_state != GameState::PLAYING) {
        return;
    }
    
    DWORD currentTime = GetTickCount();
    if (currentTime - m_lastDropTime >= static_cast<DWORD>(getDropInterval())) {
        dropPiece();
        m_lastDropTime = currentTime;
    }
}
void Game::render(HDC hdc) {
    // 清空背景
    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);
    HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdc, &clientRect, bgBrush);
    DeleteObject(bgBrush);
    
    // 绘制游戏元素
    drawBoard(hdc);
    drawCurrentPiece(hdc);
    drawNextPiece(hdc);
    drawScore(hdc);
    
    if (m_state == GameState::GAME_OVER) {
        drawGameOver(hdc);
    } else if (m_state == GameState::PAUSED) {
        // 绘制暂停文字
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        HFONT hFont = CreateFont(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
        SelectObject(hdc, hFont);
        DrawText(hdc, L"PAUSED", -1, &clientRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(hFont);
    }
}
int Game::getScore() const {
    return m_score;
}
int Game::getLevel() const {
    return m_level;
}
GameState Game::getState() const {
    return m_state;
}
void Game::spawnNewPiece() {
    m_currentPiece = m_nextPiece;
    m_nextPiece = Tetromino::createRandom();
    m_currentX = (BOARD_WIDTH - m_currentPiece.getWidth()) / 2;
    m_currentY = 0;
    
    // 检查新方块是否能放置
    if (!m_board.canPlace(m_currentPiece, m_currentX, m_currentY)) {
        m_state = GameState::GAME_OVER;
    }
}
void Game::dropPiece() {
    if (!movePiece(0, 1)) {
        // 无法下落，固定当前方块
        m_board.place(m_currentPiece, m_currentX, m_currentY);
        
        // 消除完整行
        int linesCleared = m_board.clearFullRows();
        if (linesCleared > 0) {
            addScore(linesCleared);
        }
        
        // 生成新方块
        spawnNewPiece();
    }
}
void Game::hardDrop() {
    while (movePiece(0, 1)) {
        // 持续下落直到碰撞
    }
    dropPiece(); // 固定并生成新方块
}
bool Game::movePiece(int dx, int dy) {
    int newX = m_currentX + dx;
    int newY = m_currentY + dy;
    
    if (m_board.canPlace(m_currentPiece, newX, newY)) {
        m_currentX = newX;
        m_currentY = newY;
        return true;
    }
    return false;
}
void Game::rotatePiece(bool clockwise) {
    // 保存当前形状
    Tetromino rotatedPiece = m_currentPiece;
    if (clockwise) {
        rotatedPiece.rotateClockwise();
    } else {
        rotatedPiece.rotateCounterClockwise();
    }
    
    // 尝试旋转（带墙踢检测）
    if (m_board.canPlace(rotatedPiece, m_currentX, m_currentY)) {
        m_currentPiece = rotatedPiece;
    } else {
        // 尝试左移
        if (m_board.canPlace(rotatedPiece, m_currentX - 1, m_currentY)) {
            m_currentPiece = rotatedPiece;
            m_currentX -= 1;
        }
        // 尝试右移
        else if (m_board.canPlace(rotatedPiece, m_currentX + 1, m_currentY)) {
            m_currentPiece = rotatedPiece;
            m_currentX += 1;
        }
    }
}
int Game::getDropInterval() const {
    int interval = BASE_DROP_INTERVAL - (m_level - 1) * SPEED_INCREASE_PER_LEVEL;
    return std::max(interval, MIN_DROP_INTERVAL);
}
void Game::addScore(int linesCleared) {
    // 经典计分规则
    switch (linesCleared) {
        case 1: m_score += 100 * m_level; break;
        case 2: m_score += 300 * m_level; break;
        case 3: m_score += 500 * m_level; break;
        case 4: m_score += 800 * m_level; break;
    }
    
    m_linesCleared += linesCleared;
    m_level = (m_linesCleared / 10) + 1;
}
bool Game::registerWindowClass() {
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"TetrisGameClass";
    
    return RegisterClassEx(&wc) != 0;
}
bool Game::createWindow() {
    m_hwnd = CreateWindowEx(
        0,
        L"TetrisGameClass",
        L"俄罗斯方块 - Tetris",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr,
        nullptr,
        m_hInstance,
        this
    );
    
    return m_hwnd != nullptr;
}
void Game::drawBoard(HDC hdc) {
    const auto& grid = m_board.getBoard();
    
    // 绘制网格线
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
    SelectObject(hdc, gridPen);
    
    for (int row = 0; row < BOARD_HEIGHT; ++row) {
        for (int col = 0; col < BOARD_WIDTH; ++col) {
            int x = BOARD_OFFSET_X + col * CELL_SIZE;
            int y = BOARD_OFFSET_Y + row * CELL_SIZE;
            RECT cellRect = {x, y, x + CELL_SIZE, y + CELL_SIZE};
            
            uint8_t cellValue = grid[row][col];
            if (cellValue != CELL_EMPTY) {
                HBRUSH cellBrush = getColorBrush(cellValue);
                FillRect(hdc, &cellRect, cellBrush);
                DeleteObject(cellBrush);
            } else {
                // 绘制空单元格背景
                HBRUSH emptyBrush = CreateSolidBrush(RGB(40, 40, 40));
                FillRect(hdc, &cellRect, emptyBrush);
                DeleteObject(emptyBrush);
            }
            
            // 绘制网格边框
            Rectangle(hdc, x, y, x + CELL_SIZE, y + CELL_SIZE);
        }
    }
    
    DeleteObject(gridPen);
}
void Game::drawCurrentPiece(HDC hdc) {
    const auto& shape = m_currentPiece.getShape();
    uint8_t colorValue = static_cast<uint8_t>(m_currentPiece.getColor());
    HBRUSH pieceBrush = getColorBrush(colorValue);
    
    for (int row = 0; row < m_currentPiece.getHeight(); ++row) {
        for (int col = 0; col < m_currentPiece.getWidth(); ++col) {
            if (shape[row][col] != 0) {
                int x = BOARD_OFFSET_X + (m_currentX + col) * CELL_SIZE;
                int y = BOARD_OFFSET_Y + (m_currentY + row) * CELL_SIZE;
                RECT cellRect = {x, y, x + CELL_SIZE, y + CELL_SIZE};
                FillRect(hdc, &cellRect, pieceBrush);
                
                // 绘制边框
                HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
                SelectObject(hdc, borderPen);
                Rectangle(hdc, x, y, x + CELL_SIZE, y + CELL_SIZE);
                DeleteObject(borderPen);
            }
        }
    }
    
    DeleteObject(pieceBrush);
}
void Game::drawNextPiece(HDC hdc) {
    // 绘制"下一个"标签
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(200, 200, 200));
    HFONT labelFont = CreateFont(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    SelectObject(hdc, labelFont);
    
    RECT labelRect = {PREVIEW_OFFSET_X, PREVIEW_OFFSET_Y - 30, PREVIEW_OFFSET_X + 100, PREVIEW_OFFSET_Y};
    DrawText(hdc, L"下一个", -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(labelFont);
    
    // 绘制下一个方块
    const auto& shape = m_nextPiece.getShape();
    uint8_t colorValue = static_cast<uint8_t>(m_nextPiece.getColor());
    HBRUSH pieceBrush = getColorBrush(colorValue);
    
    for (int row = 0; row < m_nextPiece.getHeight(); ++row) {
        for (int col = 0; col < m_nextPiece.getWidth(); ++col) {
            if (shape[row][col] != 0) {
                int x = PREVIEW_OFFSET_X + col * (CELL_SIZE - 5);
                int y = PREVIEW_OFFSET_Y + row * (CELL_SIZE - 5);
                RECT cellRect = {x, y, x + CELL_SIZE - 5, y + CELL_SIZE - 5};
                FillRect(hdc, &cellRect, pieceBrush);
            }
        }
    }
    
    DeleteObject(pieceBrush);
}
void Game::drawScore(HDC hdc) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(200, 200, 200));
    
    // 分数
    HFONT scoreFont = CreateFont(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    SelectObject(hdc, scoreFont);
    
    std::wstring scoreText = L"分数: " + std::to_wstring(m_score);
    RECT scoreRect = {PREVIEW_OFFSET_X, 200, PREVIEW_OFFSET_X + 100, 230};
    DrawText(hdc, scoreText.c_str(), -1, &scoreRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    
    // 等级
    std::wstring levelText = L"等级: " + std::to_wstring(m_level);
    RECT levelRect = {PREVIEW_OFFSET_X, 240, PREVIEW_OFFSET_X + 100, 270};
    DrawText(hdc, levelText.c_str(), -1, &levelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    
    // 行数
    std::wstring linesText = L"行数: " + std::to_wstring(m_linesCleared);
    RECT linesRect = {PREVIEW_OFFSET_X, 280, PREVIEW_OFFSET_X + 100, 310};
    DrawText(hdc, linesText.c_str(), -1, &linesRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    
    DeleteObject(scoreFont);
}
void Game::drawGameOver(HDC hdc) {
    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);
    
    // 半透明遮罩
    HBRUSH overlayBrush = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, overlayBrush);
    HPEN overlayPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
    SelectObject(hdc, overlayPen);
    
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 128, 0};
    AlphaBlend(hdc, 0, 0, clientRect.right, clientRect.bottom,
               hdc, 0, 0, clientRect.right, clientRect.bottom, blend);
    
    DeleteObject(overlayPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(overlayBrush);
    
    // 游戏结束文字
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 50, 50));
    HFONT gameOverFont = CreateFont(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    SelectObject(hdc, gameOverFont);
    
    RECT gameOverRect = {0, 150, clientRect.right, 250};
    DrawText(hdc, L"GAME OVER", -1, &gameOverRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    // 重新开始提示
    HFONT restartFont = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    SelectObject(hdc, restartFont);
    SetTextColor(hdc, RGB(200, 200, 200));
    
    RECT restartRect = {0, 280, clientRect.right, 320};
    DrawText(hdc, L"按 Enter 重新开始", -1, &restartRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    DeleteObject(restartFont);
    DeleteObject(gameOverFont);
}
HBRUSH Game::getColorBrush(uint8_t colorValue) {
    switch (static_cast<TetrominoColor>(colorValue)) {
        case TetrominoColor::CYAN:    return CreateSolidBrush(RGB(0, 255, 255));
        case TetrominoColor::BLUE:    return CreateSolidBrush(RGB(0, 0, 255));
        case TetrominoColor::ORANGE:  return CreateSolidBrush(RGB(255, 165, 0));
        case TetrominoColor::YELLOW:  return CreateSolidBrush(RGB(255, 255, 0));
        case TetrominoColor::GREEN:   return CreateSolidBrush(RGB(0, 255, 0));
        case TetrominoColor::MAGENTA: return CreateSolidBrush(RGB(255, 0, 255));
        case TetrominoColor::RED:     return CreateSolidBrush(RGB(255, 0, 0));
        default:                      return CreateSolidBrush(RGB(255, 255, 255));
    }
}
LRESULT CALLBACK Game::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Game* game = nullptr;
    
    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        game = reinterpret_cast<Game*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(game));
        return 0;
    }
    
    game = reinterpret_cast<Game*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    
    if (game) {
        switch (msg) {
            case WM_KEYDOWN:
                game->handleInput(wParam);
                return 0;
                
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                game->render(hdc);
                EndPaint(hwnd, &ps);
                return 0;
            }
            
            case WM_TIMER: {
                game->update();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
