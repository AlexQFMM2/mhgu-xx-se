# MHGU / MHXX SQLite 与严格合法配装器

MHGU（Switch）与 MHXX（3DS）统一使用 `game=mhgu` 和同一份 `mhgu.sqlite`。
桌面端只在存档布局解析层区分 NS 与 3DS，装备、护石、装饰珠和配装规则共用。

## 安全边界

- 数据库由可审计 CSV 确定性构建，运行时优先 SQLite，缺失时才回退 CSV。
- 武器等级孔位、防具部位/幻化/天然孔、护石技能范围与孔位阈值、装饰珠占孔均执行严格校验。
- 无法确认的防具强化等级不生成；外部 JSON 只有在完整 36 字节记录已存在于当前装备箱时才允许携带非零强化等级。
- 操虫棍包含独立猎虫实例字段；规则补齐前只允许原样保留/复制当前装备箱记录，禁止无来源生成和平台提交。
- 非法或未知记录禁止修改、批量导入、配装回填、导出、保存和平台提交；发现存档异常时进入只读保护。
- 猫装备、猫行动/技能结构和引用同样采用“无法确认则拒绝写入”，未知字段原样保留。

## 自动配装

自动搜索固定用户选择的普通武器，不包含发掘武器/防具；男女不限时分别搜索。
搜索器使用内存快照、部位 DP、胴系统复制、独立孔容量、有限装珠背包、组合剪枝和稳定指纹去重，最多保留 100 条结果。
技能目标可持续添加，搜索时间 1～60 分钟。窗口非模态，支持暂停、继续、取消和关闭等待线程退出；倒计时由 GUI 定时器独立更新，结果按 350ms 批量刷新。

## 构建与验证

```bash
python3 tools/validate_data.py data
python3 tools/build_sqlite_data.py data --output /tmp/mhgu.sqlite --manifest /tmp/mhgu.json
cmp data/mhgu.sqlite /tmp/mhgu.sqlite
qmake MHGUSaveEditor.pro -o build/Makefile && make -C build -j2
qmake tests/CoreTests.pro -o tests/build/Makefile && make -C tests/build -j2
./tests/bin/mhgu_core_tests
```

平台侧使用同一 SHA-256 manifest 加载只读 SQLite；API、广场和管理端均以 `mhgu` 单一分类处理。
