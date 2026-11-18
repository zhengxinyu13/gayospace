#!/usr/bin/env python3
"""覆盖率门禁（R-02 + R-30：DoD 2.3 阈值的自动化执行点）。

R-30 事实：OpenCppCoverage 的 CoberturaExporter 按**模块**（exe/dll）生成 <package>，
包名形如 gayospace-tests-core.exe，目录信息只在 <class filename> 属性里——
按 package 断言 TS-SW 第 12 节的目录阈值永远不会生效。
本脚本改为遍历 <class filename>，按 src/core/<子目录>/ 前缀聚合行数后断言：

  core/protocol、core/pipeline ≥ 80%；core/serial、core/session ≥ 60%；其余 core 子目录 ≥ 80%。

判定规则：
  - coverage.xml 缺失 / 不可解析 / core 范围内有效行数为 0 → 失败（空报告即假绿，
    --cover_children 缺失的典型症状）；
  - 各目录聚合覆盖率逐一断言，不达标 → 失败并在输出中列出实测值；
  - TS 第 12 节清单中尚未出现代码的目录 → 显式提示跳过，不算失败。
"""
from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# (目录名, 行覆盖率阈值) —— TS-SW 第 12 节锁定口径；key 匹配 src/core/<key>/ 前缀
THRESHOLDS: list[tuple[str, float]] = [
    ("protocol", 0.80),
    ("pipeline", 0.80),
    ("serial", 0.60),
    ("session", 0.60),
]
DEFAULT_THRESHOLD = 0.80  # 其余 core 子目录（applogic 等）
RE_CORE_SUBDIR = re.compile(r"src/core/([^/]+)/")


def norm(name: str) -> str:
    return name.replace("\\", "/")


def aggregate_by_dir(root: ET.Element) -> dict[str, tuple[int, int]]:
    """遍历 <class filename>，按 src/core/<子目录>/ 聚合 (有效行数, 命中行数)。"""
    stats: dict[str, list[int]] = {}
    for cls in root.iter("class"):
        filename = norm(cls.get("filename", ""))
        m = RE_CORE_SUBDIR.search(filename)
        if not m:
            continue  # 非 core 范围（gui/cli 等）不在覆盖率分母内
        subdir = m.group(1)
        bucket = stats.setdefault(subdir, [0, 0])
        for line in cls.iter("line"):
            bucket[0] += 1
            if int(line.get("hits", "0")) > 0:
                bucket[1] += 1
    return {d: (v[0], v[1]) for d, v in stats.items()}


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

    stats = aggregate_by_dir(root)
    total_valid = sum(v for v, _ in stats.values())
    if total_valid == 0:
        print(
            "覆盖率门禁失败：src/core 范围内有效行数为 0——"
            "典型症状是 OpenCppCoverage 缺 --cover_children（只统计到 ctest.exe 自身）或测试未命中 core"
        )
        return 1

    failures: list[str] = []
    for subdir, (valid, covered) in sorted(stats.items()):
        rate = covered / valid
        thr = dict(THRESHOLDS).get(subdir, DEFAULT_THRESHOLD)
        status = "OK" if rate >= thr else "FAIL"
        print(f"  src/core/{subdir}: 覆盖率={rate:.4f}（阈值 {thr:.2f}，有效行 {valid}）{status}")
        if rate < thr:
            failures.append(f"core/{subdir}({rate:.2%}<{thr:.0%})")

    missing = [key for key, _ in THRESHOLDS if key not in stats]
    if missing:
        print(f"  报告中未出现代码（目录尚无实现，显式跳过不算失败）：{', '.join(missing)}")

    if failures:
        print("覆盖率门禁不通过：" + ", ".join(failures))
        return 1
    print(f"覆盖率门禁通过（{len(stats)} 个 core 目录达标）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
