#include "PacketPoolAllocator.hpp"

namespace lf_mpi{
namespace packet_pool_allocator{


bool PacketPoolAllocator::init(size_t size)
{
    m_pool = av_buffer_pool_init(static_cast<int>(size),nullptr);

    return m_pool != nullptr;
}

AVBufferRef *PacketPoolAllocator::get_buffer(size_t size)
{
    if(!m_pool){
        return nullptr;
    }
    return av_buffer_pool_get(m_pool);
}

PacketPoolAllocator::~PacketPoolAllocator() noexcept
{
    try{
        if(m_pool){
            av_buffer_pool_uninit(&m_pool);
        }
    }catch(...){
        return;
    }
    
}
}
}//namespace lf_mpi
