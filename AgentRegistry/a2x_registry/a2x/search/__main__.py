"""CLI entry point: python -m a2x_registry.a2x.search"""

import argparse
import time

from a2x_registry.a2x.search.a2x_search import A2XSearch


def main():
    parser = argparse.ArgumentParser(description="A2X Hierarchical Service Search")
    parser.add_argument("--query", "-q", type=str, help="Search query")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--parallel", "-p", action="store_true", default=True)
    parser.add_argument("--max-workers", "-w", type=int, default=20)
    parser.add_argument("--mode", choices=["get_all", "get_one", "get_important"],
                        default="get_all",
                        help="Search mode: get_all (default), get_one, or get_important")
    args = parser.parse_args()

    query = args.query or "I need to book a flight to Tokyo"

    print(f"\nQuery: {query}")
    print(f"Mode: {'Parallel' if args.parallel else 'Sequential'} "
          f"(max_workers={args.max_workers}), search_mode={args.mode}")
    print("=" * 80)

    start_time = time.time()
    searcher = A2XSearch(max_workers=args.max_workers, parallel=args.parallel, mode=args.mode)
    results, stats = searcher.search(query)
    elapsed_time = time.time() - start_time

    print(f"\nFound {len(results)} services:")
    print("-" * 80)
    for i, result in enumerate(results, 1):
        print(f"\n{i}. {result.name} (ID: {result.id})")
        desc = result.description
        if len(desc) > 200:
            desc = desc[:200] + "..."
        print(f"   {desc}")

    print("\n" + "=" * 80)
    print("Search Statistics:")
    print(f"  Time Elapsed: {elapsed_time:.2f}s")
    print(f"  LLM Calls: {stats.llm_calls}")
    print(f"  Total Tokens: {stats.total_tokens}")
    print(f"  Visited Categories: {len(stats.visited_categories)}")
    print(f"  Pruned Categories: {len(stats.pruned_categories)}")

    if args.verbose:
        print("\nVisited Categories:")
        for cat in stats.visited_categories:
            print(f"  + {cat}")
        print("\nPruned Categories:")
        for cat in stats.pruned_categories:
            print(f"  - {cat}")

    return results, stats


if __name__ == "__main__":
    main()
