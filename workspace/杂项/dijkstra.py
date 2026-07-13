import heapq

def dijkstra(graph, start):
    """
    Dijkstra算法实现单源最短路径
    
    参数:
        graph: 图的邻接表表示，格式为 {节点: {邻接节点: 权重}}
        start: 起始节点
    
    返回:
        distances: 起始节点到所有节点的最短距离字典
        previous: 最短路径中每个节点的前驱节点字典，用于重建路径
    """
    # 初始化距离字典，所有节点初始距离为无穷大
    distances = {node: float('inf') for node in graph}
    distances[start] = 0
    
    # 前驱节点字典
    previous = {node: None for node in graph}
    
    # 优先队列，存储 (距离, 节点)
    priority_queue = [(0, start)]
    
    while priority_queue:
        current_distance, current_node = heapq.heappop(priority_queue)
        
        # 如果当前距离大于已知最短距离，跳过
        if current_distance > distances[current_node]:
            continue
        
        # 遍历邻接节点
        for neighbor, weight in graph[current_node].items():
            distance = current_distance + weight
            
            # 如果找到更短的路径，更新距离和前驱节点
            if distance < distances[neighbor]:
                distances[neighbor] = distance
                previous[neighbor] = current_node
                heapq.heappush(priority_queue, (distance, neighbor))
    
    return distances, previous

def get_shortest_path(previous, start, end):
    """
    根据前驱节点字典重建从start到end的最短路径
    """
    path = []
    current = end
    
    while current is not None:
        path.append(current)
        current = previous[current]
    
    # 反转路径得到从start到end的顺序
    path.reverse()
    
    # 如果路径起点不是start，说明没有路径
    if path[0] != start:
        return None
    
    return path

# 测试用例
if __name__ == "__main__":
    # 示例图的邻接表表示
    graph = {
        'A': {'B': 1, 'C': 4},
        'B': {'A': 1, 'C': 2, 'D': 5},
        'C': {'A': 4, 'B': 2, 'D': 1},
        'D': {'B': 5, 'C': 1}
    }
    
    start_node = 'A'
    distances, previous = dijkstra(graph, start_node)
    
    print(f"从节点 {start_node} 到各节点的最短距离:")
    for node, distance in distances.items():
        print(f"到 {node}: {distance}")
    
    print("\n最短路径:")
    for node in graph:
        if node != start_node:
            path = get_shortest_path(previous, start_node, node)
            print(f"到 {node}: {' -> '.join(path)} (距离: {distances[node]})")
