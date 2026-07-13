def quick_sort(arr):
    if len(arr) <= 1:
        return arr
    pivot = arr[len(arr) // 2]
    left = [x for x in arr if x < pivot]
    middle = [x for x in arr if x == pivot]
    right = [x for x in arr if x > pivot]
    return quick_sort(left) + middle + quick_sort(right)

if __name__ == "__main__":
    test_arr = [3,6,8,10,1,2,1]
    print("原数组:", test_arr)
    sorted_arr = quick_sort(test_arr)
    print("排序后:", sorted_arr)
    assert sorted_arr == [1,1,2,3,6,8,10], "排序结果错误"
    print("快速排序算法验证通过！")