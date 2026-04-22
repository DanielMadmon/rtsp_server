#pragma once
#include "sample_comm.h"
#include "RtspServer.h"
#include "acetimec/src/acetimec.h"
#include "LuckfoxMpi.hpp"
#include "mmz_alloc.hpp"
#include <chrono>
#include <memory>
#include <functional>

namespace lf_mpi{
    typedef std::atomic<bool> flag;
    

    struct send_rtsp_frame_thread_ctx{
        LuckfoxMpi* mpi_handle;
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
        osd::bmp_resolution* pixel_buffer_size = nullptr;
        flag* update_osd_f = nullptr;
        flag* stop_flag = nullptr;
        std::function<MB_BLK(LuckfoxMpi&, VIDEO_FRAME_INFO_S*)> vi_get_frame = nullptr;
        std::function<void(LuckfoxMpi&)> vi_release_frame = nullptr;
        std::function<bool(LuckfoxMpi&, VIDEO_FRAME_INFO_S&)> venc_send_frame = nullptr;
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
        bool osd_enable = true;
        uint32_t font_size = 32;
        flag* stop_flag = nullptr;
    }LuckfoxMpiConfig;
}