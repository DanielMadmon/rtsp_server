#pragma once
#include <atomic>
#include <memory>
#include "LuckfoxMpi.hpp"
#include "RtspServer.h"
#include "lf_types.hpp"


namespace lf_mpi{

    /// @brief a singleton class for easy handling of
    /// streaming from camera to video encoder and from video
    /// encoder to rtsp server
    class MpiSvc{
        using u8_vec_mmz =  std::vector<uint8_t,mmz_alloc<uint8_t>>;
        public:
        /// @brief create a new lf_mpi_svc instatnce. 
        /// if already created returns old instance and does nothing
        /// @param config service configuration
        /// @return a reference to lf_mpi_svc
        static MpiSvc& create_new(LuckfoxMpiConfig config);
        /// @brief starts service based on config passed before(e.g rtsp server and 
        /// sender thread. osd drawing)
        /// @return true on success
        /// @warning Only call this function once between MpiSvc::create_new and MpiSvc::exit_svc
        bool init();
        /// @brief must be called for service deinit.
        /// if config.stop_flag is unset than it will be set
        /// and error will be printed before exiting
        static void exit_svc();
        private:
            LuckfoxMpiConfig lf_config = {0};
            LuckfoxMpi* mpi_handle = nullptr;
            /// private constructor
            MpiSvc(LuckfoxMpiConfig config);
            /// private destructor
            ~MpiSvc();
            /// @brief blocking wait for all internal threads to exit.
            void wait_on_exit();
            static MpiSvc* take();
            static void connect_callback(xop::MediaSessionId sessionId, std::string peer_ip, 
                                        uint16_t peer_port);
            static void disconnect_callback(xop::MediaSessionId sessionId, std::string peer_ip, 
                                        uint16_t peer_port);
            static void send_rtsp_frame_thread(send_rtsp_frame_thread_ctx* thread_ctx);
            static void osd_update_timer_thread(osd_update_timer_thread_ctx* thread_ctx);
            static void send_vi_frame_thread(send_vi_frame_thread_ctx* thread_ctx);
            static inline time_t utils_get_next_minute(time_t start_time);
            bool start_vi_svc();
            bool start_rtsp_svc();
            //vars
            inline static MpiSvc* svc_instance = nullptr;
            u8_vec_mmz _pixel_buffer{};
            osd::bmp_resolution _size = {0,0};
            flag _update_osd_flag {false};
            send_rtsp_frame_thread_ctx _send_rtsp_frame_thread_ctx = {0};
            osd_update_timer_thread_ctx _osd_update_timer_thread_ctx = {0};
            send_vi_frame_thread_ctx _send_vi_frame_thread_ctx;
            flag init_done{false};
            inline static flag idr_reset {true};
            inline static flag client_conn_flag {false};
            inline static constexpr size_t rtsp_buffer_size = 400 * 1024;
            inline static std::thread vi_thread;
            inline static std::thread timer_thread;
            inline static std::thread rtsp_thread;
            inline static std::atomic<uint32_t> connected_clients = 0;
    };

    struct VencStreamDeleter {
        LuckfoxMpi* handle;
        void operator()(uint8_t*) const {
            if (handle) handle->venc_release_stream();
        }
    };
}