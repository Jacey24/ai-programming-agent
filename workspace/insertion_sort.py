def insertion_sort(arr):
    """插入排序实现，对输入列表进行原地升序排序"""
    # 从第二个元素开始遍历（第一个元素初始视为已排序部分）
    for i in range(1, len(arr)):
        current = arr[i]  # 当前需要插入的元素
        # 从已排序部分的末尾向前比较
        j = i - 1
        while j >= 0 and current < arr[j]:
            # 将比当前元素大的元素向后移动
            arr[j + 1] = arr[j]
            j -= 1
        # 找到正确位置插入当前元素
        arr[j + 1] = current
    return arr


if __name__ == "__main__":
    # 测试用例
    test_list = [12, 11, 13, 5, 6]
    print("排序前:", test_list)
    insertion_sort(test_list)
    print("排序后:", test_list)
    
    # 额外测试空列表和单元素列表
    empty_list = []
    insertion_sort(empty_list)
    print("空列表排序后:", empty_list)
    
    single_list = [42]
    insertion_sort(single_list)
    print("单元素列表排序后:", single_list)
