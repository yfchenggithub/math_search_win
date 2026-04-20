# 本地存储模块本轮实现总结（MVP）

## 本轮已完成

1. 新增本地存储服务 `LocalStorageService`：
   - 统一管理 `cache/` 目录与三类文件路径：
     - `cache/favorites.json`
     - `cache/history.json`
     - `cache/settings.json`
   - 统一提供 JSON 读取与写入接口。
   - 写入优先使用 `QSaveFile`，并增加失败兜底处理。
   - 处理目录不存在、文件不存在、JSON 为空、JSON 损坏等场景，避免崩溃。

2. 新增 `FavoritesRepository`：
   - 实现接口：
     - `load()/save()`
     - `contains()/add()/remove()/toggle()`
     - `allIds()/count()/clear()`
   - 收藏数据去重并可持久化。
   - 文件损坏时回退为空集合并可重建有效文件。

3. 新增 `HistoryRepository`：
   - 实现接口：
     - `load()/save()`
     - `addQuery(query, source)`
     - `recentItems(limit)`
     - `clear()/count()`
   - 支持最近搜索去重（同 query 刷新到前面）与固定上限保留。
   - 支持重启后恢复历史。

4. 新增 `SettingsRepository`：
   - 实现接口：
     - `load()/save()`
     - `value()/setValue()`
     - `contains()/remove()/resetToDefaults()`
   - 新增默认设置模型 `AppSettings`，覆盖窗口尺寸、筛选与排序等基础项。
   - 缺失字段时自动补默认值，损坏时回退默认配置。

5. 新增模型与工程接入：
   - 新增模型：
     - `SearchHistoryItem`
     - `AppSettings`
   - 更新主工程与测试 CMake，确保新模块参与编译链接。

6. 新增测试并通过：
   - 新增存储模块测试，覆盖：
     - 目录/文件缺失
     - 空文件
     - JSON 损坏
     - 字段缺失
     - 写入失败
     - 重启后恢复
   - 全量测试通过（含新增测试）。

## 本轮未完成

1. 页面层尚未全面接线：
   - `FavoritesPage` / `SearchPage` / `RecentSearchesPage` / `SettingsPage`
   目前还未全部改为调用新仓库接口。

2. 启动与退出生命周期尚未统一落点：
   - 尚未在应用统一入口完成集中 `load/save` 调度策略。

3. 未实现非本轮范围功能：
   - 授权/激活逻辑
   - 网络同步/云备份/多设备同步
   - 数据加密与复杂迁移系统
   - UI 重构与详情渲染扩展

## 建议下一轮优先事项

1. 在页面层逐步接线 `FavoritesRepository / HistoryRepository / SettingsRepository`。
2. 在主窗口或应用服务层补齐统一生命周期 `load/save`。
3. 增加页面联动测试，验证真实交互下的数据持久化行为。

