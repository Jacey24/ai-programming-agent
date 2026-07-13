import heapq
from collections import deque


def dijkstra(graph, start, end=None):
    """
    Dijkstra算法实现单源最短路径（适用于非负权重的有向/无向图）
    :param graph: 邻接表表示的图，格式为 {节点: [(邻接节点, 边权重), ...]}
    :param start: 起点节点
    :param end: 终点节点（可选），指定后返回到该点的最短距离和路径
    :return: 若指定end则返回(最短距离, 路径列表)，否则返回所有节点的最短距离字典
    """
    # 初始化距离字典，所有节点初始距离为无穷大
    distances = {node: float('inf') for node in graph}
    distances[start] = 0
    # 前驱节点字典，用于路径重建
    predecessors = {node: None for node in graph}
    # 优先队列，存储(当前累计距离, 当前节点)
    priority_queue = [(0, start)]
    # 已处理节点集合，避免重复处理
    visited = set()

    while priority_queue:
        current_distance, current_node = heapq.heappop(priority_queue)
        
        # 跳过已处理的节点
        if current_node in visited:
            continue
        visited.add(current_node)
        
        # 提前终止：到达终点时无需继续处理
        if current_node == end:
            break
        
        # 遍历所有邻接节点，尝试更新最短距离
        for neighbor, weight in graph[current_node]:
            new_distance = current_distance + weight
            # 发现更短路径时更新距离和前驱节点
            if new_distance < distances[neighbor]:
                distances[neighbor] = new_distance
                predecessors[neighbor] = current_node
                heapq.heappush(priority_queue, (new_distance, neighbor))
    
    # 若指定终点，重建并返回路径
    if end is not None:
        path = []
        current = end
        while current is not None:
            path.append(current)
            current = predecessors[current]
        path.reverse()
        # 路径不可达的情况
        if path[0] != start:
            return float('inf'), None
        return distances[end], path
    
    return distances


def bfs_shortest_path(graph, start, end):
    """
    BFS实现无权图的最短路径（所有边权重视为1，天然保证最短路径）
    :param graph: 邻接表表示的图，格式为 {节点: [邻接节点, ...]}
    :param start: 起点节点
    :param end: 终点节点
    :return: (最短路径长度, 路径列表)，不可达时返回(None, None)
    """
    # 初始化距离和前驱字典
    distances = {node: float('inf') for node in graph}
    distances[start] = 0
    predecessors = {node: None for node in graph}
    # BFS队列
    queue = deque([start])
    visited = set([start])

    while queue:
        current_node = queue.popleft()
        
        # 到达终点提前终止
        if current_node == end:
            break
        
        # 遍历邻接节点
        for neighbor in graph[current_node]:
            if neighbor not in visited:
                visited.add(neighbor)
                distances[neighbor] = distances[current_node] + 1
                predecessors[neighbor] = current_node
                queue.append(neighbor)
    
    # 处理不可达情况
    if distances[end] == float('inf'):
        return None, None
    
    # 重建路径
    path = []
    current = end
    while current is not None:
        path.append(current)
        current = predecessors[current]
    path.reverse()

    return distances[end], path


if __name__ == "__main__":
    # 测试Dijkstra算法（带权图场景）
    print("=== Dijkstra算法测试（带权图） ===")
    weighted_graph = {
        'A': [('B', 1), ('C', 4)],
        'B': [('A', 1), ('C', 2), ('D', 5)],
        'C': [('A', 4), ('B', 2), ('D', 1)],
        'D': [('B', 5), ('C', 1)]
    }
    start_node, end_node = 'A', 'D'
    dist, spath = dijkstra(weighted_graph, start_node, end_node)
    print(f"从 {start_node} 到 {end_node} 的最短距离: {dist}")
    print(f"最短路径: {' -> '.join(spath)}")

    # 测试单源到所有节点的最短距离
    all_dist = dijkstra(weighted_graph, 'A')
    print("\n从A出发到所有节点的最短距离:")
    for node, d in all_dist.items():
        print(f"A → {node}: {d}")

    # 测试BFS算法（无权图场景）
    print("\n=== BFS最短路径测试（无权图） ===")
    unweighted_graph = {
        'A': ['B', 'C'],
        'B': ['A', 'D', 'E'],
        'C': ['A', 'F'],
        'D': ['B'],
        'E': ['B', 'F'],
        'F': ['C', 'E']
    }
    start, end = 'A', 'F'
    path_len, bpath = bfs_shortest_path(unweighted_graph, start, end)
    print(f"从 {start} 到 {end} 的最短路径长度: {path_len}")
    print(f"最短路径: {' -> '.join(bpath)}")
