#include <memory>
#include <chrono>
#include "rga_buffer.hpp"
#include "lf_mpi_svc.hpp"
#include "lf_types.hpp"
#include "generic_log.h"
#include "acetimec.h"
#include "mmz_alloc.hpp"

using namespace lf_mpi;
using namespace std::chrono;

constexpr std::memory_order memory_order_set = std::memory_order_seq_cst;
constexpr std::memory_order memory_order_get = std::memory_order_seq_cst;

void lf_mpi::lf_mpi_svc::exit_svc()
{
    lf_mpi_svc* svc = lf_mpi_svc::take();
    if(!svc){
        return;
    }
    if(!svc->lf_config.stop_flag->load(memory_order_get)){
        LOGE("called exit_svc when stop flag not set. forcing flag set!");
        svc->lf_config.stop_flag->store(true,memory_order_set);
    }
    svc->wait_on_exit();
    if(svc->_osd_update_timer_thread_ctx.osd_handle)
        delete svc->_osd_update_timer_thread_ctx.osd_handle;
    svc->_osd_update_timer_thread_ctx.osd_handle = nullptr;

    svc->_send_rtsp_frame_thread_ctx.rtsp_server.reset();
    delete svc->_send_rtsp_frame_thread_ctx.rtsp_event_loop;

    if(svc->_send_rtsp_frame_thread_ctx.mpi_handle)
        delete svc->_send_rtsp_frame_thread_ctx.mpi_handle;
    svc->_send_rtsp_frame_thread_ctx.mpi_handle = nullptr;

}

/// private constructor
lf_mpi_svc::lf_mpi_svc(luckfox_mpi_config config):
    lf_config{config},
    _send_rtsp_frame_thread_ctx{
        .mpi_handle =  new luckfox_mpi(config.rknn_path),
        .rtsp_event_loop = new xop::EventLoop(),
        .rtsp_server = xop::RtspServer::Create(
            _send_rtsp_frame_thread_ctx.rtsp_event_loop
        ),
        .stop_flag = config.stop_flag
    },
    _osd_update_timer_thread_ctx{
        .osd_handle = new osd::text_osd(config.font_size),
        .pixel_buffer = &_pixel_buffer,
        .size = &_size,
        .update_osd_f = &_update_osd_flag,
        .stop_flag = config.stop_flag,
        .tz_info = lf_config.timezone
    },
    _send_vi_frame_thread_ctx{
        .pixel_buffer = &_pixel_buffer,
        .pixel_buffer_size = &_size,
        .update_osd_f = &_update_osd_flag,
        .stop_flag = config.stop_flag
    }
{
    RK_MPI_SYS_Init();
}



lf_mpi_svc& lf_mpi_svc::create_new(luckfox_mpi_config config)
{
    assert(config.stop_flag != nullptr);
    if(!svc_instance){
        svc_instance = new lf_mpi_svc(config);
        LOGD("lf mpi svc created new");
    }
    return *svc_instance;
}

lf_mpi_svc* lf_mpi_svc::take()
{
    return svc_instance;
}

lf_mpi::lf_mpi_svc::~lf_mpi_svc()
{
    wait_on_exit();
    RK_MPI_SYS_Exit();
}

bool lf_mpi_svc::init(){
    if(init_done.load()){
        return true;
    }
    bool ret = _send_rtsp_frame_thread_ctx.mpi_handle->init_video_in(
        lf_config.mode,
        lf_config.fps,
        lf_config.width,
        lf_config.height
    );
    if(!ret){
        LOGE("failed on init_video_in");
        return ret;
    }
    ret = _send_rtsp_frame_thread_ctx.mpi_handle->init_video_encoder(
        RK_VIDEO_ID_HEVC,
        lf_config.width,
        lf_config.height
    );
    if(!ret){
        LOGE("failed on init video encoder");
        return ret;
    }
    ret = _send_rtsp_frame_thread_ctx.rtsp_server->Start(
        lf_config.ip,
        lf_config.port
    );
    if(!ret){
        LOGE("failed on rtsp server start");
        return ret;
    }
    _send_rtsp_frame_thread_ctx.session = xop::MediaSession::CreateNew(lf_config.url);
    if(!_send_rtsp_frame_thread_ctx.session){
        LOGE("failed on MediaSession::create new");
        return false;
    }
    _send_rtsp_frame_thread_ctx.h265_source = xop::H265Source::CreateNew(lf_config.fps);
    if(!_send_rtsp_frame_thread_ctx.h265_source){
        LOGE("failed on H265Source::CreateNew");
        return false;
    }
    ret = _send_rtsp_frame_thread_ctx.session->AddSource(xop::channel_0,
        _send_rtsp_frame_thread_ctx.h265_source);
    if(!ret){
        LOGE("failed on AddSource");
        return ret;
    }
    _send_rtsp_frame_thread_ctx.session->AddNotifyConnectedCallback(
        connect_callback
    );
    _send_rtsp_frame_thread_ctx.session->AddNotifyDisconnectedCallback(
        disconnect_callback
    );
    _send_rtsp_frame_thread_ctx.session_id = 
        _send_rtsp_frame_thread_ctx.rtsp_server->AddSession(_send_rtsp_frame_thread_ctx.session);
    
    _send_vi_frame_thread_ctx.vi_get_frame = &luckfox_mpi::vi_get_frame;
    _send_vi_frame_thread_ctx.vi_release_frame = &luckfox_mpi::vi_release_frame;
    _send_vi_frame_thread_ctx.venc_send_frame = &luckfox_mpi::venc_send_frame;
    init_done.store(true);
    ret = start_vi_svc();
    if(!ret){
        LOGE("failed to start vi svc");
        return false;
    }
    ret = start_rtsp_svc();
    if(!ret){
        LOGE("failed to start rtsp svc");
        return false;
    }
    return true;
    
}

bool lf_mpi_svc::start_vi_svc()
{
    ///TODO:make use of osd enable flag
    timer_thread = std::thread(osd_update_timer_thread,&_osd_update_timer_thread_ctx);
    vi_thread =  std::thread(send_vi_frame_thread,&_send_vi_frame_thread_ctx);
    return true;
    
}

bool lf_mpi_svc::start_rtsp_svc()
{
    if(!init_done.load(memory_order_get)){
        LOGE("failed to start rtsp thread. must call init before starting thread.");
        return false;
    }
    rtsp_thread =  std::thread(send_rtsp_frame_thread,&_send_rtsp_frame_thread_ctx);
    return true;
}

void lf_mpi::lf_mpi_svc::wait_on_exit()
{
   
    if(timer_thread.joinable()){
        timer_thread.join();
        timer_thread = {};
    }
    if(vi_thread.joinable()){
        vi_thread.join();
        vi_thread = {};
    }
    if(rtsp_thread.joinable()){
        rtsp_thread.join();
        rtsp_thread = {};
    }
}


/// @brief TODO: fix artifacts when osd gets updated
/// @param thread_ctx 
void lf_mpi_svc::send_vi_frame_thread(send_vi_frame_thread_ctx * thread_ctx)
{
    if(!thread_ctx){
        LOGE("thread context can't be null exiting send_vi_frame_thread");
        return;
    }
    if(!thread_ctx->pixel_buffer || !thread_ctx->pixel_buffer_size 
        || !thread_ctx->stop_flag || !thread_ctx->update_osd_f){
        LOGE("caught null in send_vi_frame_thread_ctx exiting thread");
        return;
    }
    using px_vec = std::vector<uint8_t,mmz_alloc<uint8_t>>;
    const RgaSURF_FORMAT yuv_format = RgaSURF_FORMAT::RK_FORMAT_YCbCr_420_SP;
    const RgaSURF_FORMAT rgba_format = RgaSURF_FORMAT::RK_FORMAT_RGBA_8888;
    ///rga buffer for wrapping the osd pixel buffer
    rga_buffer_t rga_osd_buf = {0};
    ///rga buffer for vi frame
    rga_buffer_t rga_vi_yuv = {0};
    ///rga buffer for vi frame converted to rgba8888 format
    rga_buffer_t rga_vi_cvt = {0};
    ///rga buffer for post osd imposing converted back to YUV420P
    rga_buffer_t rga_venc_buf = {0};

    rga_buf::rga_buffer rga_buf_ctor;
    lf_mpi_svc* svc = lf_mpi_svc::take();
    if(!svc){
        LOGE("lf_mpi_svc is not enabled. exiting send vi thread");
        return;
    }
    auto mpi_handle = std::ref(*svc->_send_rtsp_frame_thread_ctx.mpi_handle);
    size_t vi_rgba_buffer_size = rga_buf_ctor.get_buffer_size(
        svc->lf_config.width,
        svc->lf_config.height,
        rgba_format
    );
    size_t venc_yuv_buffer_size = rga_buf_ctor.get_buffer_size(
        svc->lf_config.width,
        svc->lf_config.height,
        yuv_format
    );
    px_vec vi_rgba_buffer(vi_rgba_buffer_size);
    px_vec venc_yuv_buffer(venc_yuv_buffer_size);
    px_vec osd_rgba_buffer(1024 * 10);
    auto alloc = mmz_alloc<uint8_t>();

    uint64_t phy_addr_vi_rgba_buff = alloc.virtual_to_physical_address(&vi_rgba_buffer[0]);
    assert(phy_addr_vi_rgba_buff != 0);
    uint64_t phy_addr_venc_yuv_buff = alloc.virtual_to_physical_address(&venc_yuv_buffer[0]);
    assert(phy_addr_venc_yuv_buff != 0);
    using addr_struct = rga_buf::rga_buffer::rga_buf_addr_t;

    addr_struct addr_vi_yuv = {
        .buffer_type = rga_buf::rga_buffer::BUFFER_TYPE_PHYSICAL,
        .phy_address = 0
    };
    addr_struct addr_osd_rgba = {
        .buffer_type = rga_buf::rga_buffer::BUFFER_TYPE_PHYSICAL,
        .phy_address = 0
    };
    addr_struct addr_vi_rgba = {
        .buffer_type = rga_buf::rga_buffer::BUFFER_TYPE_PHYSICAL,
        .phy_address = phy_addr_vi_rgba_buff
    };
    addr_struct addr_venc_yuv = {
        .buffer_type = rga_buf::rga_buffer::BUFFER_TYPE_PHYSICAL,
        .phy_address = phy_addr_venc_yuv_buff
    };
    rga_buffer_handle_t res = rga_buf_ctor.create_buffer(
        svc->lf_config.width,
        svc->lf_config.height,
        rgba_format,
        addr_vi_rgba,
        &rga_vi_cvt
    );
    if(!res){
        LOGE("Failed to create rga_buffer_t for vi,exiting send vi thread");
        return;
    }
    res = rga_buf_ctor.create_buffer(
        svc->lf_config.width,
        svc->lf_config.height,
        yuv_format,
        addr_venc_yuv,
        &rga_venc_buf
    );
    if(!res){
        LOGE("Failed to create rga_buffer_t for venc,exiting send vi thread");
        return;
    }
    bool init = true;
    MB_BLK vi_mb_blk = nullptr;
    IM_STATUS im_res = IM_STATUS::IM_STATUS_FAILED;
    im_rect osd_rect = {0};
    im_osd_t osd_config = {0};
    osd::bmp_resolution size_osd{0};
    VIDEO_FRAME_INFO_S out_frame_info = {0};
    rga_buffer_handle_t osd_buffer_handle = 0;
    rga_buffer_handle_t vi_yuv_handle = 0;
    while(!thread_ctx->stop_flag->load(memory_order_get)){
        if(thread_ctx->update_osd_f->load(memory_order_get)){
            osd_rgba_buffer = *thread_ctx->pixel_buffer;
            size_osd = *thread_ctx->pixel_buffer_size;
            thread_ctx->update_osd_f->store(false,memory_order_set);
            addr_osd_rgba.phy_address = alloc.virtual_to_physical_address(&osd_rgba_buffer[0]);
            if(osd_buffer_handle){
                rga_buf_ctor.release_buffer(osd_buffer_handle);
            }
            osd_buffer_handle = rga_buf_ctor.create_buffer(
                size_osd.width,
                size_osd.height,
                rgba_format,
                addr_osd_rgba,
                &rga_osd_buf
            );
            if(!osd_buffer_handle){
                LOGE("failed to create osd rga_buffer_t exiting send vi thread");
                break;
            }
            init = false;
        }
        if(init){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        //fetch vi->import to rga->frame2rgba->osd impose->frame2yuv->send venc
        vi_mb_blk = thread_ctx->vi_get_frame(mpi_handle,&out_frame_info);
        if(vi_mb_blk){
            addr_vi_yuv.phy_address = alloc.into_physical_address(vi_mb_blk);
            vi_yuv_handle = rga_buf_ctor.create_buffer(
                svc->lf_config.width,
                svc->lf_config.height,
                yuv_format,
                addr_vi_yuv,
                &rga_vi_yuv
            );
            if(!vi_yuv_handle){
                LOGE("failed to import vi buffer to rga exiting vi thread");
                break;
            }
            im_res = imcvtcolor(
                rga_vi_yuv,
                rga_vi_cvt,
                yuv_format,
                rgba_format,
                IM_YUV_TO_RGB_BT601_LIMIT,
                1,
                nullptr
            );
            if(im_res != IM_STATUS::IM_STATUS_SUCCESS){
                LOGE("failed to convert vi frame yuv2rgba. error: %s",imStrError(im_res));
                thread_ctx->vi_release_frame(mpi_handle);
                break;
            }
            rga_buf_ctor.release_buffer(vi_yuv_handle);
            thread_ctx->vi_release_frame(mpi_handle);
            osd_rect = rga_buf::rga_buffer::get_osd_rect(
                (int32_t)size_osd.width,
                (int32_t)size_osd.height
            );
            osd_config = rga_buf::rga_buffer::get_osd_config(size_osd.width);
            im_res = imosd(rga_osd_buf,rga_vi_cvt,osd_rect,&osd_config,1,nullptr);
            if(im_res != IM_STATUS::IM_STATUS_SUCCESS){
                LOGE("failed to impose osd. error: %s",imStrError(im_res));
                break;
            }
            im_res = imcvtcolor(
                rga_vi_cvt,
                rga_venc_buf,
                rgba_format,
                yuv_format,
                IM_RGB_TO_YUV_BT601_LIMIT,
                1,
                nullptr
            );
            if(im_res != IM_STATUS::IM_STATUS_SUCCESS){
                LOGE("failed to convert rgb2yuv. error: %s",imStrError(im_res));
                break;
            }
            out_frame_info.stVFrame.pMbBlk = alloc.vir_to_handle(&venc_yuv_buffer[0]);
            assert(out_frame_info.stVFrame.pMbBlk != 0);
            thread_ctx->venc_send_frame(mpi_handle,out_frame_info);
        }///failed to get vi frame
        else{
            std::this_thread::sleep_for(milliseconds(500));
        }
    }
}

void lf_mpi_svc::osd_update_timer_thread(osd_update_timer_thread_ctx *thread_ctx)
{
    lf_mpi_svc* svc = lf_mpi_svc::take();
    if(!svc){
        LOGE("got null from lf_mpi_svc::take() exiting timer thread");
        return;
    }
    if(!thread_ctx){
        LOGE("osd_update_timer_thread_ctx *thread_ctx is null exiting timer thread");
        return;
    }
    if(!thread_ctx->osd_handle || 
        !thread_ctx->pixel_buffer || 
        !thread_ctx->size || 
        !thread_ctx->stop_flag ||
        !thread_ctx->update_osd_f){
        LOGE("got null in osd_update_timer_thread_ctx. exiting thread");
        return;
    }
    bool res = thread_ctx->osd_handle->load_ttf_file(
        svc->lf_config.font_file_path
    );
    if(!res){
        LOGE("failed to load ttf file. exiting timer thread");
    }
    AtcZoneProcessor tz_proc{0};
    AtcTimeZone tz{0};
    osd::init_local_time(
        thread_ctx->tz_info,
        &tz_proc,
        &tz
    );
    time_t raw_time = 0;
    std::string local_time_str;
    time_t next_minute = 0;
    ///the index of the seconds colon
    size_t index_sec_col = 0;
    while(!thread_ctx->stop_flag->load(memory_order_get)){
        if(!thread_ctx->update_osd_f->load(memory_order_get)) //cleared by send_vi_frame_thread
        {
            local_time_str = osd::get_local_time(tz,raw_time);
            ///remove seconds. we only want to show YYYY:MM:DD hh:mm
            index_sec_col = local_time_str.find_last_of(':');
            if(index_sec_col != std::string::npos){
                local_time_str.erase(index_sec_col);
            }
            res = thread_ctx->osd_handle->render_text_rgba(
                local_time_str,
                *thread_ctx->pixel_buffer,
                *thread_ctx->size);
            if(!res){
                LOGE("failed to get local time, exiting timer thread");
                break;
            }
            thread_ctx->update_osd_f->store(true,memory_order_set);
            next_minute = utils_get_next_minute(raw_time);
            while(std::chrono::system_clock::now() < 
                  std::chrono::system_clock::from_time_t(next_minute)){
                if(thread_ctx->stop_flag->load(memory_order_get)){
                    break;
                }
                std::this_thread::sleep_for(milliseconds(10));
            }
        }///update osd flag not cleared
        else{
            std::this_thread::sleep_for(milliseconds(2));
        }
    }
}



void lf_mpi_svc::connect_callback(xop::MediaSessionId sessionId, std::string peer_ip, 
                                        uint16_t peer_port){
    lf_mpi_svc::connected_clients.fetch_add(1,memory_order_set);
    LOGI("RTSP client connect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
    lf_mpi_svc::idr_reset.store(true,memory_order_set);
    lf_mpi_svc::client_conn_flag.store(true,memory_order_set);
}

void lf_mpi_svc::disconnect_callback(xop::MediaSessionId sessionId, std::string peer_ip, 
                                        uint16_t peer_port){
    uint32_t conns_now = lf_mpi_svc::connected_clients.fetch_sub(1,memory_order_set) - 1;
    bool st_conn_flag = conns_now > 0;
    lf_mpi_svc::client_conn_flag.store(st_conn_flag,memory_order_set);
}

void lf_mpi_svc::send_rtsp_frame_thread(send_rtsp_frame_thread_ctx *thread_ctx)
{
    if(!thread_ctx){
        LOGE("thread_ctx can't be null. exiting send_venc_frame_thread");
        return;
    }
    if(!thread_ctx->mpi_handle){
        LOGE("thread_ctx->mpi_handle can't be null. exiting send_venc_frame_thread");
        return;
    }
    luckfox_mpi* mpi_handle = thread_ctx->mpi_handle;
    lf_mpi_svc* svc = lf_mpi_svc::take();
    if(!svc){
        LOGE("lf_mpi_svc::take(). returned null. exiting send_venc_frame_thread");
        return;
    }
    if(!svc->init_done.load(memory_order_get)){
        LOGE("send_venc_frame_thread called before init. exiting thread.");
        return;
    }
    bool res = mpi_handle->start_video_encoder(svc->lf_config.osd_enable);
    if(!res){
        LOGE("failed to start video encoder. exiting send_venc_frame_thread");
        return;
    }
    size_t data_len = 0;
    ///timestamp
    uint64_t ts = 0;
    uint8_t* venc_stream = nullptr;
    xop::AVFrame video_frame{};
    while(!thread_ctx->stop_flag->load(memory_order_get)){
        if(!svc->client_conn_flag.load(memory_order_get)){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        video_frame.buffer.reset();
        venc_stream = 
            mpi_handle->venc_get_stream(svc->idr_reset.load(memory_order_get),&data_len,&ts);
        if(!venc_stream || data_len == 0){
            LOGE("Failed to get venc stream");
            return;
        }
        svc->idr_reset.store(false,memory_order_set);
        video_frame.size = data_len;
        video_frame.buffer.reset(venc_stream,lf_mpi::VencStreamDeleter{mpi_handle});
        xop::H265Source::GetTimestamp(&video_frame.timeNow,&video_frame.timestamp);
        thread_ctx->rtsp_server->PushFrame(thread_ctx->session_id,xop::channel_0,video_frame);
    }
}

inline time_t lf_mpi_svc::utils_get_next_minute(time_t start_time){
    std::tm tm = *std::localtime(&start_time);  // Convert time_t to local tm
    tm.tm_sec = 0;                              // Clear seconds
    if (tm.tm_min == 59) {                      // Handle minute rollover
        tm.tm_min = 0;
        tm.tm_hour = (tm.tm_hour + 1) % 24;
    } else {
        tm.tm_min++;
    }
    std::time_t next_minute = std::mktime(&tm); // Normalize tm to time_t
    return next_minute;
}