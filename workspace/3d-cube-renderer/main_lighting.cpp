#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

using namespace std;

// 屏幕尺寸
const int WIDTH = 80;
const int HEIGHT = 40;

// 3D点结构
struct Point3D {
    double x, y, z;
};

// 立方体的8个顶点
const vector<Point3D> vertices = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}
};

// 6个面（每个面由4个顶点索引组成）
const vector<vector<int>> faces = {
    {0, 1, 2, 3}, // 前面 (z = -1)
    {4, 5, 6, 7}, // 后面 (z = 1)
    {0, 1, 5, 4}, // 下面 (y = -1)
    {2, 3, 7, 6}, // 上面 (y = 1)
    {0, 3, 7, 4}, // 左面 (x = -1)
    {1, 2, 6, 5}  // 右面 (x = 1)
};

// 每个面的法线（归一化）
const vector<Point3D> faceNormals = {
    {0, 0, -1}, // 前面
    {0, 0, 1},  // 后面
    {0, -1, 0}, // 下面
    {0, 1, 0},  // 上面
    {-1, 0, 0}, // 左面
    {1, 0, 0}   // 右面
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

// 投影到2D屏幕坐标
struct Point2D {
    int x, y;
    double depth;
};

Point2D project(const Point3D& p, double fov, double viewerDist) {
    double factor = fov / (viewerDist + p.z);
    int screenX = (int)(p.x * factor + WIDTH / 2);
    int screenY = (int)(-p.y * factor + HEIGHT / 2);
    return {screenX, screenY, p.z};
}

// 根据光照强度选择ASCII字符
char getLightChar(double intensity) {
    // intensity 范围: -1 到 1, 我们将其映射到 0 到 1
    double normalized = (intensity + 1.0) / 2.0;
    // 限制范围
    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;
    
    // 根据亮度选择字符
    if (normalized > 0.8) return '@';
    if (normalized > 0.6) return 'O';
    if (normalized > 0.4) return 'o';
    if (normalized > 0.2) return '.';
    return ' ';
}

// 绘制填充多边形（使用扫描线算法简化版）
void drawFilledFace(vector<vector<char>>& screen, vector<vector<double>>& zbuffer,
                    const vector<Point2D>& points, char ch) {
    if (points.size() < 3) return;
    
    // 找到边界框
    int minY = HEIGHT, maxY = 0;
    int minX = WIDTH, maxX = 0;
    for (const auto& p : points) {
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
    }
    
    // 限制在屏幕范围内
    if (minY < 0) minY = 0;
    if (maxY >= HEIGHT) maxY = HEIGHT - 1;
    if (minX < 0) minX = 0;
    if (maxX >= WIDTH) maxX = WIDTH - 1;
    
    // 对于每一行，找到多边形与扫描线的交点
    for (int y = minY; y <= maxY; y++) {
        vector<int> intersections;
        
        // 遍历多边形的每条边
        for (size_t i = 0; i < points.size(); i++) {
            const auto& p1 = points[i];
            const auto& p2 = points[(i + 1) % points.size()];
            
            // 检查扫描线是否与边相交
            if ((p1.y <= y && p2.y > y) || (p2.y <= y && p1.y > y)) {
                double t = (double)(y - p1.y) / (double)(p2.y - p1.y);
                int xIntersect = (int)(p1.x + t * (p2.x - p1.x));
                if (xIntersect >= minX && xIntersect <= maxX) {
                    intersections.push_back(xIntersect);
                }
            }
        }
        
        // 排序交点
        sort(intersections.begin(), intersections.end());
        
        // 填充交点之间的像素
        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            int xStart = intersections[i];
            int xEnd = intersections[i + 1];
            for (int x = xStart; x <= xEnd; x++) {
                if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
                    // 简单深度测试：使用面的平均深度
                    double avgDepth = 0;
                    for (const auto& p : points) {
                        avgDepth += p.depth;
                    }
                    avgDepth /= points.size();
                    
                    if (avgDepth > zbuffer[y][x]) {
                        screen[y][x] = ch;
                        zbuffer[y][x] = avgDepth;
                    }
                }
            }
        }
    }
}

int main() {
    double angleX = 0, angleY = 0, angleZ = 0;
    
    // 平行光源方向（从右上方照射）
    Point3D lightDir = {1.0, 1.0, 1.0};
    // 归一化
    double lightLen = sqrt(lightDir.x * lightDir.x + lightDir.y * lightDir.y + lightDir.z * lightDir.z);
    lightDir.x /= lightLen;
    lightDir.y /= lightLen;
    lightDir.z /= lightLen;
    
    while (true) {
        // 创建屏幕缓冲和z-buffer
        vector<vector<char>> screen(HEIGHT, vector<char>(WIDTH, ' '));
        vector<vector<double>> zbuffer(HEIGHT, vector<double>(WIDTH, -999.0));
        
        // 旋转所有顶点
        vector<Point3D> rotatedVertices;
        for (const auto& v : vertices) {
            Point3D r = rotateX(v, angleX);
            r = rotateY(r, angleY);
            r = rotateZ(r, angleZ);
            rotatedVertices.push_back(r);
        }
        
        // 旋转法线（只旋转方向，不平移）
        vector<Point3D> rotatedNormals;
        for (const auto& n : faceNormals) {
            Point3D r = rotateX(n, angleX);
            r = rotateY(r, angleY);
            r = rotateZ(r, angleZ);
            rotatedNormals.push_back(r);
        }
        
        // 计算每个面的光照强度并绘制
        for (size_t i = 0; i < faces.size(); i++) {
            // 计算面法线与光源方向的点积
            const auto& normal = rotatedNormals[i];
            double intensity = normal.x * lightDir.x + normal.y * lightDir.y + normal.z * lightDir.z;
            
            // 只绘制面向光源的面（intensity > 0）
            if (intensity > 0) {
                // 获取面的顶点
                vector<Point2D> projectedPoints;
                for (int idx : faces[i]) {
                    projectedPoints.push_back(project(rotatedVertices[idx], 30.0, 5.0));
                }
                
                // 根据光照强度选择字符
                char ch = getLightChar(intensity);
                
                // 绘制填充面
                drawFilledFace(screen, zbuffer, projectedPoints, ch);
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
