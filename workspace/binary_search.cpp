#include <iostream>
#include <vector>
using namespace std;

// 迭代版本二分查找
int binarySearchIterative(const vector<int>& arr, int target) {
    int left = 0;
    int right = arr.size() - 1;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;  // 防止溢出
        
        if (arr[mid] == target) {
            return mid;
        } else if (arr[mid] < target) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return -1;  // 未找到
}

// 递归版本二分查找
int binarySearchRecursive(const vector<int>& arr, int left, int right, int target) {
    if (left > right) {
        return -1;  // 未找到
    }
    
    int mid = left + (right - left) / 2;  // 防止溢出
    
    if (arr[mid] == target) {
        return mid;
    } else if (arr[mid] < target) {
        return binarySearchRecursive(arr, mid + 1, right, target);
    } else {
        return binarySearchRecursive(arr, left, mid - 1, target);
    }
}

// 测试函数
void runTest(const vector<int>& arr, int target, const string& testName) {
    cout << "=== " << testName << " ===" << endl;
    
    // 迭代版本
    int resultIter = binarySearchIterative(arr, target);
    cout << "迭代版本: 目标值 " << target << " 的下标为 " << resultIter << endl;
    
    // 递归版本
    int resultRecur = binarySearchRecursive(arr, 0, arr.size() - 1, target);
    cout << "递归版本: 目标值 " << target << " 的下标为 " << resultRecur << endl;
    
    cout << endl;
}

int main() {
    // 测试用例1: 正常有序数组
    vector<int> arr1 = {1, 3, 5, 7, 9, 11, 13, 15};
    runTest(arr1, 7, "正常数组 - 目标存在");
    runTest(arr1, 4, "正常数组 - 目标不存在");
    
    // 测试用例2: 边界值
    vector<int> arr2 = {1, 3, 5, 7, 9};
    runTest(arr2, 1, "边界测试 - 查找第一个元素");
    runTest(arr2, 9, "边界测试 - 查找最后一个元素");
    
    // 测试用例3: 单个元素
    vector<int> arr3 = {42};
    runTest(arr3, 42, "单元素数组 - 目标存在");
    runTest(arr3, 10, "单元素数组 - 目标不存在");
    
    // 测试用例4: 空数组
    vector<int> arr4 = {};
    runTest(arr4, 5, "空数组");
    
    // 测试用例5: 重复元素（返回第一个匹配的位置）
    vector<int> arr5 = {2, 4, 4, 4, 6, 8};
    runTest(arr5, 4, "重复元素数组");
    
    return 0;
}
