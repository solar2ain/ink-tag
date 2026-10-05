from __future__ import annotations


def canonical_board_id(value: str) -> str:
    return value.strip().lower()


def board_ids_equivalent(left: str, right: str) -> bool:
    return canonical_board_id(left) == canonical_board_id(right)
