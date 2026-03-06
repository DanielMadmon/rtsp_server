// RTSP Server


#include <thread>
#include <memory>
#include <iostream>
#include <string>
#include "xop/RtspServer.h"
#include "net/Timer.h"
#include "bs.h"
#include "h265_stream.h"
#include "log.hpp"
#include <csignal>
#include "sample_comm.h"
#include "luckfox_mpi.hpp"
#include "generic_log.h"

static volatile bool    s_spnet_loop;
static volatile int32_t s_client_count;
static pthread_mutex_t  s_main_lock;
static pthread_cond_t   s_frame_cond;
static const char* aiq_file_path = "/oem/usr/share/iqfiles";
static const uint32_t sc3336_width = 2304;
static const uint32_t sc3336_height= 1296;
static const rk_aiq_working_mode_t sc3336_hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;

//void SendFrameThread(xop::RtspServer* rtsp_server, xop::MediaSessionId session_id, int& clients)
void SendFrameThread(xop::RtspServer* rtsp_server, xop::MediaSessionId session_id, luckfox_mpi* mpi_handle)
{
    //TODO: figure out how to reset capture when streaming first frames
    while(1)
    {
        if(s_spnet_loop){
            bool end_of_frame = false;
            size_t data_len = 0;
            uint64_t timestamp = 0;
            uint8_t* pData = mpi_handle->venc_get_stream(&data_len,&timestamp);
            if(data_len > 0) {
                xop::AVFrame videoFrame = {0};
                //videoFrame.type = 0;
                videoFrame.size = data_len;
                xop::H265Source::GetTimestamp(&videoFrame.timeNow,&videoFrame.timestamp);
                videoFrame.buffer.reset(new uint8_t[videoFrame.size]);
                memcpy(videoFrame.buffer.get(), pData, videoFrame.size);
                rtsp_server->PushFrame(session_id, xop::channel_0, videoFrame);
                mpi_handle->venc_release_stream();
            }
            else {
                break;
            }
        }
        xop::Timer::Sleep(1);     
    }
}
void connect_callback(xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port)
{
    printf("RTSP client connect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
    s_spnet_loop = true;
    s_client_count++;
}
void disconnect_callback(xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port)
{
    printf("RTSP client disconnect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
    if(s_client_count > 0) {
        s_client_count--;
    }
    s_spnet_loop = s_client_count > 0;
}
void signal_handler(int sig)
{
    if(sig == SIGINT || sig == SIGTERM || sig == SIGQUIT || sig == SIGKILL) {
        s_spnet_loop = false;
        int result = pthread_cond_signal(&s_frame_cond);
        LOGD("pthread_cond_signal result: %d", result);
    }
}
int main(int argc, char **argv)
{	
    log_level_set(LOG_DBG);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGKILL, signal_handler);
    std::system("RkLunch-stop.sh"); 
    std::unique_ptr<luckfox_mpi> luckfox_mpi_handle(new luckfox_mpi(aiq_file_path));
    luckfox_mpi* mpi_handle = luckfox_mpi_handle.get();
    mpi_handle->init_video_in(sc3336_hdr_mode,
                                    30,sc3336_width,sc3336_height);
    mpi_handle->init_video_encoder(RK_VIDEO_ID_HEVC,sc3336_width,sc3336_height);
    //luckfox_mpi_handle.init_vpss();
    //luckfox_mpi_handle.bind_vin_vpss();
    

    int clients = 0;
	std::string ip = "0.0.0.0";
	std::string rtsp_url = "rtsp://127.0.0.1:554/live";

	std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());
	std::shared_ptr<xop::RtspServer> server = xop::RtspServer::Create(event_loop.get());
    
	if (!server->Start(ip, 554)) {
		return -1;
	}
	
#ifdef AUTH_CONFIG
	server->SetAuthConfig("-_-", "admin", "12345");
#endif
	 
	xop::MediaSession *session = xop::MediaSession::CreateNew("live"); // url: rtsp://ip/live
    
	session->AddSource(xop::channel_0, xop::H265Source::CreateNew(30));
    
	s_spnet_loop = false;
	session->AddNotifyConnectedCallback(connect_callback);
   
	session->AddNotifyDisconnectedCallback(disconnect_callback);

	std::cout << "URL: " << rtsp_url << std::endl;
        
	xop::MediaSessionId session_id = server->AddSession(session); 
         
	std::thread thread(SendFrameThread, server.get(), session_id, luckfox_mpi_handle.get());
	thread.detach();

    pthread_mutex_lock(&s_main_lock);
    pthread_cond_wait(&s_frame_cond, &s_main_lock);
    pthread_cond_destroy(&s_frame_cond);
    pthread_mutex_destroy(&s_main_lock);


    std::cout << "rtsp server exit" << std::endl;

	return 0;
}