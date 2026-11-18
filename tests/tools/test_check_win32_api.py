#!/usr/bin/env python3
"""check_win32_api.py 的固化单测（R-03：评审实测的三段缺陷 fixture + 回归项）。

依赖零第三方库：直接 python 运行，断言失败即非零退出。
CI 步骤：python tests/tools/test_check_win32_api.py
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "ci"))
import check_win32_api as cw  # noqa: E402

_results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: str = "") -> None:
    _results.append((name, cond, detail))


# ---- fixture 1（R-03 缺陷 1）：块注释跨行后接 include，行号必须对齐 ----
SRC_LINE_NUMBER = "/* 多行\n   注释\n   继续 */\n\n#include <windows.h>\n"
hits = cw.scan_text(SRC_LINE_NUMBER)
check(
    "块注释跨行后 include 行号对齐（应为第 5 行）",
    hits == [(5, "include", "禁用的 Win32 头文件包含")],
    f"实际 {hits}",
)

# ---- fixture 2（R-03 缺陷 2）：大小写变体必须命中 ----
hits = cw.scan_text("#include <Windows.h>\n")
check("include 大小写变体 <Windows.h> 命中", len(hits) == 1 and hits[0][0] == 1, f"实际 {hits}")
hits = cw.scan_text("#include <TCHAR.H>\n")
check("include 大小写变体 <TCHAR.H> 命中", len(hits) == 1, f"实际 {hits}")

# ---- fixture 3（R-03 缺陷 3）：字符串里的 URL 不得吞掉同行的违规调用 ----
SRC_URL_LINE = 'qDebug() << "http://x"; CreateFileA(a, b);\n'
hits = cw.scan_text(SRC_URL_LINE)
check(
    "字符串含 http:// 时同行 CreateFileA 仍命中（且 URL 不误报）",
    len(hits) == 1 and hits[0][0] == 1 and "CreateFile" in hits[0][2],
    f"实际 {hits}",
)

# ---- 回归项：原始字符串内的 include 文本不误报 ----
SRC_RAW = 'auto s = R"x(/* not a comment */ #include <windows.h>)x";\n'
check("原始字符串内的 include 文本零误报", cw.scan_text(SRC_RAW) == [], f"实际 {cw.scan_text(SRC_RAW)}")

# ---- 回归项：注释与字符串中的 API 名不误报 ----
SRC_CLEAN = (
    "// CreateFileW( 出现在注释里\n"
    "/* GetLastError( 出现在块注释里 */\n"
    'const char* tip = "MessageBoxW( 不在代码里";\n'
    "int main() { return 0; }\n"
)
check("注释/字符串中的 API 名零误报", cw.scan_text(SRC_CLEAN) == [], f"实际 {cw.scan_text(SRC_CLEAN)}")

# ---- 回归项：等长性（行号对齐的结构前提） ----
sample = "a/*x\ny*/b//z\nw = \"s\\\"t\";\n"
check("剥离输出与输入逐字符等长", len(cw.strip_lexical(sample)) == len(sample))
check("剥离输出换行数与输入一致", cw.strip_lexical(sample).count("\n") == sample.count("\n"))

failed = [r for r in _results if not r[1]]
for name, ok, detail in _results:
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else f"  <- {detail}"))
print(f"{len(_results) - len(failed)}/{len(_results)} passed")
sys.exit(1 if failed else 0)
