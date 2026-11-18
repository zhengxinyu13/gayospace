#!/usr/bin/env python3
"""Win32 平台 API 扫描（TS-SW 第 11 节 / DoD 2.3；R-03 重写为单遍词法状态机）。

扫描 src/core、src/gui、src/cli、src/app（platform/ 为唯一豁免目录）。

R-03 修复的三个缺陷（fixture 固化在 tests/tools/test_check_win32_api.py）：
  1. 行号错位：旧实现先删块注释导致换行丢失 → 状态机按字符处理，输出与输入
     逐字符等长、换行原样保留，行号天然对齐；
  2. 大小写漏检：include 规则加 re.IGNORECASE（C++ 头文件在 Windows 大小写不敏感）；
  3. 同行被吞：旧实现先删 // 注释再剥字符串，"http://" 把后半行当注释吞掉 →
     状态机单遍处理，注释/字符串/原始字符串互不干扰。

命中即退出码 1，输出 文件:行号 与命中规则。
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCAN_DIRS = [REPO_ROOT / "src" / d for d in ("core", "gui", "cli", "app")]
EXTENSIONS = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx"}

# ---- 规则表（TS-SW 第 11 节；新增平台专属调用须先改 platform/ 立项并同步本表）----
RE_FORBIDDEN_INCLUDE = re.compile(
    r"#\s*include\s*[<\"]\s*("
    r"windows\.h|windowsx\.h|winsock2\.h|winreg\.h|shlobj\.h|tchar\.h"
    r"|process\.h|io\.h|direct\.h|conio\.h"
    r")\s*[>\"]",
    re.IGNORECASE,  # R-03：Windows 文件系统大小写不敏感，<Windows.h> 同样违规
)
CALL_PATTERNS = [
    ("CreateFile(W/A)", re.compile(r"\bCreateFile(?:W|A)?\s*\(")),
    ("RegOpenKey", re.compile(r"\bRegOpenKey(?:W|A|Ex)?\s*\(")),
    ("MessageBox(W/A)", re.compile(r"\b(?:\w+::)?MessageBox(?:W|A)?\s*\(")),
    ("CoInitialize", re.compile(r"\bCoInitialize(?:Ex)?\s*\(")),
    ("WaitForSingleObject", re.compile(r"\bWaitForSingleObject(?:Ex)?\s*\(")),
    ("GetLastError", re.compile(r"(?:::)?\bGetLastError\s*\(")),
]

RE_RAW_STRING_OPEN = re.compile(r'R"([^ ()\\\t\v\f\r\n]*)\(')


def strip_lexical(text: str) -> str:
    """单遍词法状态机：剥离注释与字面量内容。

    输出与输入逐字符等长、换行原样保留（R-03 缺陷 1 的修复前提），
    注释与字面量内容替换为空格。
    """
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            # 行注释：清空内容，保留换行
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and nxt == "*":
            # 块注释：逐字符清空但保留换行
            out.append(" ")
            out.append(" ")
            i += 2
            while i < n:
                if text[i] == "*" and i + 1 < n and text[i + 1] == "/":
                    out.append(" ")
                    out.append(" ")
                    i += 2
                    break
                out.append(text[i] if text[i] == "\n" else " ")
                i += 1
            # 未闭合的块注释吞到 EOF（容忍病态输入）
        elif c == "R" and nxt == '"':
            # 原始字符串 R"delim( ... )delim"
            m = RE_RAW_STRING_OPEN.match(text, i)
            if m:
                closer = ")" + m.group(1) + '"'
                end = text.find(closer, m.end())
                seg_end = n if end == -1 else end + len(closer)
                while i < seg_end:
                    out.append(text[i] if text[i] == "\n" else " ")
                    i += 1
            else:
                out.append(c)
                i += 1
        elif c == '"':
            # 字符串字面量：保留引号，内容清空（R-03 缺陷 3 的修复前提）
            out.append(c)
            i += 1
            while i < n:
                ch = text[i]
                if ch == "\\" and i + 1 < n:
                    out.append(" ")
                    out.append(" ")
                    i += 2
                    continue
                out.append(ch if ch == "\n" else " ")
                i += 1
                if ch == '"':
                    break
        elif c == "'":
            # 字符字面量：同上
            out.append(c)
            i += 1
            while i < n:
                ch = text[i]
                if ch == "\\" and i + 1 < n:
                    out.append(" ")
                    out.append(" ")
                    i += 2
                    continue
                out.append(ch if ch == "\n" else " ")
                i += 1
                if ch == "'" or ch == "\n":
                    break
        else:
            out.append(c)
            i += 1
    return "".join(out)


def scan_text(text: str) -> list[tuple[int, str, str]]:
    """扫描单份源码文本，返回 (行号, 命中类别, 规则名) 列表。供单测直接调用。"""
    cleaned = strip_lexical(text)
    hits: list[tuple[int, str, str]] = []
    for lineno, line in enumerate(cleaned.splitlines(), start=1):
        if RE_FORBIDDEN_INCLUDE.search(line):
            hits.append((lineno, "include", "禁用的 Win32 头文件包含"))
        for name, pattern in CALL_PATTERNS:
            if pattern.search(line):
                hits.append((lineno, "call", f"禁用的平台 API 调用形态（{name}）"))
    return hits


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
        for lineno, kind, rule in scan_text(raw):
            violations.append(f"{path.relative_to(REPO_ROOT)}:{lineno}: {rule}")

    if violations:
        print("Win32 平台 API 扫描不通过（业务代码禁止平台锁死，TS-SW 第 11 节）：")
        for v in violations:
            print(f"  {v}")
        return 1

    print("Win32 平台 API 扫描通过：业务代码零平台专属调用。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
