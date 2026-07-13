#include <iostream>

// 冒泡排序核心函数（带提前终止优化，升序）
void bubbleSort(int arr[], int n) {
    for (int i = 0; i < n - 1; ++i) {
        bool swapped = false; // 标记本轮是否发生交换
        // 每轮遍历未排序部分，相邻元素比较
        for (int j = 0; j < n - 1 - i; ++j) {
            if (arr[j] > arr[j + 1]) {
                // 交换相邻元素
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
                swapped = true;
            }
        }
        // 本轮无交换，说明数组已有序，提前终止
        if (!swapped) {
            break;
        }
    }
}

// 辅助函数：打印数组
void printArray(int arr[], int n) {
    for (int i = 0; i < n; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << std::endl;
}

int main() {
    // 测试用例数组
    int arr[] = {64, 34, 25, 12, 22, 11, 90};
    int n = sizeof(arr) / sizeof(arr[0]);

    std::cout << "排序前数组: ";
    printArray(arr, n);

    bubbleSort(arr, n);

    std::cout << "排序后数组: ";
    printArray(arr, n);

    return 0;
}