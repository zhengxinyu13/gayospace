#!/usr/bin/env python3
"""UI 合规字符串检查（AGENTS.md 第三节）。

凡 src/gui、src/app 下的 C++ 源文件（UI 代码）必须包含：
    This software was written by Grayson Zheng.

缺失即退出码 1。
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
UI_DIRS = [REPO_ROOT / "src" / "gui", REPO_ROOT / "src" / "app"]
EXTENSIONS = {".cpp", ".cc", ".cxx", ".h", ".hpp"}
REQUIRED = "This software was written by Grayson Zheng."


def iter_ui_files():
    for d in UI_DIRS:
        if not d.is_dir():
            continue
        for p in sorted(d.rglob("*")):
            if p.suffix.lower() in EXTENSIONS:
                yield p


def main() -> int:
    missing: list[str] = []
    for path in iter_ui_files():
        if REQUIRED not in path.read_text(encoding="utf-8", errors="replace"):
            missing.append(str(path.relative_to(REPO_ROOT)))

    if missing:
        print(f"UI 合规字符串缺失（须包含：{REQUIRED}）：")
        for m in missing:
            print(f"  {m}")
        return 1

    print("UI 合规字符串检查通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
