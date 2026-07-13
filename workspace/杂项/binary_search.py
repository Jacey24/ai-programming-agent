def binary_search(arr, target):
    left, right = 0, len(arr) - 1
    while left <= right:
        # 计算中间位置，避免整数溢出（Python中无此问题，但为通用写法）
        mid = left + (right - left) // 2
        if arr[mid] == target:
            return mid
        elif arr[mid] < target:
            # 目标在右半区间
            left = mid + 1
        else:
            # 目标在左半区间
            right = mid - 1
    # 未找到目标
    return -1


if __name__ == "__main__":
    # 测试用例：有序数组
    test_array = [2, 5, 8, 12, 16, 23, 38, 56, 72, 91]
    
    # 测试1：查找存在的元素
    target1 = 23
    res1 = binary_search(test_array, target1)
    print(f"查找元素 {target1} 的索引: {res1} (预期结果: 5)")
    
    # 测试2：查找不存在的元素
    target2 = 10
    res2 = binary_search(test_array, target2)
    print(f"查找元素 {target2} 的索引: {res2} (预期结果: -1)")
    
    # 测试3：查找首元素
    target3 = 2
    res3 = binary_search(test_array, target3)
    print(f"查找元素 {target3} 的索引: {res3} (预期结果: 0)")
    
    # 测试4：查找尾元素
    target4 = 91
    res4 = binary_search(test_array, target4)
    print(f"查找元素 {target4} 的索引: {res4} (预期结果: 9)")
