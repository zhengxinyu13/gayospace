add_subdirectory(unit)
# cli 测试依赖 gayospace-cli-core（批次①）；core-only 探针不构建 src/cli → 目标不存在，
# 须跳过（R-108，第 30 轮评审：与 unit 的 Qt 冒烟守卫写法一致）
if(NOT GAYOSPACE_CORE_ONLY)
    add_subdirectory(cli)
endif()
# integration/：虚拟串口对捞用例（com0com spike 通过后接入，TS-SW 第 12 节）
# manual/：手工用例脚本与检查单（随测试报告模板搭建）
