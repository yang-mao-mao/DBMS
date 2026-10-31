# 缓冲池测试与修复说明

## 验证结果

2026-10-28，在 Windows / MSYS2 MinGW GCC 15.2.0、CMake 4.1.1、C++20 环境验证：

- 新增 26 项独立缓冲池测试，并接入 CTest；原有 3 项存储测试保持不变。
- 修复前复现 11 项缓冲池测试失败；修复后 Debug、Release 均为 **29/29 通过**。
- Release 额外启用 `-Wall -Wextra -Wpedantic`，构建无警告。
- 随机模型测试使用容量为 1、3、7 的缓冲池，每种执行 1000 步，共 3000 步。
  固定种子为 `20261028 + pool_size`，可以重复复现。
- 测试数据库使用系统临时目录中的独立文件，不使用项目中的 `page_test.db`。

## 测试覆盖

- 构造参数检查、帧状态重置、缓存命中及重复 pin/unpin、全帧被 pin 时拒绝换页。
- DATA / INDEX / OVERFLOW 页创建，非法页号及非法页类型检查。
- 干净页淘汰、脏页写回、脏标志累积、刷盘不改变 pin 计数。
- 尚未 unpin 的页显式刷盘、重复刷盘、空池及多页刷盘。
- 读页失败、写入拒绝、分配失败回滚、删除失败恢复，不泄漏可用帧。
- pinned 页不可删除、缓存内外删除、重复删除、释放页号复用及旧内容清除。
- 正常析构保存修改、单页失败时继续刷新其它页、关闭后重开验证。
- 随机混合创建、读取、修改、pin/unpin、删除及刷盘，与独立模型逐步比对。

失败路径通过损坏磁盘页或内存页的校验和触发读失败、写入校验拒绝，
缓冲池行为通过公开 API 验证，不访问其私有成员，也不修改磁盘管理器的运行逻辑。
这不等同于模拟真实硬盘故障。

## 修复内容

| 问题 | 修改 |
| --- | --- |
| 缓冲池没有析构写回，正常结束时修改丢失 | 新增析构函数，在帧及磁盘管理器释放前尽力刷新 |
| 显式刷新只检查脏标志，漏写尚未 unpin 的修改 | 显式刷新实际写入页面；失败时标脏，成功后清脏 |
| `flush_all_pages` 在第一个失败页处直接返回 | 尝试全部页面，最终汇总成功/失败 |
| 目标页读失败前已淘汰旧页，失败请求改变缓存和磁盘 | 先读入临时页，读取与校验成功后才提交帧替换 |
| 已释放的 FREE 页被缓存，重新分配相同页号时误命中过期副本 | 拒绝将 FREE 页加入缓存 |
| META 页由磁盘层更新，缓冲池中的副本会过期且无法通过普通写页刷新 | META 保留给磁盘管理器，缓冲池拒绝 `fetch_page(0)` |
| 全帧被 pin 时仍先分配磁盘页再回滚，失败请求使文件增长 | 分配前先检查是否有空闲或可淘汰帧 |

修复位置保留了中文说明注释。另外，`storage/DiskManager.cpp` 末尾原有笔记块注释
未闭合，导致所有相关目标无法编译；仅补齐结束符并注明原因，未改动磁盘逻辑。

## 运行方法

在项目根目录执行（以下使用 Ninja，也可使用项目现有的 CMake 生成器）：

```powershell
cmake -S . -B cmake-build-verify -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build cmake-build-verify -j 4
ctest --test-dir cmake-build-verify --output-on-failure
```

仅运行缓冲池测试或单独运行随机测试：

```powershell
ctest --test-dir cmake-build-verify -R '^buffer_pool_' --output-on-failure
.\cmake-build-verify\buffer_pool_test.exe randomized_model
```

Release 验证：

```powershell
cmake -S . -B cmake-build-buffer-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic"
cmake --build cmake-build-buffer-release -j 4
ctest --test-dir cmake-build-buffer-release --output-on-failure
```

测试沿用项目的 `enable_test_assertions`，Release 下也保留断言，避免测试实际未执行。

## 使用约定与边界

- `fetch_page` / `new_page` 成功会增加一次 pin，调用方必须配对 `unpin_page`；修改后传 `true`。
- 释放最后一个 pin 后不再使用原 `Page*`，它可能在下一次操作中被复用或淘汰。
- 显式刷盘也支持仍被 pin 的页面，但不会替调用方 unpin。
- `new_page` 失败返回空指针并保留输出页号；全帧被 pin 的失败不会修改磁盘分配状态。
- 析构写回只能尽力执行，不能向调用方报告失败；重要保存点需显式调用并检查 `flush_all_pages()`。
- 当前按单线程接口验证，保留原有扫描选择可淘汰帧的策略；未增加线程安全、LRU、崩溃恢复或断电持久性保证。
