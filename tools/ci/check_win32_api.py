#!/usr/bin/env python3
"""Win32 平台 API 扫描（TS-SW 第 11 节 / DoD 2.3）。

扫描 src/core、src/gui、src/cli、src/app（platform/ 为唯一豁免目录）。
先做词法级剥离（块注释 / 行注释 / 字符串字面量 / 字符字面量），
再匹配 Win32 头文件包含与 API 调用形态，避免误伤注释与日志文本。

命中即退出码 1，输出 文件:行号 与命中规则。
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCAN_DIRS = [REPO_ROOT / "src" / d for d in ("core", "gui", "cli", "app")]
EXTENSIONS = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx"}

# ---- 词法剥离 ----
RE_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
RE_LINE_COMMENT = re.compile(r"//[^\n]*")
RE_STRING_LITERAL = re.compile(r'"(?:\\.|[^"\\\n])*"')
RE_CHAR_LITERAL = re.compile(r"'(?:\\.|[^'\\\n])'")

# ---- 规则表（TS-SW 第 11 节；新增平台专属调用须先改 platform/ 立项并同步本表）----
RE_FORBIDDEN_INCLUDE = re.compile(
    r"#\s*include\s*[<\"]\s*("
    r"windows\.h|windowsx\.h|winsock2\.h|process\.h|io\.h|direct\.h|conio\.h"
    r")\s*[>\"]"
)
CALL_PATTERNS = [
    ("CreateFile(W/A)", re.compile(r"\bCreateFile(?:W|A)?\s*\(")),
    ("RegOpenKey", re.compile(r"\bRegOpenKey(?:W|A|Ex)?\s*\(")),
    ("MessageBox(W/A)", re.compile(r"\b(?:\w+::)?MessageBox(?:W|A)?\s*\(")),
    ("CoInitialize", re.compile(r"\bCoInitialize(?:Ex)?\s*\(")),
    ("WaitForSingleObject", re.compile(r"\bWaitForSingleObject(?:Ex)?\s*\(")),
    ("GetLastError", re.compile(r"(?:::)?\bGetLastError\s*\(")),
]


def strip_lexical(text: str) -> str:
    """剥离注释与字面量；行号保持不变（命中位置对剥离后文本重新计算）。"""
    text = RE_BLOCK_COMMENT.sub("", text)
    text = RE_LINE_COMMENT.sub("", text)
    text = RE_STRING_LITERAL.sub('""', text)
    text = RE_CHAR_LITERAL.sub("''", text)
    return text


def iter_source_files():
    for d in SCAN_DIRS:
        if not d.is_dir():
            continue
        for p in sorted(d.rglob("*")):
            if p.suffix.lower() in EXTENSIONS:
                yield p


def main() -> int:
    violations: list[str] = []
    for path in iter_source_files():
        raw = path.read_text(encoding="utf-8", errors="replace")
        cleaned = strip_lexical(raw)
        for lineno, line in enumerate(cleaned.splitlines(), start=1):
            if RE_FORBIDDEN_INCLUDE.search(line):
                violations.append(f"{path.relative_to(REPO_ROOT)}:{lineno}: 禁用的 Win32 头文件包含")
            for name, pattern in CALL_PATTERNS:
                if pattern.search(line):
                    violations.append(
                        f"{path.relative_to(REPO_ROOT)}:{lineno}: 禁用的平台 API 调用形态（{name}）"
                    )

    if violations:
        print("Win32 平台 API 扫描不通过（业务代码禁止平台锁死，TS-SW 第 11 节）：")
        for v in violations:
            print(f"  {v}")
        return 1

    print("Win32 平台 API 扫描通过：业务代码零平台专属调用。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
