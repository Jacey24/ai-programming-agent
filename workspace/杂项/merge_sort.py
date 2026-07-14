def merge_sort(arr):
    """归并排序算法实现"""
    if len(arr) <= 1:
        return arr
    
    # 分割数组为左右两部分
    mid = len(arr) // 2
    left = merge_sort(arr[:mid])
    right = merge_sort(arr[mid:])
    
    # 合并有序的左右两部分
    return merge(left, right)

def merge(left, right):
    """合并两个有序数组"""
    result = []
    i = j = 0
    
    # 遍历两个数组，将较小的元素依次放入结果
    while i < len(left) and j < len(right):
        if left[i] <= right[j]:
            result.append(left[i])
            i += 1
        else:
            result.append(right[j])
            j += 1
    
    # 将剩余元素添加到结果
    result.extend(left[i:])
    result.extend(right[j:])
    return result

if __name__ == "__main__":
    # 测试用例
    test_array = [38, 27, 43, 3, 9, 82, 10]
    print("原始数组:", test_array)
    sorted_array = merge_sort(test_array)
    print("排序后数组:", sorted_array)
    
    # 验证排序结果正确性
    expected = [3, 9, 10, 27, 38, 43, 82]
    assert sorted_array == expected, f"排序错误，预期结果: {expected}"
    print("归并排序验证通过！")
