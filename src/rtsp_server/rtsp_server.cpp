// RTSP Server


#include <thread>
#include <memory>
#include <iostream>
#include <string>
#include "xop/RtspServer.h"
#include "net/Timer.h"
#include "bs.h"
#include "h265_stream.h"
#include <csignal>
#include "sample_comm.h"
#include "luckfox_mpi.hpp"
#include "generic_log.h"
#include "config.h"
#include "osd.hpp"
static volatile bool    s_spnet_loop;
static volatile int32_t s_client_count;
static std::atomic<bool>got_sig_f = {false};
static std::atomic<bool>restart={false};
static pthread_mutex_t  s_main_lock;
static pthread_cond_t   s_frame_cond;
static const char* aiq_file_path = CONF_AIQ_FILES_PATH;
static const char* archive_file_path = "/mnt/sdcard/";
static const uint32_t sc3336_width = CONF_SENSOR_WIDTH;
static const uint32_t sc3336_height= CONF_SENSOR_HEIGHT;
static const rk_aiq_working_mode_t sc3336_hdr_mode = CONF_HDR_MODE;
static const std::string font_file_path = CONF_OSD_FONT_PATH;
void SendFrameThread(xop::RtspServer* rtsp_server, xop::MediaSessionId session_id, luckfox_mpi* mpi_handle)
{
    mpi_handle->start_video_encoder(CONF_OSD_ENABLE);
    while(!got_sig_f.load())
    {
        if(s_spnet_loop){
            size_t data_len = 0;
            uint64_t timestamp = 0;
            uint8_t* pData = mpi_handle->venc_get_stream(restart.load(),&data_len,&timestamp);
            restart.store(false);
            if(data_len > 0 && pData != NULL) {
                xop::AVFrame videoFrame = {0};
                //videoFrame.type = 0;
                videoFrame.size = data_len;
                xop::H265Source::GetTimestamp(&videoFrame.timeNow,&videoFrame.timestamp);
                videoFrame.buffer.reset(new uint8_t[videoFrame.size]);
                memcpy(videoFrame.buffer.get(), pData, videoFrame.size);
                rtsp_server->PushFrame(session_id, xop::channel_0, videoFrame);
                mpi_handle->venc_release_stream();
            }else{
                LOGE("failed to get venc stream");
                break;
            }
        }
    }
}
void connect_callback(xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port)
{
    printf("RTSP client connect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
    restart.store(true);
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
        got_sig_f.store(true);
        int result = pthread_cond_signal(&s_frame_cond);
        LOGD("pthread_cond_signal result: %d", result);
    }
}



int main(int argc, char **argv)
{	
    got_sig_f.store(false);
    log_level_set(LOG_DBG);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGKILL, signal_handler);
    std::system("RkLunch-stop.sh");
    luckfox_mpi mpi_handle(aiq_file_path);
    mpi_handle.init_video_in(sc3336_hdr_mode,
                                    30,sc3336_width,sc3336_height);
    mpi_handle.init_video_encoder(RK_VIDEO_ID_HEVC,sc3336_width,sc3336_height);
    std::string ip = CONF_IP_ADDR;
	std::string rtsp_url = CONF_RTSP_PATH;

	std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());
	std::shared_ptr<xop::RtspServer> server = xop::RtspServer::Create(event_loop.get());
    osd::text_osd osd_handle(32);
    
    osd_handle.load_ttf_file(font_file_path);
    std::vector<uint8_t>pixel_buffer(1024,0);
    osd::bmp_resolution resolution {0};
    bool res_render = osd_handle.render_text_rgba8888("test text 1234:/",pixel_buffer,resolution);
    LOGI("result reneder text:%d,width:%d,height%d",res_render,resolution.width,resolution.height);
    res_render = osd_handle.save_rgba8888_to_bmp("/mnt/sdcard/test.bmp",pixel_buffer,resolution);
    LOGI("res bmp write:%d",res_render);
	if (!server->Start(ip, 554)) {
		return -1;
	}
	
	xop::MediaSession *session = xop::MediaSession::CreateNew("live"); // url: rtsp://ip/live
    
	session->AddSource(xop::channel_0, xop::H265Source::CreateNew(30));
    
	s_spnet_loop = false;
	session->AddNotifyConnectedCallback(connect_callback);
   
	session->AddNotifyDisconnectedCallback(disconnect_callback);

	std::cout << "URL: " << rtsp_url << std::endl;
        
	xop::MediaSessionId session_id = server->AddSession(session); 
    LOGD("rtsp_server session id:%d",session_id);    
	std::thread thread(SendFrameThread, server.get(), session_id, &mpi_handle);
	thread.detach();

    pthread_mutex_lock(&s_main_lock);
    pthread_cond_wait(&s_frame_cond, &s_main_lock);
    pthread_cond_destroy(&s_frame_cond);
    pthread_mutex_destroy(&s_main_lock);


    std::cout << "rtsp server exit" << std::endl;

	return 0;
}