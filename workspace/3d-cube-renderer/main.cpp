#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>

using namespace std;

// 控制台尺寸
const int WIDTH = 80;
const int HEIGHT = 40;

// 立方体的8个顶点（相对于中心）
struct Point3D {
    double x, y, z;
};

const vector<Point3D> vertices = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}
};

// 12条边（顶点索引对）
const vector<pair<int, int>> edges = {
    {0,1}, {1,2}, {2,3}, {3,0},
    {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7}
};

// 3D旋转矩阵
Point3D rotateX(const Point3D& p, double angle) {
    double c = cos(angle), s = sin(angle);
    return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
}

Point3D rotateY(const Point3D& p, double angle) {
    double c = cos(angle), s = sin(angle);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}

Point3D rotateZ(const Point3D& p, double angle) {
    double c = cos(angle), s = sin(angle);
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}

// 投影到2D屏幕坐标（简单透视投影）
struct Point2D {
    int x, y;
    double depth; // 用于z-buffer
};

Point2D project(const Point3D& p, double fov, double viewerDist) {
    double factor = fov / (viewerDist + p.z);
    int screenX = (int)(p.x * factor + WIDTH / 2);
    int screenY = (int)(-p.y * factor + HEIGHT / 2);
    return {screenX, screenY, p.z};
}

// 绘制线段的简单算法（Bresenham风格）
void drawLine(vector<vector<char>>& screen, vector<vector<double>>& zbuffer,
              const Point2D& p1, const Point2D& p2, char ch) {
    int x1 = p1.x, y1 = p1.y;
    int x2 = p2.x, y2 = p2.y;
    
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    
    // 插值深度
    double depth1 = p1.depth, depth2 = p2.depth;
    double totalDist = sqrt((double)(x2-x1)*(x2-x1) + (double)(y2-y1)*(y2-y1));
    if (totalDist < 0.001) totalDist = 0.001;
    
    while (true) {
        // 计算当前点的深度（线性插值）
        double t = 0.0;
        if (totalDist > 0.001) {
            double curDist = sqrt((double)(x1-x1)*(x1-x1) + (double)(y1-y1)*(y1-y1)); // 实际应为到起点的距离
            // 简化：使用起点深度
        }
        
        if (x1 >= 0 && x1 < WIDTH && y1 >= 0 && y1 < HEIGHT) {
            // 简单深度测试：使用起点深度
            if (depth1 > zbuffer[y1][x1]) {
                screen[y1][x1] = ch;
                zbuffer[y1][x1] = depth1;
            }
        }
        
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

int main() {
    double angleX = 0, angleY = 0, angleZ = 0;
    
    while (true) {
        // 创建屏幕缓冲和z-buffer
        vector<vector<char>> screen(HEIGHT, vector<char>(WIDTH, ' '));
        vector<vector<double>> zbuffer(HEIGHT, vector<double>(WIDTH, -999.0));
        
        // 旋转并投影所有顶点
        vector<Point2D> projected;
        for (const auto& v : vertices) {
            Point3D r = rotateX(v, angleX);
            r = rotateY(r, angleY);
            r = rotateZ(r, angleZ);
            projected.push_back(project(r, 30.0, 5.0));
        }
        
        // 绘制所有边
        for (const auto& edge : edges) {
            drawLine(screen, zbuffer, projected[edge.first], projected[edge.second], '#');
        }
        
        // 在顶点位置绘制亮点
        for (const auto& p : projected) {
            if (p.x >= 0 && p.x < WIDTH && p.y >= 0 && p.y < HEIGHT) {
                screen[p.y][p.x] = '@';
            }
        }
        
        // 输出到控制台
        cout << "\033[H"; // 移动光标到左上角
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                cout << screen[y][x];
            }
            cout << '\n';
        }
        
        // 更新旋转角度
        angleX += 0.02;
        angleY += 0.03;
        angleZ += 0.01;
        
        // 控制帧率
        this_thread::sleep_for(chrono::milliseconds(50));
    }
    
    return 0;
}
