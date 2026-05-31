#pragma once
#include <memory>
#include "lf_types.hpp"
#include "utils.hpp"
#include "ArchiveSvcTypes.hpp"
#include "generic_log.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}


namespace lf_mpi{
namespace archive_svc{

using std::unique_ptr;
using archive_svc_types::ArchiveSvcConfig;
struct AvOutCtxDeletr{
        void operator()(AVFormatContext* ctx){
            if(ctx){
                avformat_free_context(ctx);
            }
        }
    };
struct AvBsfCtxDeleter{
    void operator()(AVBSFContext* ctx){
        if(ctx){
            av_bsf_free(&ctx);
        }
    }
};
struct AvCodecParameterDeleter{
    void operator()(AVCodecParameters* par){
        if(par){
            avcodec_parameters_free(&par);
        }
    }
};

struct AvPacketDeleter{
    void operator()(AVPacket* pkt){
        if(pkt){
            av_packet_free(&pkt);
        }
    }
};

using AvOutCtxPtr = unique_ptr<AVFormatContext,AvOutCtxDeletr>;
using AvBsfCtxPtr = unique_ptr<AVBSFContext,AvBsfCtxDeleter>;
using AvCodecParPtr = unique_ptr<AVCodecParameters,AvCodecParameterDeleter>;
using AvPacketPtr = unique_ptr<AVPacket,AvPacketDeleter>;


class ArchiveSvc{

    public:
    ArchiveSvc();
    ArchiveSvc(const ArchiveSvc&) = delete;
    ~ArchiveSvc()noexcept;
    ArchiveSvc& operator=(const ArchiveSvc&) = delete;
    bool init(ArchiveSvcConfig config);
    bool write(uint8_t* annexb_data,size_t len,uint64_t pts_us);
    bool finalize();
    void cleanup();


    private:
    AvOutCtxPtr m_out_ctx{};
    AVStream*   m_stream_p{nullptr};
    bool m_header_written{false};
    bool m_extradata_extracted{false};
    char m_err_buf[AV_ERROR_MAX_STRING_SIZE]{0};
    struct pts_us_base{
        bool init_done{false};
        uint64_t pts_us{0};
    }m_pts_us_base;
    ArchiveSvcConfig m_config;
    int64_t m_last_dts{AV_NOPTS_VALUE};
    bool m_trailer_written{false};
    bool m_finalize_done{false};
};

}
}
