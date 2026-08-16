# MHGU / MHXX Save Editor

Monster Hunter Generations Ultimate（Nintendo Switch）存档修改器。当前首版支持
MHGU 的 `system`（支持无头及带 36 字节头的版本）；MHXX / Nintendo 3DS 尚无真实
样本，因此暂不开放。

界面采用与 MH3G、MH4G 统一的中文管理台，左侧切换存档槽、角色、道具箱、装备箱
和猫猫，右侧在同一窗口编辑。防具幻化已经实机验证可用；MHGU 原版武器幻化只会
改变部分界面显示，不会改变实际武器模型，因此修改器不再提供武器外观编辑。

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

1. 打开大小为 `5,159,064` 字节（无头）或 `5,159,100` 字节（带 36 字节头）的
   MHGU `system`。
2. 从存档 1/2/3 中选择一个已使用槽位。
3. 编辑角色信息、2300 格道具箱、猎人/猫装备箱或最多 84 只猫。
4. 点击“保存修改”原子覆盖当前打开的 `system`；成功后会弹窗显示路径。

切换存档槽、打开其他文件或退出时，如果存在未保存修改，程序会提供“保存 / 放弃 /
取消”。两种 `system` 保存时都会保持原有文件头形式；文件名明确为 `system_backup`
的游戏备份会被拒绝，修改器不会直接编辑它。

## 已实现功能

- 角色：名字、游玩时间、金钱、HR、HR Points 和各村点数。
- 道具箱：搜索、非空筛选、单格编辑、添加至首个空位；使用 GU 的 19 bit 编码。
- 猎人装备：20 种可编辑类别、等级、实际 ID、3 个装饰珠、护石技能和孔数。防具孔数
  来自游戏原生 `armorSeriesData`，武器孔数来自 `weaponXXLevelData`，珠子占孔来自
  `decoData`；护石按品级分别提示第一/第二技能的可用性及合法点数范围。不存在的
  ID/等级组合、非法护石技能点、DUMMY 珠和占孔超限会显示红色风险提示，但不会阻止
  应用、导入或保存，是否继续由用户决定。
  护石技能 ID 使用游戏 `skillTypeData` 的 206 项存档数组，合法范围直接读取
  `amuletSkillData00..07`；Dex 的 `SklTree_ID` 只用于名称关联，不会写入存档。
- 猎人穿戴缓存：只在装备箱来源记录能唯一确认时同步，否则警告并保持缓存不变。
- 猎人及猫防具幻化：已移除 `【测试】` 标记，并限制为相同部位；武器幻化入口已停用。
- 猫猫：先选倾向，再分别选择行动/技能的 A/B/C 构成；生成槽默认只列同组项目，
  每槽也可开启“自由选择”。固有、准固有、共通和传授槽分开显示，已装备列表只从
  当前已学池选择。外来、配信或无法识别的结构默认逐字节保留，只有主动点击重建才
  套用普通猫布局。
- 猫猫高级设置可直接编辑四个原始数组和 `0x54..0x57` 四个模式/有效长度字节。
  规则错误以红色列出具体原因，但不会自动修正、弹确认框或阻止应用和保存；仅索引、
  文件边界和磁盘写入等安全错误会阻止操作。技能记忆槽成本尚未确认的项目明确显示
  “成本未知”，不使用 A/B/C 生成点数猜测。

任务/训练/设施状态以及尚未确认的装备引用只读。首版不包含背包、临时袋或角色槽
复制/删除；道具箱和装备箱支持 CSV 表单导入导出。

## 游戏数据

`data/cn` 和 `data/en` 包含道具、装备、猫装备、58 个支援行动、97 个被动技能及
猫猫合法生成规则。CSV 由 `tools/build_data.py` 确定性生成，不应手工维护；来源、
固定 Dex 哈希和重建方法见 [`data/README.md`](data/README.md)。道具、猎人装备及
猫装备 ID 由 MHXX 游戏 `RomFS/table` 的 GMD 数组和配套计数表确定；Dex 只补
中英文名称及属性。

当前装备类型存档映射为：1 头、2 胸、3 腕、4 腰、5 腿、6 护石；7 大剑、8 片手、
9 锤、10 长枪、11 重弩、12 保留、13 轻弩、14 太刀、15 斩斧、16 铳枪、17 弓、
18 双剑、19 狩猎笛、20 操虫棍、21 盾斧。保留类型 12 不会出现在编辑下拉框中。
GU 不生成 MH4G 的发掘装备数据。

## 验证

```bash
python3 tools/validate_data.py data
# 本地保留游戏资源导出时可执行逐表 ID 集合复核：
python3 tools/validate_data.py data --game-names /tmp/mhxx-game-resources.json
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
