#ifndef TETROMINO_H
#define TETROMINO_H

#include <vector>

class Tetromino {
public:
    enum Type { I, O, T, S, Z, J, L, NONE };

    Tetromino();
    Tetromino(Type type);
    ~Tetromino();

    void setType(Type type);
    Type getType() const;

    const std::vector<std::vector<int>>& getShape() const;
    int getX() const;
    int getY() const;
    void setX(int x);
    void setY(int y);

    void rotate();
    void rotateBack();

    static std::vector<std::vector<int>> getShapeForType(Type type);
    static int getColorForType(Type type);

private:
    Type type;
    int x;
    int y;
    std::vector<std::vector<int>> shape;

    void initShape();
};

#endif // TETROMINO_H
