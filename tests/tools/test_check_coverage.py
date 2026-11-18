#!/usr/bin/env python3
"""check_coverage.py 的固化单测（R-30：真实 Cobertura 形态 = package 为模块名、
目录信息在 class filename 里）。零第三方依赖，直接 python 运行。"""
from __future__ import annotations

import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "ci"))
import check_coverage as cc  # noqa: E402

_results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: str = "") -> None:
    _results.append((name, cond, detail))


def doc_from_xml(xml_text: str) -> ET.Element:
    return ET.fromstring(xml_text)


# 真实形态：package 名是模块（exe），目录只在 class 的 filename 里
XML_REAL = """
<coverage line-rate="0.9">
  <package name="gayospace-tests-core.exe" line-rate="0.9">
    <classes>
      <class name="Version" filename="C:\\src\\gayospace\\src\\core\\applogic\\Version.cpp" line-rate="1.0">
        <lines><line hits="1" number="1"/><line hits="1" number="2"/></lines>
      </class>
      <class name="Parser" filename="C:\\src\\gayospace\\src\\core\\protocol\\Parser.cpp" line-rate="0.9">
        <lines><line hits="1" number="1"/><line hits="1" number="2"/><line hits="1" number="3"/>
               <line hits="1" number="4"/><line hits="1" number="5"/><line hits="0" number="6"/>
               <line hits="1" number="7"/><line hits="1" number="8"/><line hits="1" number="9"/>
               <line hits="1" number="10"/></lines>
      </class>
      <class name="Ring" filename="C:\\src\\gayospace\\src\\core\\pipeline\\Ring.cpp" line-rate="1.0">
        <lines><line hits="1" number="1"/><line hits="1" number="2"/><line hits="1" number="3"/><line hits="1" number="4"/></lines>
      </class>
      <class name="Gui" filename="C:\\src\\gayospace\\src\\gui\\MainWindow.cpp" line-rate="1.0">
        <lines><line hits="1" number="1"/></lines>
      </class>
    </classes>
  </package>
</coverage>
"""

# 1. 聚合口径：目录来自 class filename；gui 不进分母
stats = cc.aggregate_by_dir(doc_from_xml(XML_REAL))
check(
    "按 class filename 聚合出 core 子目录",
    set(stats) == {"applogic", "protocol", "pipeline"},
    f"实际 {sorted(stats)}",
)
check("gui 不计入 core 分母", "gui" not in stats, f"实际 {sorted(stats)}")
check("protocol 9/10", stats["protocol"] == (10, 9), f"实际 {stats.get('protocol')}")

# 2. 主流程：真实形态 + 达标 → 通过（protocol 90% ≥ 80%，applogic 100% ≥ 80%）
with tempfile.TemporaryDirectory() as tmp:
    old_cwd = Path.cwd()
    (Path(tmp) / "coverage.xml").write_text(XML_REAL, encoding="utf-8")
    import os
    os.chdir(tmp)
    try:
        rc = cc.main()
    finally:
        os.chdir(old_cwd)
    check("真实形态达标场景 exit 0", rc == 0, f"实际 {rc}")

# 3. serial 目录按 60% 放宽阈值断言：50% → 失败（exit 1）；目录正确聚合
XML_SERIAL_LOW = XML_REAL.replace(
    'src\\core\\pipeline\\Ring.cpp', 'src\\core\\serial\\Serial.cpp'
).replace(
    '<lines><line hits="1" number="1"/><line hits="1" number="2"/><line hits="1" number="3"/><line hits="1" number="4"/></lines>',
    '<lines><line hits="1" number="1"/><line hits="0" number="2"/><line hits="1" number="3"/><line hits="0" number="4"/></lines>',
)
stats = cc.aggregate_by_dir(doc_from_xml(XML_SERIAL_LOW))
check("serial 目录被正确聚合", "serial" in stats, f"实际 {sorted(stats)}")
check("serial 2/4 命中", stats.get("serial") == (4, 2), f"实际 {stats.get('serial')}")
with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp) / "coverage.xml").write_text(XML_SERIAL_LOW, encoding="utf-8")
    import os
    old_cwd = Path.cwd()
    os.chdir(tmp)
    try:
        rc = cc.main()
    finally:
        os.chdir(old_cwd)
    check("serial 50% 低于 60% 阈值 → exit 1", rc == 1, f"实际 {rc}")

failed = [r for r in _results if not r[1]]
for name, ok, detail in _results:
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else f"  <- {detail}"))
print(f"{len(_results) - len(failed)}/{len(_results)} passed")
sys.exit(1 if failed else 0)
