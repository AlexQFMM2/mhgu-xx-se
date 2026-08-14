# MHGU / MHXX Save Editor

Monster Hunter Generations Ultimate（Nintendo Switch）存档修改器。当前首版支持
MHGU 的无头 `system`；MHXX / Nintendo 3DS 尚无真实样本，因此暂不开放。

界面沿用同工作区 [MH3U Save Editor](../mh3u-se/) 的 Qt 5 / Fusion 卡片风格，已经
实现三槽选择、角色信息、道具箱、装备箱和猫猫编辑。猎人及猫猫幻化统一标为
`【测试】`，其中武器幻化只有存档字段依据，游戏是否读取仍需实机验证。

> 修改器不会自动备份，也不会修改游戏的 `system_backup`。请在打开修改器前由用户
> 自行备份完整存档目录。

## 构建与运行

需要 Qt 5.15、支持 C++17 的编译器和 qmake。

Linux：

```bash
qmake MHGUSaveEditor.pro -o build/Makefile
make -C build -j2
./bin/MHGUSaveEditor
```

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1 `
  -QtBin C:\Qt\5.15.2\mingw81_64\bin
```

程序从可执行文件旁边的 `data/` 或源码根目录的 `data/` 加载 CSV。Windows 打包脚本
会生成包含 Qt/MinGW 运行库和 data 的 `release/windows/`。

## 使用流程

1. 打开大小为 `5,159,064` 字节的 MHGU `system`。
2. 从存档 1/2/3 中选择一个已使用槽位。
3. 编辑角色信息、2300 格道具箱、猎人/猫装备箱或最多 84 只猫。
4. 点击“保存”直接覆盖当前打开的 `system`。

切换存档槽、打开其他文件或退出时，如果存在未保存修改，程序会提供“保存 / 放弃 /
取消”。带 36 字节文件头、大小为 `5,159,100` 的 `system_backup` 会被拒绝。

## 已实现功能

- 角色：名字、游玩时间、金钱、HR、HR Points 和各村点数。
- 道具箱：搜索、非空筛选、单格编辑、添加至首个空位；使用 GU 的 19 bit 编码。
- 猎人装备：20 种类型、等级、实际 ID、3 个装饰珠、护石技能和孔数。
- 猎人穿戴缓存：只在装备箱来源记录能唯一确认时同步，否则警告并保持缓存不变。
- 猎人防具、猫防具及实验性猎人/猫武器幻化：选择限制在相同部位或武器种类，全部
  显示 `【测试】`。
- 猫猫：基本信息、外观、支援行动、被动技能、倾向固有项和生成规律。
- 猫规则检测：等级/EXP、数组连续、装备/已学关系、数量和 generation tier 模式只
  标记“合法 / 不合法”，不会阻止用户写入自定义组合。

任务/训练/设施状态以及尚未确认的装备引用只读。首版不包含背包、临时袋、角色槽
复制/删除或跨平台导入导出。

## 游戏数据

`data/cn` 和 `data/en` 包含道具、装备、猫装备、58 个支援行动、97 个被动技能及
猫猫合法生成规则。CSV 由 `tools/build_data.py` 确定性生成，不应手工维护；来源、
固定 Dex 哈希和重建方法见 [`data/README.md`](data/README.md)。

当前装备类型存档映射为：1 头、2 胸、3 腕、4 腰、5 腿、6 护石、7–20 十四种
武器。GU 不生成 MH4G 的发掘装备数据。

## 验证

```bash
python3 tools/validate_data.py data
qmake tests/CoreTests.pro -o tests/build/Makefile
make -C tests/build -j2
./tests/bin/mhgu_core_tests
QT_QPA_PLATFORM=offscreen ./bin/MHGUSaveEditor --smoke-test
```

本地私有样本可额外验证，样本不会提交：

```bash
./tests/bin/mhgu_core_tests --sample ../research/mhgu/samples/ns/system
```

核心始终以完整原始字节缓冲为权威，只补丁已确认字段；未修改的打开/保存必须逐字节
一致。

## 许可与来源

项目使用 GPL-3.0-or-later，见 [LICENSE](LICENSE)。Qt UI 由 GPL-3.0 的 MH3U Save
Editor 界面迁移并保留归属；MHXXSaveEditor 和 Cotto 只用于字段及结果交叉核对。详细
说明见 [ATTRIBUTION.md](ATTRIBUTION.md)。
