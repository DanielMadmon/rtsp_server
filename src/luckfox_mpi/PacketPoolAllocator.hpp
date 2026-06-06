#pragma once
#include "ArchiveSvcTypes.hpp"
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

namespace lf_mpi{
namespace packet_pool_allocator{

using std::vector;

class PacketPoolAllocator{

    public:
    PacketPoolAllocator() = default;
    PacketPoolAllocator(PacketPoolAllocator&&) = delete; 
    PacketPoolAllocator(const PacketPoolAllocator&) = delete;
    bool init(size_t size = 300 * 1024);
    AVBufferRef* get_buffer(size_t size);
    ~PacketPoolAllocator() noexcept;
    private:
    AVBufferPool* m_pool{nullptr};
};


}//namespace packet_pool_allocator
}//namespace lf_mpi

