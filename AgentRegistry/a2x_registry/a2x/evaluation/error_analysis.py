"""
Error Analysis Report Generator for A2X Evaluation Results.

Generates detailed error analysis from evaluation_results.json.
Categorizes errors into:
  A. Did not enter correct category (navigation failure)
  B. Entered correct category but did not select correct tool (selection failure)
"""

import json
import logging
from pathlib import Path
from typing import Dict, List, Any, Tuple

logger = logging.getLogger(__name__)


def load_taxonomy_and_services(results_dir: str) -> Tuple[Dict, Dict, Dict]:
    """Load taxonomy, class definitions, and service data.

    Reads paths from config.json in the results directory.
    Falls back to defaults if config doesn't contain path info.

    Returns:
        (taxonomy, classes, services_dict)
    """
    results_path = Path(results_dir)
    config_path = results_path / 'config.json'

    # Default paths
    taxonomy_path = Path('database/ToolRet_clean/taxonomy/taxonomy.json')
    class_path = Path('database/ToolRet_clean/taxonomy/class.json')
    service_path = Path('database/ToolRet_clean/service.json')

    if config_path.exists():
        with open(config_path, 'r', encoding='utf-8') as f:
            config = json.load(f)

        # Read paths from config if available
        if config.get('taxonomy_path'):
            taxonomy_path = Path(config['taxonomy_path'])
        if config.get('class_path'):
            class_path = Path(config['class_path'])
        elif config.get('taxonomy_path'):
            # Derive class_path from taxonomy_path (same directory)
            class_path = Path(config['taxonomy_path']).parent / 'class.json'
        if config.get('service_path'):
            service_path = Path(config['service_path'])

    with open(taxonomy_path, 'r', encoding='utf-8') as f:
        taxonomy = json.load(f)

    with open(class_path, 'r', encoding='utf-8') as f:
        classes = json.load(f)

    with open(service_path, 'r', encoding='utf-8') as f:
        services = json.load(f)
    services_dict = {s['id']: s for s in services}

    return taxonomy, classes, services_dict


def build_parent_map(taxonomy: Dict) -> Dict[str, str]:
    """Build a mapping from child category to parent category."""
    parent_map = {}
    for cat_id, cat_data in taxonomy.get('categories', {}).items():
        for child_id in cat_data.get('children', []):
            parent_map[child_id] = cat_id
    return parent_map


def get_category_path(cat_id: str, parent_map: Dict, root: str = 'root') -> List[str]:
    """Get full path from root to the given category.

    Returns list like ['root', 'cat_1', 'cat_2', 'cat_id']
    """
    path = [cat_id]
    current = cat_id
    while current in parent_map:
        current = parent_map[current]
        path.insert(0, current)
    # Add root if not already there
    if path[0] != root:
        path.insert(0, root)
    return path


def find_tool_categories(tool_id: str, taxonomy: Dict) -> List[str]:
    """Find all categories that contain a given tool."""
    categories = []
    for cat_id, cat_data in taxonomy.get('categories', {}).items():
        if tool_id in cat_data.get('services', []):
            categories.append(cat_id)
    return categories


def get_category_info(cat_id: str, classes: Dict) -> Dict[str, str]:
    """Get category name and description."""
    cat_info = classes.get('categories', {}).get(cat_id, {})
    return {
        'id': cat_id,
        'name': cat_info.get('name', 'Unknown'),
        'description': cat_info.get('description', 'No description')
    }


def format_category_path(path: List[str], classes: Dict) -> str:
    """Format category path as readable string with names."""
    parts = []
    for cat_id in path:
        info = get_category_info(cat_id, classes)
        parts.append(f"{cat_id}({info['name']})")
    return " → ".join(parts)


def get_tool_info(tool_id: str, services_dict: Dict) -> Dict[str, str]:
    """Get tool name and description."""
    tool = services_dict.get(tool_id, {})
    return {
        'id': tool_id,
        'name': tool.get('name', 'Unknown'),
        'description': tool.get('description', 'No description')
    }


def classify_error(
    missed_tools: List[str],
    visited_category_ids: List[str],
    taxonomy: Dict
) -> Tuple[str, List[str], List[str]]:
    """Classify error into category A or B.

    A: Did not enter correct category (navigation failure)
    B: Entered correct category but did not select correct tool (selection failure)

    Returns:
        (error_type, tools_category_A, tools_category_B)
    """
    visited_set = set(visited_category_ids) if visited_category_ids else set()

    tools_not_visited = []  # Category A
    tools_visited_but_missed = []  # Category B

    for tool_id in missed_tools:
        tool_categories = find_tool_categories(tool_id, taxonomy)

        if not tool_categories:
            # Tool not in any category (shouldn't happen normally)
            tools_not_visited.append(tool_id)
            continue

        # Check if any of the tool's categories were visited
        visited_correct_cat = any(cat in visited_set for cat in tool_categories)

        if visited_correct_cat:
            tools_visited_but_missed.append(tool_id)
        else:
            tools_not_visited.append(tool_id)

    # Determine overall error type
    if tools_not_visited and tools_visited_but_missed:
        error_type = 'mixed'
    elif tools_not_visited:
        error_type = 'A_navigation_failure'
    else:
        error_type = 'B_selection_failure'

    return error_type, tools_not_visited, tools_visited_but_missed


def generate_error_report(results_path: str) -> Dict[str, Any]:
    """Generate error analysis report from evaluation results.

    Args:
        results_path: Path to evaluation_results.json

    Returns:
        Error analysis report dictionary
    """
    results_dir = str(Path(results_path).parent)

    with open(results_path, 'r', encoding='utf-8') as f:
        results = json.load(f)

    # Load taxonomy and services
    taxonomy, classes, services_dict = load_taxonomy_and_services(results_dir)

    # Build parent map for category path lookup
    parent_map = build_parent_map(taxonomy)

    query_metrics = results.get('query_metrics', [])

    # Initialize analysis structure
    analysis = {
        'summary': {
            'total_queries': len(query_metrics),
            'perfect_queries': 0,
            'error_queries': 0,
            'category_A_count': 0,  # Navigation failure
            'category_B_count': 0,  # Selection failure
            'mixed_count': 0,
            'total_missed_tools': 0,
            'total_wrong_tools': 0,
        },
        'error_categories': {
            'A_navigation_failure': [],  # Did not enter correct category
            'B_selection_failure': [],   # Entered but did not select
            'mixed': [],                  # Both A and B
        },
        'detailed_errors': [],  # All errors with full details
    }

    for qm in query_metrics:
        recall = qm.get('recall', 0)

        # Count totals
        missed_tools = qm.get('missed_tools', [])
        wrong_tools = qm.get('wrong_tools', [])
        analysis['summary']['total_missed_tools'] += len(missed_tools)
        analysis['summary']['total_wrong_tools'] += len(wrong_tools)

        # Skip perfect queries
        if recall == 1.0:
            analysis['summary']['perfect_queries'] += 1
            continue

        analysis['summary']['error_queries'] += 1

        # Get query info
        query_id = qm.get('query_id', '')
        query_text = qm.get('query', '')
        visited_category_ids = qm.get('visited_category_ids', [])
        expected_tools = qm.get('expected_tools', [])
        found_tools = qm.get('found_tools', [])
        correct_tools = qm.get('correct_tools', [])

        # Classify error
        error_type, tools_cat_A, tools_cat_B = classify_error(
            missed_tools, visited_category_ids, taxonomy
        )

        # Update category counts
        if error_type == 'A_navigation_failure':
            analysis['summary']['category_A_count'] += 1
        elif error_type == 'B_selection_failure':
            analysis['summary']['category_B_count'] += 1
        else:
            analysis['summary']['mixed_count'] += 1

        # Build detailed error entry
        error_entry = {
            'query_id': query_id,
            'query': query_text,
            'recall': recall,
            'precision': qm.get('precision', 0),
            'error_type': error_type,
            'visited_categories': visited_category_ids,

            # Correct tools info
            'correct_tools_detail': [],

            # Missed tools info (categorized)
            'missed_tools_A_navigation': [],  # Didn't enter correct category
            'missed_tools_B_selection': [],   # Entered but didn't select

            # Wrong tools info
            'wrong_tools_detail': [],
        }

        # Add correct tool details
        for tool_id in expected_tools:
            tool_info = get_tool_info(tool_id, services_dict)
            tool_categories = find_tool_categories(tool_id, taxonomy)
            cat_infos = [get_category_info(cat_id, classes) for cat_id in tool_categories]
            # Get full paths for each category
            cat_paths = []
            for cat_id in tool_categories:
                path = get_category_path(cat_id, parent_map)
                path_str = format_category_path(path, classes)
                cat_info = get_category_info(cat_id, classes)
                cat_paths.append({
                    'path': path,
                    'path_str': path_str,
                    'description': cat_info['description']
                })

            was_found = tool_id in correct_tools

            error_entry['correct_tools_detail'].append({
                'tool_id': tool_id,
                'tool_name': tool_info['name'],
                'tool_description': tool_info['description'][:200] + '...' if len(tool_info['description']) > 200 else tool_info['description'],
                'categories': cat_infos,
                'category_paths': cat_paths,
                'was_found': was_found,
            })

        # Add missed tools with category A (navigation failure)
        for tool_id in tools_cat_A:
            tool_info = get_tool_info(tool_id, services_dict)
            tool_categories = find_tool_categories(tool_id, taxonomy)
            cat_infos = [get_category_info(cat_id, classes) for cat_id in tool_categories]

            error_entry['missed_tools_A_navigation'].append({
                'tool_id': tool_id,
                'tool_name': tool_info['name'],
                'tool_description': tool_info['description'][:200] + '...' if len(tool_info['description']) > 200 else tool_info['description'],
                'correct_categories': cat_infos,
                'reason': 'Category not visited during navigation',
            })

        # Add missed tools with category B (selection failure)
        for tool_id in tools_cat_B:
            tool_info = get_tool_info(tool_id, services_dict)
            tool_categories = find_tool_categories(tool_id, taxonomy)
            cat_infos = [get_category_info(cat_id, classes) for cat_id in tool_categories]

            # Find which correct categories were visited
            visited_set = set(visited_category_ids)
            visited_correct = [cat for cat in tool_categories if cat in visited_set]

            error_entry['missed_tools_B_selection'].append({
                'tool_id': tool_id,
                'tool_name': tool_info['name'],
                'tool_description': tool_info['description'][:200] + '...' if len(tool_info['description']) > 200 else tool_info['description'],
                'correct_categories': cat_infos,
                'visited_correct_categories': visited_correct,
                'reason': 'Category visited but tool not selected',
            })

        # Get visited category paths for A-type errors
        visited_cat_paths = []
        for cat_id in visited_category_ids:
            path = get_category_path(cat_id, parent_map)
            path_str = format_category_path(path, classes)
            cat_info = get_category_info(cat_id, classes)
            visited_cat_paths.append({
                'cat_id': cat_id,
                'path': path,
                'path_str': path_str,
                'description': cat_info['description'][:200] + '...' if len(cat_info['description']) > 200 else cat_info['description']
            })
        error_entry['visited_category_paths'] = visited_cat_paths

        # Add wrong tools details (limit to first 10)
        for tool_id in wrong_tools[:10]:
            tool_info = get_tool_info(tool_id, services_dict)
            tool_categories = find_tool_categories(tool_id, taxonomy)
            cat_infos = [get_category_info(cat_id, classes) for cat_id in tool_categories[:3]]  # Limit categories

            error_entry['wrong_tools_detail'].append({
                'tool_id': tool_id,
                'tool_name': tool_info['name'],
                'tool_description': tool_info['description'][:150] + '...' if len(tool_info['description']) > 150 else tool_info['description'],
                'categories': cat_infos,
            })

        if len(wrong_tools) > 10:
            error_entry['wrong_tools_count'] = len(wrong_tools)
            error_entry['wrong_tools_truncated'] = True

        # Add to appropriate category list
        analysis['error_categories'][error_type].append({
            'query_id': query_id,
            'query': query_text[:100] + '...' if len(query_text) > 100 else query_text,
            'recall': recall,
            'missed_count': len(missed_tools),
            'wrong_count': len(wrong_tools),
        })

        # Add to detailed errors
        analysis['detailed_errors'].append(error_entry)

    return analysis


def format_report_markdown(analysis: Dict[str, Any]) -> str:
    """Format error analysis as markdown report."""
    lines = []
    summary = analysis['summary']

    lines.append("# Error Analysis Report\n")

    # Summary
    lines.append("## Summary\n")
    lines.append("| Metric | Value |")
    lines.append("|--------|-------|")
    lines.append(f"| Total Queries | {summary['total_queries']} |")
    lines.append(f"| Perfect Queries (Recall=1) | {summary['perfect_queries']} |")
    lines.append(f"| Error Queries (Recall<1) | {summary['error_queries']} |")
    lines.append(f"| Category A (Navigation Failure) | {summary['category_A_count']} |")
    lines.append(f"| Category B (Selection Failure) | {summary['category_B_count']} |")
    lines.append(f"| Mixed (A+B) | {summary['mixed_count']} |")
    lines.append("")

    # Error Type Explanation
    lines.append("## Error Categories\n")
    lines.append("- **Category A (Navigation Failure)**: 没有进入正确分类")
    lines.append("- **Category B (Selection Failure)**: 进入正确分类但没有选择正确工具")
    lines.append("")

    # Category A Errors - Detailed
    cat_a_errors = [e for e in analysis['detailed_errors'] if e['error_type'] == 'A_navigation_failure']
    if cat_a_errors:
        lines.append("---")
        lines.append("# Category A: 没有进入正确分类\n")
        for i, error in enumerate(cat_a_errors):
            lines.append(f"## {i+1}. {error['query_id']}\n")
            lines.append(f"**请求内容**: {error['query']}\n")

            # 正确工具
            for tool in error['correct_tools_detail']:
                if not tool['was_found']:
                    lines.append("**正确工具**:")
                    lines.append(f"- ID: `{tool['tool_id']}`")
                    lines.append(f"- 名称: {tool['tool_name']}")
                    lines.append(f"- 描述: {tool['tool_description']}")
                    lines.append("")

                    # 正确工具分类：完整链条+描述
                    if tool.get('category_paths'):
                        lines.append("**正确工具分类**:")
                        for cat_path in tool['category_paths']:
                            lines.append(f"- 路径: {cat_path['path_str']}")
                            lines.append(f"- 描述: {cat_path['description'][:300]}...")
                        lines.append("")

            # 进入的错误工具分类：完整链条+描述
            if error.get('visited_category_paths'):
                lines.append("**进入的错误分类**:")
                # 去重，只显示唯一的分类路径
                seen_cats = set()
                for cat_path in error['visited_category_paths']:
                    if cat_path['cat_id'] not in seen_cats:
                        seen_cats.add(cat_path['cat_id'])
                        lines.append(f"- 路径: {cat_path['path_str']}")
                        lines.append(f"- 描述: {cat_path['description']}")
                        lines.append("")
            else:
                lines.append("**进入的错误分类**: 无\n")

            lines.append("---\n")

    # Category B Errors - Detailed
    cat_b_errors = [e for e in analysis['detailed_errors'] if e['error_type'] == 'B_selection_failure']
    if cat_b_errors:
        lines.append("---")
        lines.append("# Category B: 进入正确分类但没有选择正确工具\n")
        for i, error in enumerate(cat_b_errors):
            lines.append(f"## {i+1}. {error['query_id']}\n")
            lines.append(f"**请求内容**: {error['query']}\n")
            lines.append(f"**访问过的分类**: {error['visited_categories']}\n")

            # 正确工具
            for tool in error['correct_tools_detail']:
                if not tool['was_found']:
                    lines.append("**正确工具**:")
                    lines.append(f"- ID: `{tool['tool_id']}`")
                    lines.append(f"- 名称: {tool['tool_name']}")
                    lines.append(f"- 描述: {tool['tool_description']}")
                    lines.append("")

                    # 正确工具分类
                    if tool['categories']:
                        lines.append("**正确工具分类**:")
                        for cat in tool['categories']:
                            lines.append(f"- ID: `{cat['id']}`")
                            lines.append(f"- 名称: {cat['name']}")
                            lines.append(f"- 描述: {cat['description'][:300]}...")
                        lines.append("")

            # 错误工具
            if error['wrong_tools_detail']:
                lines.append("**错误工具**:")
                for tool in error['wrong_tools_detail'][:3]:
                    lines.append(f"- ID: `{tool['tool_id']}`")
                    lines.append(f"- 名称: {tool['tool_name']}")
                    lines.append(f"- 描述: {tool['tool_description']}")
                    if tool['categories']:
                        cat = tool['categories'][0]
                        lines.append(f"- 分类: `{cat['id']}` ({cat['name']})")
                    lines.append("")
                if len(error['wrong_tools_detail']) > 3:
                    lines.append(f"... 还有 {len(error['wrong_tools_detail']) - 3} 个错误工具\n")
            else:
                lines.append("**错误工具**: 无\n")

            lines.append("---\n")

    # Mixed Errors - Detailed
    mixed_errors = [e for e in analysis['detailed_errors'] if e['error_type'] == 'mixed']
    if mixed_errors:
        lines.append("---")
        lines.append("# Mixed: 混合错误 (部分A + 部分B)\n")
        for i, error in enumerate(mixed_errors):
            lines.append(f"## {i+1}. {error['query_id']}\n")
            lines.append(f"**请求内容**: {error['query']}\n")

            # Category A missed tools
            if error['missed_tools_A_navigation']:
                lines.append("### A类错误 (没有进入正确分类):\n")
                for tool in error['missed_tools_A_navigation']:
                    lines.append("**正确工具**:")
                    lines.append(f"- ID: `{tool['tool_id']}`")
                    lines.append(f"- 名称: {tool['tool_name']}")
                    lines.append(f"- 描述: {tool['tool_description']}")
                    if tool['correct_categories']:
                        lines.append("**正确工具分类**:")
                        for cat in tool['correct_categories']:
                            lines.append(f"- ID: `{cat['id']}`")
                            lines.append(f"- 名称: {cat['name']}")
                    lines.append("")

            # Category B missed tools
            if error['missed_tools_B_selection']:
                lines.append("### B类错误 (进入正确分类但没选择):\n")
                for tool in error['missed_tools_B_selection']:
                    lines.append("**正确工具**:")
                    lines.append(f"- ID: `{tool['tool_id']}`")
                    lines.append(f"- 名称: {tool['tool_name']}")
                    lines.append(f"- 描述: {tool['tool_description']}")
                    if tool['correct_categories']:
                        lines.append("**正确工具分类**:")
                        for cat in tool['correct_categories']:
                            lines.append(f"- ID: `{cat['id']}`")
                            lines.append(f"- 名称: {cat['name']}")
                    lines.append(f"**已访问的正确分类**: {tool['visited_correct_categories']}")
                    lines.append("")

            # 错误工具
            if error['wrong_tools_detail']:
                lines.append("**错误工具**:")
                for tool in error['wrong_tools_detail'][:3]:
                    lines.append(f"- ID: `{tool['tool_id']}`")
                    lines.append(f"- 名称: {tool['tool_name']}")
                    if tool['categories']:
                        cat = tool['categories'][0]
                        lines.append(f"- 分类: `{cat['id']}` ({cat['name']})")
                    lines.append("")

            lines.append("---\n")

    return "\n".join(lines)


def save_error_report(results_dir: str) -> str:
    """Generate and save error report for a results directory.

    Args:
        results_dir: Path to results directory containing evaluation_results.json

    Returns:
        Path to saved report
    """
    results_path = Path(results_dir) / 'evaluation_results.json'
    if not results_path.exists():
        raise FileNotFoundError(f"No evaluation_results.json in {results_dir}")

    analysis = generate_error_report(str(results_path))

    # Save JSON
    json_path = Path(results_dir) / 'error_analysis.json'
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(analysis, f, indent=2, ensure_ascii=False)

    # Save Markdown
    md_path = Path(results_dir) / 'error_analysis.md'
    md_content = format_report_markdown(analysis)
    with open(md_path, 'w', encoding='utf-8') as f:
        f.write(md_content)

    logger.info(f"Analysis Summary:")
    logger.info(f"  Total Queries: {analysis['summary']['total_queries']}")
    logger.info(f"  Error Queries: {analysis['summary']['error_queries']}")
    logger.info(f"  Category A (Navigation): {analysis['summary']['category_A_count']}")
    logger.info(f"  Category B (Selection): {analysis['summary']['category_B_count']}")
    logger.info(f"  Mixed: {analysis['summary']['mixed_count']}")

    return str(md_path)


if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        results_dir = sys.argv[1]
    else:
        # Default to latest results
        results_base = Path('results')
        # Find most recent
        dirs = sorted([d for d in results_base.iterdir() if d.is_dir()],
                     key=lambda x: x.name, reverse=True)
        if dirs:
            results_dir = str(dirs[0])
        else:
            logger.error("No results directory found")
            sys.exit(1)

    logger.info(f"Generating error report for: {results_dir}")
    report_path = save_error_report(results_dir)
    logger.info(f"Report saved to: {report_path}")
