#pragma once
#include "luckfox_mpi.hpp"
#include "RtspServer.h"
#include "lf_types.hpp"
#include <atomic>

namespace lf_mpi{
    class lf_mpi_svc{
        using u8_vec_mmz =  std::vector<uint8_t,mmz_alloc<uint8_t>>;
        public:
        static lf_mpi_svc& create_new(luckfox_mpi_config config);
        ~lf_mpi_svc();
        bool init();
        bool start_vi_svc();
        bool start_rtsp_svc();
        bool wait_on_exit();
        /// @brief should only be called at exit
        static void exit_svc();
        private:
            luckfox_mpi_config lf_config = {0};
            lf_mpi_svc(luckfox_mpi_config config);
            static lf_mpi_svc* take();
            static void connect_callback(xop::MediaSessionId sessionId, std::string peer_ip, 
                                        uint16_t peer_port);
            static void disconnect_callback(xop::MediaSessionId sessionId, std::string peer_ip, 
                                        uint16_t peer_port);
            static void send_venc_frame_thread(send_rtsp_frame_thread_ctx* thread_ctx);
            static void osd_update_timer_thread(osd_update_timer_thread_ctx* thread_ctx);
            static void send_vi_frame_thread(send_vi_frame_thread_ctx* thread_ctx);
            static inline time_t utils_get_next_minute(time_t start_time);
            //vars
            inline static lf_mpi_svc* svc_instance = nullptr;
            u8_vec_mmz _pixel_buffer = u8_vec_mmz(16384,0);
            osd::bmp_resolution _size = {0,0};
            flag _update_osd_flag {true};
            send_rtsp_frame_thread_ctx _send_rtsp_frame_thread_ctx = {0};
            osd_update_timer_thread_ctx _osd_update_timer_thread_ctx = {0};
            send_vi_frame_thread_ctx _send_vi_frame_thread_ctx;
            flag init_done {false};
            flag svc_vi_active {false};
            flag svc_rtsp_active  {false};
            inline static flag idr_reset {true};
            inline static flag client_conn_flag {false};
    };
}