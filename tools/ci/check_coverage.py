#!/usr/bin/env python3
"""覆盖率门禁（R-02：DoD 2.3 阈值的自动化执行点，此前只查文件存在=假绿）。

解析 OpenCppCoverage 产出的 Cobertura 报告，按 TS-SW 第 12 节目录阈值断言：
  core/protocol、core/pipeline ≥ 80%；core/serial、core/session ≥ 60%；其余 core 子目录 ≥ 80%。

判定规则：
  - coverage.xml 缺失 / 不可解析 / 有效行数为 0 → 失败（空报告即假绿，--cover_children 缺失的典型症状）；
  - 报告中出现的 core 包逐个断言阈值，不达标 → 失败并在输出中列出实测值；
  - TS 第 12 节清单中尚未出现在报告里的目录（尚无代码）→ 显式提示跳过，不算失败。
"""
from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# (目录关键字, 行覆盖率阈值) —— TS-SW 第 12 节锁定口径
THRESHOLDS: list[tuple[str, float]] = [
    ("core/protocol", 0.80),
    ("core/pipeline", 0.80),
    ("core/serial", 0.60),
    ("core/session", 0.60),
]
DEFAULT_THRESHOLD = 0.80  # 其余 core 子目录（applogic 等）


def norm(name: str) -> str:
    return name.replace("\\", "/")


def main() -> int:
    path = Path("coverage.xml")
    if not path.exists():
        print("覆盖率门禁失败：coverage.xml 不存在")
        return 1
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as e:
        print(f"覆盖率门禁失败：coverage.xml 解析失败：{e}")
        return 1

    packages = {p.get("name", ""): p for p in root.iter("package")}
    print(f"coverage.xml 汇总 line-rate = {root.get('line-rate')}")

    # 有效行数按包内 <line> 元素计数（Cobertura 的 lines-valid 在包级不可靠）
    total_lines = sum(len(list(pkg.iter("line"))) for pkg in packages.values())
    if total_lines == 0:
        print(
            "覆盖率门禁失败：报告有效行数为 0——"
            "典型症状是 OpenCppCoverage 缺 --cover_children（只统计到 ctest.exe 自身）或测试未命中 core"
        )
        return 1

    failures: list[str] = []
    covered_pkgs = 0
    for pkg_name, pkg in sorted(packages.items()):
        valid = len(list(pkg.iter("line")))
        if valid == 0:
            continue
        covered_pkgs += 1
        rate = float(pkg.get("line-rate", "0"))
        thr = DEFAULT_THRESHOLD
        for key, t in THRESHOLDS:
            if key in norm(pkg_name):
                thr = t
                break
        status = "OK" if rate >= thr else "FAIL"
        print(f"  {pkg_name}: line-rate={rate:.4f}（阈值 {thr:.2f}，有效行 {valid}）{status}")
        if rate < thr:
            failures.append(f"{pkg_name}({rate:.2%}<{thr:.0%})")

    missing = [
        key
        for key, _ in THRESHOLDS
        if not any(key in norm(n) for n in packages)
    ]
    if missing:
        print(f"  报告中未出现（目录尚无代码，显式跳过不算失败）：{', '.join(missing)}")

    if failures:
        print("覆盖率门禁不通过：" + ", ".join(failures))
        return 1
    print(f"覆盖率门禁通过（{covered_pkgs} 个代码包达标）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
