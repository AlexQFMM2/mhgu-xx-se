# MHGU / MHXX Save Editor

Monster Hunter Generations Ultimate（Nintendo Switch）/ Monster Hunter XX
（Nintendo 3DS）存档修改器。当前支持 Citra 的 MHXX（3DS 模拟器）无头 `system`，以及 Yuzu 的
MHGU（NS 模拟器）无头/带36字节头 `system`；不支持实体机原始存档。读取后主界面会持续显示当前
游戏、模拟器和文件格式。

Yuzu 的 MHXX Nintendo Switch Ver. 当前尚未支持。由于它与 MHGU 同属 NS 模拟器 `system`
场景，读取 NS 格式后程序会要求用户明确确认文件来自 MHGU；取消确认不会进入编辑页面。

界面采用与 MH3G、MH4G 统一的中文管理台，左侧切换存档槽、角色、道具箱、装备箱
和猫猫，右侧在同一窗口编辑。防具幻化已经实机验证可用；MHGU 原版武器幻化只会
改变部分界面显示，不会改变实际武器模型，因此修改器不再提供武器外观编辑。

> 修改器不会自动备份，也不会修改游戏的 `system_backup`。请在打开修改器前由用户
> 自行备份完整存档目录。

## 开发进度（2026-09-04）

- MHGU / MHXX 已完成 SQLite 数据迁移、严格校验基础编辑闭环和自动配装器；正式桌面版本为
  `v0.2.1`，Windows portable 由 GitHub Action 自动构建并发布；本版本增加 Citra/Yuzu 模拟器格式
  常驻标识、读取确认提示，并将应用版本号同步为 `0.2.1`。
- 自动配装固定普通武器，支持男女分支、动态技能目标、固定装备、独立孔容量、装珠剪枝、
  暂停/继续/取消、独立倒计时和批量结果刷新；不生成发掘武器/防具或未经确认的非法数据。
- 已接入 MHED 平台，GU 与 XX 统一使用 `game=mhgu`；平台生产已部署严格 36 字节记录校验。
- 本仓库可自动识别并读写 Nintendo Switch MHGU 与 Nintendo 3DS MHXX；XX 当前通过
  文件解析、合成写入和未修改往返测试，尚待修改后放回3DS游戏实测。
- GU/XX 共用经过游戏资源和原版编辑器交叉验证的数据 ID 与角色相对字段模型，但分别
  校验文件尺寸、文件头、槽位基址和每槽边界。
- 不提供整份存档或角色槽的平台转换；跨平台传输仅通过规范化道具箱/装备箱表单完成。

由于 MHGU 非法装备可能导致游戏卡死，任何无法确认合法的猎人装备、护石、装饰珠或猫
数据都会被拒绝写入；发现异常存档槽时自动进入只读保护，不会静默修复原始字节。

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

程序优先从可执行文件旁边的 `data/mhgu.sqlite` 加载 SQLite，缺失时回退到 CSV。可用
`python3 tools/build_sqlite_data.py data` 从已审计 CSV 确定性重建数据库。Windows 打包
脚本会生成包含 Qt/MinGW 运行库、SQLite、manifest 和 data 的 `release/windows/`。

## 使用流程

1. 打开以下任一 `system`：
   - MHXX（3DS）：`4,726,152` 字节；
   - MHGU（NS）：`5,159,064` 字节（无头）或 `5,159,100` 字节（带36字节头）。
2. 从存档 1/2/3 中选择一个已使用槽位。
3. 编辑角色信息、2300 格道具箱、猎人/猫装备箱或最多 84 只猫。
4. 点击“保存修改”原子覆盖当前打开的 `system`；成功后会弹窗显示路径。

切换存档槽、打开其他文件或退出时，如果存在未保存修改，程序会提供“保存 / 放弃 /
取消”。三种文件表示保存时都会保持原尺寸及原有文件头形式；文件名明确为 `system_backup`
的游戏备份会被拒绝，修改器不会直接编辑它。

## 已实现功能

- 角色：名字、游玩时间、金钱、HR、HR Points 和各村点数。
- 道具箱：搜索、非空筛选、单格编辑、添加至首个空位；使用 GU/XX 共用的19 bit编码。
- 猎人装备：20 种可编辑类别、等级、实际 ID、3 个装饰珠、护石技能和孔数。防具孔数
  来自游戏原生 `armorSeriesData`，武器孔数来自 `weaponXXLevelData`，珠子占孔来自
  `decoData`；护石按品级分别校验第一/第二技能的可用性、合法点数范围以及技能点组合
  对应的自然孔数。不存在的
  ID/等级组合、非法护石技能点、DUMMY 珠和占孔超限会阻止应用、导入和保存。
  防具强化等级的完整升级表尚未纳入安全数据；编辑器不允许猜测或改写该字段，
  只接受存档中已有的原始等级记录。
  操虫棍还包含独立猎虫实例字段；规则补齐前只允许原样保留或复制当前装备箱中的
  完整武器记录，禁止无来源生成，平台提交暂不接受操虫棍。
  护石技能 ID 使用游戏 `skillTypeData` 的 206 项存档数组，合法范围直接读取
  `amuletSkillData00..07`；Dex 的 `SklTree_ID` 只用于名称关联，不会写入存档。
- 猎人穿戴缓存：只在装备箱来源记录能唯一确认时同步，否则警告并保持缓存不变。
- 猎人及猫防具幻化：已移除 `【测试】` 标记，并限制为相同部位；武器幻化入口已停用。
- 猫猫：先选倾向，再分别选择行动/技能的 A/B/C 构成；生成槽默认只列同组项目，
  每槽也可开启“自由选择”。固有、准固有、共通和传授槽分开显示，已装备列表只从
  当前已学池选择。A/B/C 生成槽属于已学池，只有另外加入“已装备行动/技能”列表的
  内容才会在游戏菜单中显示；程序不会自动替用户装备。外来、配信或无法识别的结构
  默认逐字节保留，只有主动点击重建才套用普通猫布局。
- 猫猫高级设置可直接编辑四个原始数组和 `0x54..0x57` 四个模式/有效长度字节。
  规则错误以红色列出具体原因；非法或无法确认的结构会阻止应用和保存，不自动修正或
  清除原始数据。技能记忆槽成本尚未确认的项目不会被编辑器改写。

任务/训练/设施状态以及尚未确认的装备引用只读。首版不包含背包、临时袋或角色槽
复制/删除。

道具箱和装备箱可导出带 `MHXX_GU_TRANSFER,1` 版本标识的逻辑 CSV，并在 GU 与 XX 间
双向导入。表单不包含文件头、角色基址或原始记录块，因此不会进行隐式存档转换；导入
先完整校验再一次性写入内存，失败时不改变任何格子。程序继续兼容旧版无版本头的 GU
道具箱和装备箱表单。

配装器支持严格合法的 MHGU/MHXX JSON 导入/导出；导入会校验完整 36 字节 `record_hex`、
性别、武器近战/远程与防具适用性、护石自然规则和每件装备独立孔位，失败时整批不回填。
自动配装窗口还可固定头、胸、腕、腰、腿或护石，固定记录仍必须通过同一严格校验。

## 游戏数据

`data/cn` 和 `data/en` 包含道具、装备、猫装备、58 个支援行动、97 个被动技能及
猫猫合法生成规则。CSV 由 `tools/build_data.py` 确定性生成，不应手工维护；来源、
固定 Dex 哈希和重建方法见 [`data/README.md`](data/README.md)。道具、猎人装备及
猫装备 ID 由 MHXX 游戏 `RomFS/table` 的 GMD 数组和配套计数表确定；Dex 只补
中英文名称及属性。

当前装备类型存档映射为：1 头、2 胸、3 腕、4 腰、5 腿、6 护石；7 大剑、8 片手、
9 锤、10 长枪、11 重弩、12 保留、13 轻弩、14 太刀、15 斩斧、16 铳枪、17 弓、
18 双剑、19 狩猎笛、20 操虫棍、21 盾斧。保留类型 12 不会出现在编辑下拉框中。
GU/XX 不生成 MH4G 的发掘装备数据。

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
./tests/bin/mhgu_core_tests --mhxx-sample /path/to/private/mhxx/system
```

核心始终以完整原始字节缓冲为权威，只补丁已确认字段；未修改的打开/保存必须逐字节
一致。

## 许可与来源

项目使用 GPL-3.0-or-later，见 [LICENSE](LICENSE)。Qt UI 由 GPL-3.0 的 MH3U Save
Editor 界面迁移并保留归属；MHXXSaveEditor 和 Cotto 只用于字段及结果交叉核对。详细
说明见 [ATTRIBUTION.md](ATTRIBUTION.md)。
