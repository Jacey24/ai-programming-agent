/*
 * DFS 通用C++模板
 * 适用场景：树/图/网格遍历、回溯搜索（排列、组合、子集、棋盘问题等）
 * 核心思路：递归深入，遇到终止条件回溯，标记已访问节点避免重复遍历
 */
#include <iostream>
#include <vector>
using namespace std;

// -------------- 示例1：网格型DFS（以岛屿数量问题为例）--------------
// 方向数组：上下左右，四方向遍历，如果是八方向就加四个斜向
const int dirs[4][2] = {{-1,0}, {1,0}, {0,-1}, {0,1}};

/**
 * @param grid 输入的网格
 * @param visited 访问标记数组，也可以直接修改原grid做标记（比如把访问过的陆地改成水）省空间
 * @param x 当前行坐标
 * @param y 当前列坐标
 */
void dfs_grid(vector<vector<int>>& grid, vector<vector<bool>>& visited, int x, int y) {
    int m = grid.size();
    int n = grid[0].size();
    // 1. 递归终止条件：坐标越界 / 当前位置已访问 / 当前位置不是要遍历的目标（比如这里是水0就返回）
    if (x < 0 || x >= m || y <0 || y >=n || visited[x][y] || grid[x][y] == 0) {
        return;
    }
    // 2. 标记当前节点为已访问
    visited[x][y] = true;
    // 3. 处理当前节点（如果有需要的逻辑，这里岛屿问题只要标记就行，不需要额外处理）
    // 4. 递归遍历所有邻接节点
    for (auto& d : dirs) {
        int nx = x + d[0];
        int ny = y + d[1];
        dfs_grid(grid, visited, nx, ny);
    }
    // 5. 如果是需要回溯的场景（比如路径搜索），这里要撤销访问标记（回溯），普通遍历不需要
}

int countIslands(vector<vector<int>>& grid) {
    int m = grid.size();
    if (m == 0) return 0;
    int n = grid[0].size();
    vector<vector<bool>> visited(m, vector<bool>(n, false));
    int res = 0;
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            // 遇到未访问的陆地，开始DFS
            if (!visited[i][j] && grid[i][j] == 1) {
                res++;
                dfs_grid(grid, visited, i, j);
            }
        }
    }
    return res;
}

// -------------- 示例2：回溯型DFS（以子集问题为例）--------------
/**
 * @param nums 输入数组
 * @param path 当前路径
 * @param res 结果集
 * @param start 起始遍历位置，避免重复选元素
 */
void dfs_backtrack(vector<int>& nums, vector<int>& path, vector<vector<int>>& res, int start) {
    // 1. 终止条件：这里子集问题每个路径都是合法结果，所以先把当前路径加入结果；如果是组合/排列有长度要求就判断长度
    res.push_back(path);
    // 2. 遍历所有可选的下一个节点
    for (int i = start; i < nums.size(); i++) {
        // 3. 选择当前节点
        path.push_back(nums[i]);
        // 4. 递归深入，下一层从i+1开始（避免重复选）
        dfs_backtrack(nums, path, res, i+1);
        // 5. 回溯，撤销选择
        path.pop_back();
    }
}

vector<vector<int>> subsets(vector<int>& nums) {
    vector<vector<int>> res;
    vector<int> path;
    dfs_backtrack(nums, path, res, 0);
    return res;
}

int main() {
    // 测试网格DFS：岛屿数量
    vector<vector<int>> grid = {
        {1,1,0,0,0},
        {1,1,0,0,0},
        {0,0,1,0,0},
        {0,0,0,1,1}
    };
    cout << "岛屿数量测试：" << countIslands(grid) << endl; // 预期输出3

    // 测试回溯DFS：子集
    vector<int> nums = {1,2,3};
    vector<vector<int>> subs = subsets(nums);
    cout << "子集测试，共" << subs.size() << "个子集：" << endl;
    for (auto& p : subs) {
        cout << "[";
        for (int i = 0; i < p.size(); i++) {
            if (i>0) cout << ",";
            cout << p[i];
        }
        cout << "] ";
    }
    cout << endl;
    return 0;
}