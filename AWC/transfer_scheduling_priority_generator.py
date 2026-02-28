import json
import os
from collections import defaultdict, deque

def topological_sort(nodes, edges):
    # 构建图的邻接表和入度表
    graph = defaultdict(list)
    in_degree = defaultdict(int)
    
    # 初始化入度表
    for node in nodes:
        in_degree[node['id']] = 0
    
    # 构建邻接表和入度表
    for edge in edges:
        source = edge['source']
        target = edge['target']
        graph[source].append(target)
        in_degree[target] += 1
    
    # 使用 Kahn 算法进行拓扑排序
    queue = deque([node for node in nodes if in_degree[node['id']] == 0])
    sorted_nodes = []
    
    while queue:
        current_node = queue.popleft()
        sorted_nodes.append(current_node)
        
        # 减少相邻节点的入度
        for neighbor_id in graph[current_node['id']]:  # neighbor_id 是整数
            # 找到 neighbor_id 对应的节点对象
            neighbor_node = next((node for node in nodes if node['id'] == neighbor_id), None)
            if neighbor_node is not None:
                in_degree[neighbor_id] -= 1
                if in_degree[neighbor_id] == 0:
                    queue.append(neighbor_node)  # 将节点对象加入队列
    
    # 检查是否有环
    if len(sorted_nodes) != len(nodes):
        raise ValueError("The graph contains a cycle, cannot perform topological sort.")
    
    return sorted_nodes

def sort_type_0_nodes(nodes, edges):
    # 对所有节点进行拓扑排序
    sorted_all_nodes = topological_sort(nodes, edges)
    
    # 提取 type 1 节点的拓扑排序结果
    sorted_type_1_nodes = [node for node in sorted_all_nodes if node['type'] == 1]
    
    # 根据 type 1 节点的拓扑排序结果，对 type 0 节点进行排序
    sorted_type_0_nodes = []
    for type_1_node in sorted_type_1_nodes:
        for edge in edges:
            if edge['target'] == type_1_node['id']:
                # 查找 type 0 节点并检查其类型
                type_0_node = next((node for node in nodes if node['id'] == edge['source'] and node['type'] == 0), None)
                if type_0_node is not None and type_0_node not in sorted_type_0_nodes:
                    sorted_type_0_nodes.append(type_0_node)
    
    return sorted_type_0_nodes

# 获取脚本所在目录
script_dir = os.path.dirname(os.path.abspath(__file__))

# 构建 JSON 文件的完整路径
json_file_path = os.path.join(script_dir, 'workflow_graph_info1.json')

# 读取 JSON 文件
with open(json_file_path, 'r') as file:
    data = json.load(file)

# 提取节点和边信息
nodes = data.get('nodes', [])
edges = data.get('edges', [])

# 对 type 0 节点进行排序
sorted_type_0_nodes = sort_type_0_nodes(nodes, edges)

# 将排序结果保存到文件
output_file_path = os.path.join(script_dir, 'sorted_download_components_info.json')
with open(output_file_path, 'w') as file:
    json.dump(sorted_type_0_nodes, file, indent=4)

print(f"Sorted Type 0 Nodes saved to: {output_file_path}")
