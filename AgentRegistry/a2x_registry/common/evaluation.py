"""Shared evaluation utilities across search methods."""

from typing import List, Tuple


def compute_set_metrics(
    expected_tools: List[str], found_tools: List[str]
) -> Tuple[list, list, list, float, float, bool]:
    """Compute precision/recall/hit from expected vs found tool lists.

    Returns:
        (correct, missed, wrong, precision, recall, hit)
    """
    expected_set = set(expected_tools)
    found_set = set(found_tools)
    correct = list(expected_set & found_set)
    missed = list(expected_set - found_set)
    wrong = list(found_set - expected_set)
    precision = len(correct) / len(found_tools) if found_tools else 0.0
    recall = len(correct) / len(expected_tools) if expected_tools else 0.0
    hit = len(correct) > 0
    return correct, missed, wrong, precision, recall, hit
