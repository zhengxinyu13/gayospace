#!/usr/bin/env python3
"""覆盖率门禁（R-02 + R-30：DoD 2.3 阈值的自动化执行点）。

R-30 事实：OpenCppCoverage 的 CoberturaExporter 按**模块**（exe/dll）生成 <package>，
包名形如 gayospace-tests-core.exe，目录信息只在 <class filename> 属性里——
按 package 断言 TS-SW 第 12 节的目录阈值永远不会生效。
本脚本改为遍历 <class filename>，按 src/core/<子目录>/ 前缀聚合行数后断言：

  core/protocol、core/pipeline ≥ 80%；core/serial、core/session ≥ 60%；其余 core 子目录 ≥ 80%。
R-41：同一源文件被多个测试模块链入时会重复出现，按 (文件, 行号) 去重取 max 命中；
R-43：src/core/ 根级源文件（无子目录层）归入 "core" 桶，阈值取默认。
serial 首批补充：AUTOMOC 生成文件（*_autogen/moc_*.cpp、mocs_compilation.cpp）
是构建产物而非手写代码，不进覆盖率分母（TS-SW 第 12 节分母 = 手写源码目录）。

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
DEFAULT_THRESHOLD = 0.80  # 其余 core 子目录（applogic 及根级 core 桶等）
# R-43：src/core/ 下允许根级源文件（src/core/Common.cpp），归入 "core" 桶
RE_CORE_PATH = re.compile(r"src/core/(.+)$")


def norm(name: str) -> str:
    return name.replace("\\", "/")


def _subdir_of(filename: str) -> str | None:
    """src/core/<子目录>/xxx.cpp → 子目录名；src/core/xxx.cpp（根级）→ "core"。非 core → None。"""
    # R-58：按路径组件判断 AUTOMOC 生成目录（目录名形如 <target>_autogen）。
    # 不用子串匹配——手写文件名/目录名含 "_autogen" 字样（如 port_autogen_helper.cpp）
    # 不应被静默排除出分母。
    if any(part.endswith("_autogen") for part in norm(filename).split("/")):
        return None
    m = RE_CORE_PATH.search(filename)
    if not m:
        return None
    rest = m.group(1)
    return rest.split("/", 1)[0] if "/" in rest else "core"


def aggregate_by_dir(root: ET.Element) -> dict[str, tuple[int, int]]:
    """按 (规范化 filename, 行号) 去重，各模块取 hits 最大值，再按 src/core/<子目录>/ 聚合。

    R-41：OpenCppCoverage 每个模块（exe/dll）一份 <package>，静态库 gayospace_core 被多个
    测试 exe 链入时，同一源文件会在多个包里各出现一次——直接累加会把共同引用的文件
    重复计数（覆盖的被 0 命中模块腰斩 → 假红）。跨模块语义正确的口径：同一行取 max。
    """
    line_hits: dict[tuple[str, int], int] = {}
    for cls in root.iter("class"):
        filename = norm(cls.get("filename", ""))
        if _subdir_of(filename) is None:
            continue  # 非 core 范围（gui/cli 等）不在覆盖率分母内
        for line in cls.iter("line"):
            key = (filename, int(line.get("number", "0")))
            hits = int(line.get("hits", "0"))
            if hits > line_hits.get(key, -1):
                line_hits[key] = hits
    stats: dict[str, list[int]] = {}
    for (filename, _), hits in line_hits.items():
        bucket = stats.setdefault(_subdir_of(filename), [0, 0])
        bucket[0] += 1
        if hits > 0:
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
