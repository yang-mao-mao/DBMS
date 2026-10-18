#ifndef CI_PAN_CUN_CHU_H
#define CI_PAN_CUN_CHU_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>

//定义常量
inline constexpr std::size_t PAGE_SIZE = 4096;
constexpr std::uint32_t INVALID_PAGE = 0xFFFFFFFFu;
constexpr std::uint32_t PAGE_MAGIC   = 0xDBDBDBDBu;
constexpr std::uint32_t PAGE_FORMAT_VERSION = 1u;
constexpr std::uint16_t SLOT_FLAG_DELETED  = 0x0001;
constexpr std::uint16_t SLOT_FLAG_OVERFLOW = 0x0002;

//using Page = std::array<char, PAGE_SIZE>;

// create_file truncates an existing file. open_file does not.
bool create_file(const std::string& name);
std::unique_ptr<std::ifstream> open_read(const std::string& name);
std::unique_ptr<std::fstream> open_rw(const std::string& name);
std::unique_ptr<std::fstream> create_rw(const std::string& name);
std::unique_ptr<std::fstream> open_file(const std::string& name);



//页面结构应如是
// +------------------------+  偏移 0
// |      PageHeader        |  固定 48 字节
// +------------------------+  偏移 free_start，应该是槽记录的末尾
// |  槽0 | 槽1 | 槽2 | ...  |  槽目录从低地址向高地址增长
// +------------------------+  偏移 free_end，应该是最后一条数据记录的开头
// |      空闲空间           |
// +------------------------+
// |  ... 记录数据 ...       |  记录数据从页尾向低地址增长
// +------------------------+
// |  记录3 | 记录2 | 记录1   |
// +------------------------+  偏移 PAGE_SIZE

enum class PageType : std::uint16_t {
    INVALID   = 0,
    META      = 1,   // 元数据页（通常是第 0 页）
    DATA      = 2,   // 数据页
    INDEX     = 3,   // B+ 树索引页
    OVERFLOW  = 4,   // 溢出页
    FREE      = 5,   // 空闲页
};

//定义页头
struct PageHeader {
    // ---- 标识与元信息 (16 字节) ----
    std::uint32_t magic;     // 魔数，例如 0xDBDBDBDB，标识这是数据库页
    PageType page_type;      // 页类型：数据页/索引页/溢出页/空闲页...
    std::uint16_t page_size; // 页大小，校验用（比如 4096）
    std::uint32_t page_id;   // 页号（从 0 开始）
    std::uint32_t file_version; // 文件格式版本号

    // ---- 崩溃恢复 (8 字节) ----
    std::uint64_t lsn;       // 最近一次修改对应的日志序列号

    // ---- 空闲空间管理 (8 字节) ---
    //这里我们将尝试采用变长记录，为了实现这一点，我们还需要一个数据结构：槽
    std::uint16_t slot_count;     // 槽目录中槽的数量
    std::uint16_t free_start;     // 空闲空间的起始偏移
    std::uint16_t free_end;       // 空闲空间的结束偏移
    std::uint16_t fragment_bytes; // 碎片字节数（已删除但未回收的小空洞）

    // ---- 页间链接 (8 字节) ----
    std::uint32_t prev_page; // 上一页页号，0xFFFFFFFF 表示无
    std::uint32_t next_page; // 下一页页号，0xFFFFFFFF 表示无

    // ---- 完整性校验 (8 字节) ----
    std::uint32_t checksum;  // 整页校验和（计算时本字段置 0）
    std::uint32_t reserved;  // 预留，凑齐对齐或未来扩展
};
static_assert(sizeof(PageHeader) == 48, "PageHeader 大小不对");
static_assert(std::is_trivially_copyable_v<PageHeader>,
              "PageHeader 必须可以按字节读写");

//定义页类型


//定义槽结构
struct Slot {
    std::uint16_t offset;   // 记录在页内的起始偏移
    std::uint16_t length;   // 记录长度（字节）
    std::uint16_t flags;    // 标志位：是否删除、是否溢出等 0指定为正常使用的记录，1表示已删除
};
static_assert(sizeof(Slot) == 6, "Slot 大小不对");
static_assert(std::is_trivially_copyable_v<Slot>, "Slot 必须可以按字节读写");

struct Page {
    PageHeader header{};
    char body[PAGE_SIZE - sizeof(PageHeader)]{};

    // ==================== 生命周期 ====================
    void init(std::uint32_t page_id, PageType type);   // 初始化空页
    void clear();                                       // 清空所有记录，保留页号

    // ==================== 插入 ====================
    // 返回槽号，失败返回 -1（页满）
    int  insert_record(const char* record, std::uint16_t length);

    // ==================== 读取 ====================
    // 按槽号取记录；槽号非法或已删除返回 nullptr
    const char* get_record(std::uint16_t slot_index, std::uint16_t& out_len) const;
    [[nodiscard]]bool        is_deleted(std::uint16_t slot_index) const;

    // ==================== 更新 ====================
    // 新长度 <= 旧长度：原地改
    // 新长度 >  旧长度：尝试搬移，失败返回 false
    bool update_record(std::uint16_t slot_index,
                       const char* record, std::uint16_t length);

private:
    [[nodiscard]] Slot load_slot(std::uint16_t i) const;
    void store_slot(std::uint16_t i, const Slot& s);

public:
    // ==================== 删除 ====================
    bool delete_record(std::uint16_t slot_index);      // 标记删除（不搬数据）
    void compact();                                     // 整理碎片，回收空间,不改变槽索引
    void purge_deleted_slots();                        //改变槽索引

    // ==================== 遍历（给上层用） ====================
    [[nodiscard("有人调用了slot_count函数，但是没有接收返回值")]] std::uint16_t slot_count()  const {
        return header.slot_count;
    }
    [[nodiscard("有人调用了free_bytes函数，但是没有接收返回值")]] std::uint16_t free_bytes()  const {
        if (header.free_start > header.free_end || header.free_end > PAGE_SIZE) {
            return 0;
        }
        return static_cast<std::uint16_t>(header.free_end - header.free_start);
    }

    // ==================== 校验 ====================
    [[nodiscard]]std::uint32_t compute_checksum() const;
    [[nodiscard]]bool          verify_checksum()  const;
    [[nodiscard]]bool          validate() const;



    void          update_checksum();
};
static_assert(sizeof(Page) == PAGE_SIZE, "Page 必须是 4096 字节");
static_assert(std::is_trivially_copyable_v<Page>, "Page 必须可以按整页字节读写");


// A page numbered n starts at byte n * PAGE_SIZE.
// Both functions transfer exactly one whole page.
bool write_page(std::fstream& file, std::uint64_t page_no, const Page& page);
bool read_page(std::fstream& file, std::uint64_t page_no, Page& page);

#endif // CI_PAN_CUN_CHU_H
