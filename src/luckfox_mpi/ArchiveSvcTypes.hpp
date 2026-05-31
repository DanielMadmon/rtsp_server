#pragma once
#include "utils.hpp"
#include "lf_types.hpp"
#include <string>

namespace lf_mpi{
namespace archive_svc_types{

using ResolutionVal = utils::SafeIntRange<int,128,4096>;
using std::string;


struct ArchiveSvcResolution{
    ResolutionVal width;
    ResolutionVal height;
    ArchiveSvcResolution(size_t width,size_t height):
    width(static_cast<int>(width)),
    height(static_cast<int>(height))
    {}
};

struct ArchiveSvcConfig{
    flag*  stop_flag{nullptr};
    //max file size in megabytes
    size_t max_file_size{256};
    string storage_path{"/mnt/sdcard/camera_backup_0.mp4"};
    uint16_t width{2304};
    uint16_t height{1296};
    int32_t fps{30};
};



namespace comptime_consts{
    constexpr size_t DEFAULT_QUEUE_SIZE {1000};
    constexpr std::array<uint8_t,4> START_CODE_PREFIX_4 {0x00, 0x00, 0x00, 0x01}; // 4-byte start code
    constexpr std::array<uint8_t,3> START_CODE_PREFIX_3 {0x00, 0x00, 0x01}; // 3-byte start code
    constexpr uintmax_t MB{1048576ull};
}

}
}