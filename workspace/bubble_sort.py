def bubble_sort(arr):
    n = len(arr)
    for i in range(n):
        swapped = False
        # Last i elements are already sorted
        for j in range(n - i - 1):
            if arr[j] > arr[j + 1]:
                arr[j], arr[j + 1] = arr[j + 1], arr[j]
                swapped = True
        # Early exit if no swaps occurred in this pass
        if not swapped:
            break
    return arr

if __name__ == "__main__":
    test_case = [64, 34, 25, 12, 22, 11, 90]
    print("Original array:", test_case)
    print("Sorted array:", bubble_sort(test_case))