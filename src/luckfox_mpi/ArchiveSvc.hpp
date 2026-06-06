#pragma once
#include "SigTx.hpp"
#include <atomic>
#include <string>
#include <thread>
#include <filesystem>
#include <boost/static_string.hpp>
#include <set>
#include <optional>
#include <vector>
#include <HevcFileWriter.hpp>

namespace lf_mpi{
namespace archive_svc{

using std::atomic,std::string,std::thread,std::vector;
using fs_string = boost::static_string<512>;
using filematch_set   = std::set<size_t,std::less<size_t>,std::allocator<size_t>>;
using regex_str       = boost::static_string<64>;

template<typename T>
using option = std::optional<T>;

struct ArchiveSvcConfig{
    uint32_t      bitrate{0};
    uint16_t      width{0};
    uint16_t      height{0};
    fs_string     storage_path{};
    fs_string     filename_prefix{};
    uint32_t      fps{0};
    size_t        writer_queue_size{0};
    uintmax_t     max_file_size_mb{0};
};

class ArchiveSvc{
    public:
    ArchiveSvc(ArchiveSvcConfig config);
    ArchiveSvc(const ArchiveSvc&) = delete;
    ArchiveSvc(ArchiveSvc&&) = delete;
    bool init();
    void deinit();
    ~ArchiveSvc();

    private:
    void writer_thread();
    atomic<bool> m_stop_flag{false};
    hevc_file_writer::HevcFileWriter m_hevc_file_writer{};
    thread m_writer_thread{};
};

struct ArchiveSvcFsOpsConfig{
    const fs_string storage_path;
    const fs_string filename_prefix;
    const uint64_t max_file_size_mb;
    ArchiveSvcFsOpsConfig(const ArchiveSvcConfig& cfg):
        storage_path(cfg.storage_path),
        filename_prefix(cfg.filename_prefix),
        max_file_size_mb(cfg.max_file_size_mb)
        {}
};

class ArchiveSvcFsOps{
    public:
    ArchiveSvcFsOps() = default;
    ArchiveSvcFsOps(const ArchiveSvcFsOps&) = default;
    explicit ArchiveSvcFsOps(const ArchiveSvcConfig& config);
    option<fs_string> get_next_full_file_name();
    option<bool>      need_new_file(const fs_string& full_filename) const;
    private:
    option<vector<size_t>> get_num_files_list() const;
    option<uintmax_t>  get_available_space_mb() const;
    [[nodiscard]] bool rm_file(size_t filenum) const;
    option<fs_string>  build_full_file_name(size_t filenum) const;
    ArchiveSvcFsOpsConfig m_config;
    bool                  m_init_done;
    const uint64_t        m_max_file_size_bytes;
};


}//archive_svc
}//lf_mpi