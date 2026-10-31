# 基于页面的 B+ 树索引

本目录在现有 `storage/` 和 `buffer_pool/` 上实现持久化 B+ 树。
所有源码、文档和构建配置均位于 `index/`，没有修改已有模块和根目录 CMake。
没有引入第三方树容器，也不绕过缓冲池直接读写文件。

## 当前功能

- 单线程，唯一 `int64_t` 键，叶子项为 `IndexKey -> RID{page_id, slot_id}`。
- 支持创建索引、重新打开、有序插入、重复键检测和跨多层节点的等值查询。
- 叶子页满后自动分裂，并维护单向叶子链；分隔键向父节点传播。
- 内部页满后也会分裂；根分裂时生成新根并更新索引头页。
- 支持左闭右开的范围扫描、无界扫描和结果数量限制，沿叶子链顺序读取。
- 支持删除索引项、双向借位、叶子和内部节点合并、根收缩及废弃页回收。
- 独立索引头页持久化根页号，不占用由 `DiskManager` 管理的第 0 页。
- 所有索引页面通过缓冲池读写，自动配对 pin/unpin，修改后标脏。
- 每次仅暂时 pin 一页，支持只有一个缓冲帧时的查询、插入和分裂。
- 兼容第一阶段生成的单叶子索引页和头页，重新打开后可继续插入并自动扩展。

**287 是每个叶子页的容量，不再是整个索引的总容量上限。**
每个内部页最多有 335 个分隔键、336 个子页；树可按需继续增加层数，
实际容量仍受磁盘、页号空间及运行时资源限制。

尚未实现并发控制、跨表与索引的原子事务、WAL 和崩溃恢复。
目前每次访问会解码并验证整页，面向正确性和磁盘页面组织，尚不是高性能实现。

完整开发过程、算法、不变量、测试和恢复设计边界见 `DEVELOPMENT_REPORT.md`。

## 文件职责

| 文件 | 职责 |
| --- | --- |
| `RID.h` | 记录位置，包含数据页号与槽号 |
| `BPlusTreePage.h/.cpp` | 叶子节点、内部节点和索引头页的验证、编码及解码 |
| `BPlusTree.h/.cpp` | 创建、打开、路由、插入、分裂传播和等值查询 |
| `b_plus_tree_test.cpp` | 页面格式、树结构、不变量、持久化和失败路径测试 |
| `b_plus_tree_operations_test.cpp` | 范围扫描、删除、借位、合并、页面复用及混合随机测试 |
| `DEVELOPMENT_REPORT.md` | 开发过程、设计依据、核心算法、验证方法和限制说明 |
| `CMakeLists.txt` | 独立构建索引库和测试，编译复用现有存储及缓冲池源码 |

## 页面格式

所有索引页都使用现有的 `PageHeader + Slot + record` 布局，
节点内容保存为槽 0 中的一条记录。编码先在临时页面中重建记录，成功后才替换页面副本，
继续满足 `Page::validate()` 和校验和约束；拒绝非 INDEX 页、无效页和多槽页。

索引记录内部的整数按小端逐字段编码，不把 `std::vector`、内存指针或含填充字节的结构体直接写盘。
这只规定索引记录的格式，外围页面格式仍遵循现有存储实现。

### 叶子页

记录头为 20 字节：

| 字段 | 字节数 |
| --- | --- |
| magic：`0x42505449` | 4 |
| version：`1` | 2 |
| kind：`1` | 2 |
| entry_count | 2 |
| reserved：`0` | 2 |
| parent_page_id | 4 |
| next_page_id | 4 |

后续每项为 14 字节：`key(8) + page_id(4) + slot_id(2)`。
键必须严格递增。叶子分裂时保留左页号，新分配右页，右页继承原后继，
左页指向右页，把右页首键复制到父节点。

### 内部页

同样使用 20 字节记录头，但 `kind = 2`，数量字段是分隔键数量，
最后 4 字节是第一个子页号，而不是叶子后继页号。
随后每项为 12 字节：`separator_key(8) + right_child_page_id(4)`。
有 N 个分隔键就有 N+1 个子页。分隔键等于对应右子树的最小键，
查找时使用 `upper_bound`，键相等时进入右子树。

内部节点分裂时把中间键提升至父节点；左右节点均不保留该键，
搬到新右节点下的所有子页都会更新父页号。
根节点分裂时，新建根节点、调整左右节点的父页号，并更新索引头页。

解码检查长度、版本、类型、数量、排序和基础页号约束，失败时保持输出对象不变。
查询还检查路径上的父子关系、键范围和循环引用，错误不当作“记录不存在”。
`open()` 只检查头页和根节点，不做全树审计；未访问分支的损坏可能在后续操作时才发现。

### 索引头页

记录为 12 字节：`magic(4) + version(2) + kind(2，值为 0) + root_page_id(4)`。
根页号已持久化，但**索引头页号本身仍需上层目录或调用方保存**，当前没有系统目录自动发现索引。

## 基本使用

包含 `index/BPlusTree.h`。以下假定已有 `BufferPoolManager buffer_pool`，
且记录确实位于数据页 8 的槽 3：

```cpp
auto tree = BPlusTree::create(buffer_pool);
if (!tree) {
    throw std::runtime_error("cannot create index");
}

const page_id_t header_page_id = tree->header_page_id();
const auto inserted = tree->insert(1001, RID{8, 3});
if (inserted != BPlusTree::InsertResult::INSERTED) {
    throw std::runtime_error("cannot insert index entry");
}

RID location;
if (tree->find(1001, location) == BPlusTree::FindResult::FOUND) {
    Page* page = buffer_pool.fetch_page(location.page_id);
    if (page == nullptr) {
        throw std::runtime_error("cannot fetch data page");
    }
    std::uint16_t length = 0;
    const char* record = page->get_record(location.slot_id, length);
    const bool record_exists = record != nullptr;
    buffer_pool.unpin_page(location.page_id, false);
    if (!record_exists) {
        throw std::runtime_error("index points to missing record");
    }
}

if (!buffer_pool.flush_all_pages()) {
    throw std::runtime_error("cannot flush database");
}
```

业务代码需要包含 `<stdexcept>`。记录必须在数据页 unpin 前处理或复制，
不能继续使用上例中的 `record` 指针。实际业务还应显式处理查询返回 `PAGE_ERROR` 的情况。

索引不会解析业务记录，也不会验证 RID 对应的记录是否存在。
`RID::is_valid()` 仅检查页号不是 0 或 `INVALID_PAGE`；数据页类型、槽范围和记录存活状态由记录管理层负责。

重新打开数据库及缓冲池后，用保存的头页号打开索引，不要再次调用 `create()`：

```cpp
auto tree = BPlusTree::open(buffer_pool, header_page_id);
if (!tree) {
    throw std::runtime_error("cannot open index");
}
```

同一数据库支持多个独立索引；同一个缓冲池中，也可有多个句柄顺序访问同一个索引。
每次插入、删除、等值查询和非空范围扫描会重新读取头页，避免其他句柄改变根后仍从旧根开始。
`root_page_id()` 是无 I/O 的缓存访问器，返回本句柄最近成功读取或更新的根页号；
若其他句柄修改了根，本句柄下一次实际访问树时刷新。
不支持多个独立缓冲池同时修改同一数据库文件。
缓冲池的生命周期必须覆盖索引对象，销毁对象不会删除磁盘索引页。

### 返回值

- `insert()`：`INSERTED`、`DUPLICATE_KEY`、`INVALID_RID`、`PAGE_ERROR`。
- 取消第一阶段的 `PAGE_FULL`：页满会尝试分裂；分配或页面操作失败时返回 `PAGE_ERROR`。
- `find()`：`FOUND`、`NOT_FOUND`、`PAGE_ERROR`；未找到或失败时保持输出 RID 不变。
- `create()` / `open()`：失败返回空指针。
- `erase()`：`REMOVED`、`NOT_FOUND`、`PAGE_ERROR`、`RECLAIM_FAILED`。
- `scan()`：`SUCCESS`、`INVALID_RANGE`、`PAGE_ERROR`。

### 范围扫描

```cpp
std::vector<BPlusTreeLeafEntry> entries;
const auto result = tree->scan(1000, 2000, entries, 100);
if (result != BPlusTree::ScanResult::SUCCESS) {
    throw std::runtime_error("cannot scan index");
}
```

区间是 `[lower, upper)`，上例按键升序返回最多 100 项。
用 `std::nullopt` 或 `{}` 表示无下界或无上界，例如 `tree->scan({}, {}, entries)` 扫描全部。
无上界可以包含 `INT64_MAX`，无需计算可能溢出的 `INT64_MAX + 1`。
下界大于上界返回 `INVALID_RANGE`；上下界相等、`limit == 0` 返回成功的空结果，不访问树。
成功时替换输出向量；出错时保持原输出不变，不暴露部分结果。
默认不限制数量，会在内存中收集全部匹配项；大范围查询建议显式设置 `limit`。
当前没有跨调用的游标或并发快照语义。

### 删除索引项

```cpp
const auto result = tree->erase(1001);
if (result == BPlusTree::EraseResult::RECLAIM_FAILED) {
    throw std::runtime_error("entry removed, but index page reclamation failed");
}
if (result == BPlusTree::EraseResult::PAGE_ERROR) {
    throw std::runtime_error("cannot erase index entry");
}
```

`erase()` 只删除索引里的键与 RID，不删除数据页中的业务记录。
删除会更新分隔键；节点不足时先尝试向左、右兄弟借位，否则合并，并逐层修复父节点。
根只剩一个子页时提升子页；全部删空后保留一个空叶子根页和索引头页，可重新插入。
非根叶子至少保留 144 项，非根内部节点至少保留 167 个分隔键。

废弃页在结构修改提交后才交给缓冲池删除并回到磁盘空闲链表。
`RECLAIM_FAILED` 表示**索引项已经删除，但部分废弃页未能回收**，例如该页仍被外部 pin。
这不是“删除未发生”，不要把它当成事务回滚成功；当前不自动保存或重试回收队列，
可能遗留不可达页面。不应由外部长期 pin 或修改索引拥有的页面。
正常回收成功的页面可由后续分配复用，但数据库文件不会因回收自动缩小。

### 失败与持久化边界

修改先暂存在内存中的页面副本里，包括新页、移动子页的父页号以及头页更新。
提交前预检涉及页面是否仍可获取且内容未改变；随后先写新节点，最后更新头页。
提交前发现损坏或无法获取页面时，不改写原有节点，并尽力释放本次分配的新页。
若提交途中失败，尽力按逆序恢复页面副本；恢复也失败时保留新页，避免释放可能已被引用的页面。
删除涉及的废弃页只在结构提交成功后释放；结构提交失败时不释放这些旧页。

这不是事务或 WAL：真实 I/O 故障、内存分配异常或进程中断仍不保证多页更新原子性，
恢复失败也可能遗留页面和不一致的索引。正常保存点需要检查 `flush_all_pages()`。
查询的路径页访问数随树高增长，但还会读取索引头页；缓冲池命中不代表发生物理磁盘 I/O。
本阶段测试正确性与可扩展性，未宣称相对全表扫描的实测加速比。

### 与现有数据页配合

- 索引页由索引层独占，不混入其他记录。
- 数据页 `compact()` 不改变槽号，可以保持原 RID 有效。
- 不对被索引数据页直接执行 `purge_deleted_slots()`；它改变槽号，必须同步修正索引。
- 删除记录、修改索引字段、跨页搬迁或释放数据页，都需要上层协调维护索引。
  当前已有 `erase()`，但数据页操作和索引操作之间尚无原子事务保证。

## 独立构建和测试

从项目根目录执行，无需修改根目录 `CMakeLists.txt`：

```powershell
cmake -S index -B cmake-build-index -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build cmake-build-index -j 4
ctest --test-dir cmake-build-index --output-on-failure
```

Release 验证：

```powershell
cmake -S index -B cmake-build-index-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build cmake-build-index-release -j 4
ctest --test-dir cmake-build-index-release --output-on-failure
```

只运行多层节点和大数据量测试：

```powershell
ctest --test-dir cmake-build-index -R 'index_tree_internal_split|index_tree_descending' --output-on-failure
```

测试包括 10 万项正序插入与逐项查询、6 万项逆序插入与逐项查询，
实际触发内部节点分裂、根分裂、三层树，以及非根内部节点继续分裂。
还验证树高、叶子数量、父指针、分隔键、节点占用和叶子链，
覆盖单缓冲帧、整数边界、重复键、多索引隔离、多句柄顺序访问、重启读取、
损坏分支、分裂失败后回收，以及与 `std::map` 对比的随机模型。
新增测试覆盖区间边界、跨页扫描、输出失败保持、双向借位、双向合并、根收缩、
回收失败区分、删空后页复用，以及 48,672 项三层边界树的乱序删空与分批重启。
内部删除边界树由测试按照合法页面格式直接构造，不把它宣称为生产级批量加载接口。
测试使用临时数据库，不覆盖项目的 `page_test.db`；Release 也保留断言。

在 CLion 中可单独打开本目录的 `CMakeLists.txt` 构建测试。
根工程 `db_test` 目标仍未链接索引模块，现有构建和运行行为保持不变。

## 后续方向

1. 在记录管理层和系统目录中集成索引生命周期及数据同步。
2. 在相同数据与缓存条件下，对比全表扫描和索引查询的耗时与页面访问次数。
3. 增加流式范围游标、回收重试或不可达页整理。
4. 优化页内搜索和编码开销，设计并发控制、事务与 WAL 恢复协议。
