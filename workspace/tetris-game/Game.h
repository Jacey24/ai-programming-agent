#ifndef GAME_H
#define GAME_H

#include <vector>
#include <string>
#include "Tetromino.h"

class Game {
public:
    Game();
    ~Game();

    void run();
    void draw();
    void handleInput();
    void update();
    bool isRunning() const;

private:
    static const int BOARD_WIDTH = 10;
    static const int BOARD_HEIGHT = 20;
    static const int BOARD_TOP = 2;
    static const int BOARD_LEFT = 2;

    std::vector<std::vector<int>> board;
    Tetromino currentPiece;
    Tetromino nextPiece;
    int score;
    int level;
    int linesCleared;
    bool gameOver;
    bool running;

    void initBoard();
    void spawnNewPiece();
    bool isValidPosition(const Tetromino& piece, int newX, int newY) const;
    void lockPiece();
    void clearLines();
    void drawBoard() const;
    void drawPiece(const Tetromino& piece) const;
    void drawNextPiece() const;
    void drawScore() const;
    void drawGameOver() const;
    void rotatePiece();
    void moveLeft();
    void moveRight();
    void moveDown();
    void hardDrop();
};

#endif // GAME_H
