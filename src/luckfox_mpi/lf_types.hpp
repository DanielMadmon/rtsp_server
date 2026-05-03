#pragma once
#include <chrono>
#include <memory>
#include <functional>
#include "sample_comm.h"
#include "RtspServer.h"
#include "acetimec/src/acetimec.h"
#include "LuckfoxMpi.hpp"
#include "mmz_alloc.hpp"
#include "routing.hpp"
#include "im2d.hpp"
#include "rga_buffer.hpp"

namespace lf_mpi{
    typedef std::atomic<bool> flag;

    //values take from librga
    typedef enum{
            ROT_90     = 1 << 0,
            ROT_180    = 1 << 1,
            ROT_270    = 1 << 2,
    }ROTATION_OPTS;
    
    template <typename T,typename Alloc = mmz_alloc<T>>
    using MmzVector = std::vector<T,Alloc>;

    struct send_rtsp_frame_thread_ctx{
        xop::EventLoop* rtsp_event_loop;
        std::shared_ptr<xop::RtspServer> rtsp_server;
        xop::MediaSessionId session_id;
        xop::MediaSession *session = nullptr;
        xop::H265Source* h265_source = nullptr;
        flag* stop_flag = nullptr;
    };

    struct osd_update_timer_thread_ctx{
        osd::text_osd* osd_handle;
        std::vector<uint8_t,mmz_alloc<uint8_t>>* pixel_buffer = nullptr;
        osd::bmp_resolution* size = nullptr;
        flag*  update_osd_f = nullptr;
        flag* stop_flag = nullptr;
        const AtcZoneInfo* tz_info = &kAtcZonedballZoneAsia_Jerusalem;
    };
    struct send_vi_frame_thread_ctx{
        ///pointer to a vector containing the rednerd osd in rgba8888 format
        std::vector<uint8_t,mmz_alloc<uint8_t>>* pixel_buffer = nullptr;
        /// osd pixel buffer resolution
        osd::bmp_resolution* pixel_buffer_size = nullptr;
        /// venc input frame resolution
        osd::bmp_resolution venc_frame_res{0};
        flag* update_osd_f = nullptr;
        flag* stop_flag = nullptr;
        std::function<MB_BLK(LuckfoxMpi&, VIDEO_FRAME_INFO_S*)> vi_get_frame = nullptr;
        std::function<void(LuckfoxMpi&)> vi_release_frame = nullptr;
        std::function<bool(LuckfoxMpi&, VIDEO_FRAME_INFO_S&)> venc_send_frame = nullptr;
    };

    struct frame_transform_ctx{
        bool                init_done{false};
        /// crop or resize dst buf
        rga_buffer_t        dst_buf{0};
        /// crop or resize dst handle
        rga_buffer_handle_t dst_handle{0};
        /// crop or resize dst vec
        MmzVector<uint8_t>  dst_vec{};
        /// rotate dst buf
        rga_buffer_t        rot_dst_buf = {0};
        /// rotate dst handle
        rga_buffer_handle_t rot_dst_handle = 0;
        /// rotate dst vec
        MmzVector<uint8_t>  rot_dst_vec{}; 
        /// rga_buffer_t constructor
        rga_buf::rga_buffer rga_buf_ctor{}; 

        bool                crop_and_rotate{};
        bool                resize_and_rotate{};
        bool                rotate{};
    };

    typedef struct{
        std::string rknn_path = "/oem/usr/share/iqfiles";
        std::string font_file_path = "/oem/usr/share/simsun_en.ttf";
        rk_aiq_working_mode_t mode = rk_aiq_working_mode_t::RK_AIQ_WORKING_MODE_NORMAL;
        int32_t fps = 30;
        uint32_t width = 2304;
        uint32_t height = 1296;
        RK_CODEC_ID_E codec = RK_VIDEO_ID_HEVC;
        std::string ip = "0.0.0.0";
        std::string url = "live";
        uint16_t port = 554;
        const AtcZoneInfo* timezone = &kAtcZonedballZoneAsia_Jerusalem;
        bool rtsp_enable = true;
        uint32_t font_size = 32;
        routing::MpiViBindTo vi_binding = routing::MpiViBindTo::OSD;
        flag* stop_flag = nullptr;
        bool rotate_vi_frame = false;
        bool resize_vi_frame = false;
        bool crop_vi_frame = false;
        uint32_t resize_or_crop_width = 0;
        uint32_t resize_or_crop_height = 0;
        uint32_t crop_x = 0;
        uint32_t crop_y = 0;
        ROTATION_OPTS rotation_opts;
    }LuckfoxMpiConfig;

    constexpr RgaSURF_FORMAT YUV_RGA_FORMAT = RgaSURF_FORMAT::RK_FORMAT_YCbCr_420_SP;
    
    typedef struct TransformResult{
        rga_buffer_t rga_buffer;
        bool ok;
        TransformResult(rga_buffer_t rb,
                    bool is_ok) 
        : rga_buffer(rb), ok(is_ok) {}
        /// @brief construct failed result
        TransformResult() : rga_buffer({0}),ok(false){}
    }TransformResult;
}