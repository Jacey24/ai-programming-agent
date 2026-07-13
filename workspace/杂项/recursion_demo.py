#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
递归标准实现示例
递归核心三要素：
1. 基线条件(Base Case)：递归的终止条件，当满足该条件时直接返回结果，不再递归
2. 递归条件(Recursive Case)：将原问题拆解为规模更小的同类子问题，调用自身求解子问题
3. 递归返回：子问题的解需要能够组合成原问题的解
"""

from typing import List


def factorial(n: int) -> int:
    """
    阶乘计算（递归经典示例）
    数学定义：n! = n * (n-1) * (n-2) * ... * 1，特殊的 0! = 1
    :param n: 非负整数
    :return: n的阶乘结果
    """
    # 基线条件：当n为0或1时，直接返回1，终止递归
    if n == 0 or n == 1:
        return 1
    # 递归条件：n! = n * (n-1)!，将问题拆解为求n-1的阶乘
    return n * factorial(n - 1)


def fibonacci(n: int) -> int:
    """
    斐波那契数列（朴素递归实现，注意：该实现存在大量重复计算，n较大时效率极低）
    数学定义：F(0)=0, F(1)=1, F(n) = F(n-1) + F(n-2) （n≥2）
    :param n: 非负整数，表示数列第n项（从0开始计数）
    :return: 斐波那契数列第n项的值
    """
    # 基线条件
    if n == 0:
        return 0
    if n == 1:
        return 1
    # 递归条件
    return fibonacci(n - 1) + fibonacci(n - 2)


def recursive_sum(numbers: List[int]) -> int:
    """
    递归计算列表元素和
    :param numbers: 整数列表
    :return: 列表所有元素的和
    """
    # 基线条件：空列表的和为0
    if len(numbers) == 0:
        return 0
    # 递归条件：列表和 = 第一个元素 + 剩余元素的和
    return numbers[0] + recursive_sum(numbers[1:])


def reverse_string(s: str) -> str:
    """
    递归反转字符串
    :param s: 待反转的字符串
    :return: 反转后的字符串
    """
    # 基线条件：空字符串或长度为1的字符串反转后是自身
    if len(s) <= 1:
        return s
    # 递归条件：反转字符串 = 最后一个字符 + 剩余部分的反转结果
    return s[-1] + reverse_string(s[:-1])


if __name__ == "__main__":
    # 测试用例
    print("=== 递归示例验证 ===")
    # 阶乘测试
    print(f"5! = {factorial(5)}（预期结果：120）")
    print(f"0! = {factorial(0)}（预期结果：1）")
    # 斐波那契测试
    print(f"斐波那契第10项 = {fibonacci(10)}（预期结果：55）")
    # 列表求和测试
    test_list = [1, 2, 3, 4, 5]
    print(f"列表{test_list}的和 = {recursive_sum(test_list)}（预期结果：15）")
    # 字符串反转测试
    test_str = "hello recursion"
    print(f"字符串\"{test_str}\"反转结果：{reverse_string(test_str)}（预期结果：noisrucer olleh）")