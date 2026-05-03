#include <memory>
#include <chrono>
#include "lf_mpi_svc.hpp"
#include "lf_types.hpp"
#include "generic_log.h"
#include "acetimec.h"
#include "mmz_alloc.hpp"
#include "routing.hpp"

using namespace lf_mpi;
using namespace std::chrono;
using namespace routing;

constexpr std::memory_order memory_order_set = std::memory_order_seq_cst;
constexpr std::memory_order memory_order_get = std::memory_order_seq_cst;

void lf_mpi::MpiSvc::exit_svc()
{
    MpiSvc* svc = MpiSvc::take();
    if(!svc){
        return;
    }
    if(!svc->lf_config.stop_flag->load(memory_order_get)){
        LOGW("called exit_svc when stop flag not set. forcing flag set!");
        svc->lf_config.stop_flag->store(true,memory_order_set);
    }
    svc->wait_on_exit();
    if(svc->_osd_update_timer_thread_ctx.osd_handle)
        delete svc->_osd_update_timer_thread_ctx.osd_handle;
    svc->_osd_update_timer_thread_ctx.osd_handle = nullptr;

    svc->_send_rtsp_frame_thread_ctx.rtsp_server.reset();
    delete svc->_send_rtsp_frame_thread_ctx.rtsp_event_loop;

    if(svc->mpi_handle)
        delete svc->mpi_handle;
    svc->mpi_handle = nullptr;

}

MpiSvc::MpiSvc(LuckfoxMpiConfig config):
    lf_config{config},
    mpi_handle{new LuckfoxMpi(config.rknn_path)},
    _pixel_buffer(4096),
    _send_rtsp_frame_thread_ctx{
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



MpiSvc& MpiSvc::create_new(LuckfoxMpiConfig config)
{
    assert(config.stop_flag != nullptr);
    if(!svc_instance){
        svc_instance = new MpiSvc(config);
        LOGD("lf mpi svc created new");
    }
    return *svc_instance;
}

MpiSvc* MpiSvc::take()
{
    return svc_instance;
}

lf_mpi::MpiSvc::~MpiSvc()
{
    wait_on_exit();
    RK_MPI_SYS_Exit();
}

bool MpiSvc::init(){
    if(init_done.load(memory_order_get)){
        return true;
    }
    bool ret = mpi_handle->init_video_in(
        lf_config.mode,
        lf_config.fps,
        lf_config.width,
        lf_config.height
    );
    if(!ret){
        LOGE("failed on init_video_in");
        return ret;
    }
    set_venc_res(_send_vi_frame_thread_ctx);
    if(!init_vi_transform(transfrom_ctx)){
        LOGE("failed to init vi transform");
        return false;
    }
    ret = mpi_handle->init_video_encoder(
        RK_VIDEO_ID_HEVC,
        _send_vi_frame_thread_ctx.venc_frame_res.width,
        _send_vi_frame_thread_ctx.venc_frame_res.height
    );
    if(!ret){
        LOGE("failed on init video encoder");
        return ret;
    }
    ret = mpi_handle->start_video_encoder();
    if(!ret){
        LOGE("failed to start video encoder");
        return false;
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
    
    _send_vi_frame_thread_ctx.vi_get_frame = &LuckfoxMpi::vi_get_frame;
    _send_vi_frame_thread_ctx.vi_release_frame = &LuckfoxMpi::vi_release_frame;
    _send_vi_frame_thread_ctx.venc_send_frame = &LuckfoxMpi::venc_send_frame;
    ret = false;
    ///TODO:allow resizing with vpss_config->venc->rtsp
    ///TODO:allow seperate channel in venc output for file archiving to sd card
    switch (lf_config.vi_binding)
    {
        case MpiViBindTo::OSD:{
            ret = start_vi_svc();
            if(!ret){
                LOGE("failed to start vi svc");
                return false;
            }
            break;
        }
        case MpiViBindTo::VENC:{
            ret = mpi_handle->bind_vin_venc();
            if(!ret){
                return false;
            }
            break;
        }
    }
    init_done.store(true);
    if(lf_config.rtsp_enable){
        ret = start_rtsp_svc();
        if(!ret){
            LOGE("failed to start rtsp svc");
            return false;
        }
    }
    if(!ret){
        LOGW("called MpiSvc::init without starting any service.");
    }

    return true;
    
}

bool MpiSvc::start_vi_svc()
{
    timer_thread = std::thread(osd_update_timer_thread,&_osd_update_timer_thread_ctx);
    vi_thread =  std::thread(send_vi_frame_thread,&_send_vi_frame_thread_ctx);
    return true;
    
}

bool MpiSvc::start_rtsp_svc()
{
    if(!init_done.load(memory_order_get)){
        LOGE("failed to start rtsp thread. must call init before starting thread.");
        return false;
    }
    rtsp_thread =  std::thread(send_rtsp_frame_thread,&_send_rtsp_frame_thread_ctx);
    return true;
}

void lf_mpi::MpiSvc::wait_on_exit()
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


void MpiSvc::send_vi_frame_thread(send_vi_frame_thread_ctx * thread_ctx)
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
    ///rga buffer for post osd imposing converted back to YUV420P
    rga_buffer_t rga_venc_buf = {0};

    rga_buf::rga_buffer rga_buf_ctor;
    MpiSvc* svc = MpiSvc::take();
    if(!svc){
        LOGE("lf_mpi_svc is not enabled. exiting send vi thread");
        return;
    }
    auto mpi_handle = std::ref(*svc->mpi_handle);
    size_t venc_yuv_buffer_size = rga_buf_ctor.get_buffer_size(
        static_cast<uint32_t>(thread_ctx->venc_frame_res.width),
        static_cast<uint32_t>(thread_ctx->venc_frame_res.height),
        yuv_format
    );
    px_vec venc_yuv_buffer(venc_yuv_buffer_size);
    px_vec osd_rgba_buffer(1024 * 10);
    auto alloc = mmz_alloc<uint8_t>();

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
    addr_struct addr_venc_yuv = {
        .buffer_type = rga_buf::rga_buffer::BUFFER_TYPE_PHYSICAL,
        .phy_address = phy_addr_venc_yuv_buff
    };

    rga_buffer_handle_t vi_yuv_handle = rga_buf_ctor.create_buffer(
        static_cast<uint32_t>(thread_ctx->venc_frame_res.width),
        static_cast<uint32_t>(thread_ctx->venc_frame_res.height),
        yuv_format,
        addr_venc_yuv,
        &rga_venc_buf
    );
    if(!vi_yuv_handle){
        LOGE("Failed to create rga_buffer_t for venc,exiting send vi thread");
        return;
    }
    bool init = true;
    MB_BLK vi_mb_blk = nullptr;
    IM_STATUS im_res = IM_STATUS::IM_STATUS_FAILED;
    im_rect osd_rect = {0};
    osd::bmp_resolution size_osd{0};
    VIDEO_FRAME_INFO_S out_frame_info = {0};
    rga_buffer_handle_t osd_buffer_handle = 0;

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
        //fetch vi->import to rga->copy->osd impose->frame2yuv->send venc
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
            if(svc->transfrom_ctx.init_done){
                TransformResult tres = svc->apply_vi_transform(svc->transfrom_ctx,rga_vi_yuv);
                if(tres.ok){
                    rga_buf_ctor.release_buffer(rga_vi_yuv.handle);
                    rga_vi_yuv = tres.rga_buffer;
                }
            }      
            osd_rect = rga_buf::rga_buffer::get_osd_rect(
                (int32_t)size_osd.width,
                (int32_t)size_osd.height
            );
            /**
             * refer to github.com/airockchip/librga/blob/main/samples/alpha_demo/src/rga_alpha_yuv_demo.cpp
             */
            const im_rect prect{
                .x = 0,
                .y = 0,
                .width = osd_rect.width,
                .height = osd_rect.height
            };
            ///TODO: maybe there is a way to do it without copy
            im_res = imcopy(rga_vi_yuv,rga_venc_buf,1,nullptr);
            
            const int32_t imp_usage = IM_SYNC | IM_ALPHA_BLEND_DST_OVER;
            im_res = imcheck_composite(rga_vi_yuv,rga_venc_buf,rga_osd_buf,osd_rect,osd_rect,prect);
            if(im_res != IM_STATUS::IM_STATUS_NOERROR){
                LOGE("imcheck_composite failed. error:%s",imStrError(im_res));
                break;
            }
            im_res = improcess(rga_vi_yuv,rga_venc_buf,rga_osd_buf,osd_rect,
                                osd_rect,prect,-1,nullptr,nullptr,imp_usage);
            if(im_res != IM_STATUS::IM_STATUS_SUCCESS){
                LOGE("failed to impose osd. error: %s",imStrError(im_res));
                break;
            }
            thread_ctx->vi_release_frame(mpi_handle);
            out_frame_info.stVFrame.pMbBlk = alloc.vir_to_handle(&venc_yuv_buffer[0]);
            out_frame_info.stVFrame.u32Width = rga_vi_yuv.width;
            out_frame_info.stVFrame.u32Height = rga_vi_yuv.height;
            out_frame_info.stVFrame.u32VirWidth = rga_vi_yuv.width;
            out_frame_info.stVFrame.u32VirHeight = rga_vi_yuv.height;
            thread_ctx->venc_send_frame(mpi_handle,out_frame_info);
        }///failed to get vi frame
        else{
            std::this_thread::sleep_for(milliseconds(500));
        }
    }
}

void MpiSvc::osd_update_timer_thread(osd_update_timer_thread_ctx *thread_ctx)
{
    MpiSvc* svc = MpiSvc::take();
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
        return;
    }
    
    std::string ch_db = "abcdefghijklmnopqrstuvwxyz123456789-: ";
    if(!thread_ctx->osd_handle->init_glyph_map(ch_db)){
        LOGE("error. failed to init map");
    }
    svc->_pixel_buffer.resize(500*64*4);
    AtcZoneProcessor tz_proc{0};
    AtcTimeZone tz{0};
    osd::init_local_time(
        thread_ctx->tz_info,
        &tz_proc,
        &tz
    );
    time_t raw_time = 0;
    std::string local_time_str;
    while(!thread_ctx->stop_flag->load(memory_order_get)){
        if(!thread_ctx->update_osd_f->load(memory_order_get)) //cleared by send_vi_frame_thread
        {
            local_time_str = osd::get_local_time(tz,raw_time);
            res = thread_ctx->osd_handle->render_text_rgba_with_glyph_map(
                local_time_str,
                *thread_ctx->pixel_buffer,
                *thread_ctx->size);
            
            if(!res){
                LOGE("failed to get local time, exiting timer thread");
                break;
            }
            thread_ctx->update_osd_f->store(true,memory_order_set);
            int64_t ms_passed = 0;
            while(ms_passed < 1000){
                if(thread_ctx->stop_flag->load(memory_order_get)){
                    break;
                }
                ms_passed += 10;
                std::this_thread::sleep_for(milliseconds(10));
            }
        }///update osd flag not cleared
        else{
            std::this_thread::sleep_for(milliseconds(2));
        }
    }
}



void MpiSvc::connect_callback(xop::MediaSessionId sessionId, std::string peer_ip, 
                                        uint16_t peer_port){
    MpiSvc::connected_clients.fetch_add(1,memory_order_set);
    LOGI("RTSP client connect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
    MpiSvc::idr_reset.store(true,memory_order_set);
    MpiSvc::client_conn_flag.store(true,memory_order_set);
}

void MpiSvc::disconnect_callback(xop::MediaSessionId sessionId, std::string peer_ip, 
                                        uint16_t peer_port){
    uint32_t conns_now = MpiSvc::connected_clients.fetch_sub(1,memory_order_set) - 1;
    bool st_conn_flag = conns_now > 0;
    MpiSvc::client_conn_flag.store(st_conn_flag,memory_order_set);
}

void MpiSvc::send_rtsp_frame_thread(send_rtsp_frame_thread_ctx *thread_ctx)
{
    if(!thread_ctx){
        LOGE("thread_ctx can't be null. exiting send_venc_frame_thread");
        return;
    }
    MpiSvc* svc = MpiSvc::take();
    if(!svc){
        LOGE("lf_mpi_svc::take(). returned null. exiting send_venc_frame_thread");
        return;
    }
    if(!svc->mpi_handle){
        LOGE("mpi_handle can't be null existing %s",__FUNCTION__);
        return;
    }
    if(!svc->init_done.load(memory_order_get)){
        LOGE("send_venc_frame_thread called before init. exiting thread.");
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
            svc->mpi_handle->venc_get_stream(svc->idr_reset.load(memory_order_get),&data_len,&ts);
        if(!venc_stream || data_len == 0){
            LOGE("Failed to get venc stream");
            raise(SIGINT);
            return;
        }
        svc->idr_reset.store(false,memory_order_set);
        video_frame.size = data_len;
        video_frame.buffer.reset(venc_stream,lf_mpi::VencStreamDeleter{svc->mpi_handle});
        xop::H265Source::GetTimestamp(&video_frame.timeNow,&video_frame.timestamp);
        thread_ctx->rtsp_server->PushFrame(thread_ctx->session_id,xop::channel_0,video_frame);
    }
}


/// TODO: fix cropping not working
/// TODO: fix resizing not working
bool MpiSvc::init_vi_transform(struct frame_transform_ctx& ctx){
    using frame_size = osd::bmp_resolution;
    using address = rga_buf::rga_buffer::rga_buf_addr_t;
    using address_type = rga_buf::rga_buffer::rga_buffer_type_t;

    bool vi_transform = lf_config.crop_vi_frame || lf_config.resize_vi_frame || lf_config.rotate_vi_frame;
    bool crop_and_rotate = lf_config.crop_vi_frame && lf_config.rotate_vi_frame;
    bool resize_and_rotate = lf_config.resize_vi_frame && lf_config.rotate_vi_frame;
    if(ctx.init_done || !vi_transform){
       return true;
    }
    if(_send_vi_frame_thread_ctx.venc_frame_res.width == 0 || 
        _send_vi_frame_thread_ctx.venc_frame_res.height == 0){
            LOGE("must call set_venc_res before init_vi_transform");
            ctx.init_done = false;
            return false;
    }
    bool alloc_dst{},alloc_rot_dst{};
    frame_size dst_fsz{},rot_dst_fsz{};
    //pre-allocate rga_buffer_t
    //pre-allocate rga_buffer_vectors
    if(lf_config.rotate_vi_frame && !crop_and_rotate && !resize_and_rotate){
        alloc_rot_dst = true;
        rot_dst_fsz = _send_vi_frame_thread_ctx.venc_frame_res;
    }else{
        alloc_dst = true;
        dst_fsz.assign_from_u32(lf_config.resize_or_crop_width,lf_config.resize_or_crop_height);
        alloc_rot_dst = true;
        rot_dst_fsz = _send_vi_frame_thread_ctx.venc_frame_res;
    }

    size_t vec_size = 0;
    if(alloc_rot_dst){
        vec_size = ctx.rga_buf_ctor.get_buffer_size(
            static_cast<uint32_t>(rot_dst_fsz.width),
            static_cast<uint32_t>(rot_dst_fsz.height),
            YUV_RGA_FORMAT
        );
        try{
            ctx.rot_dst_vec.resize(vec_size);
        }catch(const std::bad_alloc& e){
            LOGE("%s,%s,%d",e.what(),__FUNCTION__,__LINE__);
            ctx.init_done = false;
            return false;
        }
        address dst_addr{
            .buffer_type = address_type::BUFFER_TYPE_PHYSICAL,
            .phy_address = ctx.rot_dst_vec.get_allocator().virtual_to_physical_address(ctx.rot_dst_vec.data())
        };
        ctx.rot_dst_handle = ctx.rga_buf_ctor.create_buffer(
            static_cast<uint32_t>(rot_dst_fsz.width),
            static_cast<uint32_t>(rot_dst_fsz.height),
            YUV_RGA_FORMAT,
            dst_addr,
            &ctx.rot_dst_buf
        );
        if(!ctx.rot_dst_handle){
            ctx.init_done = false;
            return false;
        }
    }
    if(alloc_dst){
        vec_size = ctx.rga_buf_ctor.get_buffer_size(
            static_cast<uint32_t>(dst_fsz.width),
            static_cast<uint32_t>(dst_fsz.height),
            YUV_RGA_FORMAT
        );
        try{
            ctx.dst_vec.resize(vec_size);
        }catch(const std::bad_alloc& e){
            LOGE("%s,%s,%d",e.what(),__FUNCTION__,__LINE__);
            ctx.init_done = false;
            return false;
        }
        address dst_addr{
            .buffer_type = address_type::BUFFER_TYPE_PHYSICAL,
            .phy_address = ctx.dst_vec.get_allocator().virtual_to_physical_address(ctx.dst_vec.data())
        };
        ctx.dst_handle = ctx.rga_buf_ctor.create_buffer(
            static_cast<uint32_t>(dst_fsz.width),
            static_cast<uint32_t>(dst_fsz.height),
            YUV_RGA_FORMAT,
            dst_addr,
            &ctx.dst_buf
        );
        if(!ctx.dst_handle){
            ctx.init_done = false;
            return false;
        }
    }
    ctx.crop_and_rotate = crop_and_rotate;
    ctx.resize_and_rotate = resize_and_rotate;
    ctx.rotate = lf_config.rotate_vi_frame;
    ctx.init_done = true;
    return true;
}

TransformResult MpiSvc::apply_vi_transform(struct frame_transform_ctx& ctx,rga_buffer_t& rga_vi_yuv){
    if(!ctx.init_done || !rga_vi_yuv.handle){
        return TransformResult();
    }
    IM_STATUS status = IM_STATUS::IM_STATUS_FAILED;
    if(ctx.crop_and_rotate){
        im_rect crop_rect{
            .x = static_cast<int>(lf_config.crop_x),
            .y = static_cast<int>(lf_config.crop_y),
            .width = ctx.dst_buf.width,
            .height = ctx.dst_buf.height
        };
        status = imcrop(
            rga_vi_yuv,
            ctx.dst_buf,
            crop_rect,
            1,
            nullptr
        );
        if(status != IM_STATUS::IM_STATUS_SUCCESS){
            LOGE("crop failed. %s",imStrError(status));
            return TransformResult();
        }
        status = imrotate(
            ctx.dst_buf,
            ctx.rot_dst_buf,
            lf_config.rotation_opts,
            1,
            nullptr
        );
        if(status != IM_STATUS::IM_STATUS_SUCCESS){
            return TransformResult();
        }
        return TransformResult(ctx.rot_dst_buf,true);
    }else if(ctx.resize_and_rotate){
        status = imresize(
            rga_vi_yuv,
            ctx.dst_buf,
            0,
            0,
            IM_INTERP_DEFAULT,
            1,
            nullptr
        );
        if(status != IM_STATUS::IM_STATUS_SUCCESS){
            LOGE("imresize failed. %s",imStrError(status));
            return TransformResult();
        }
        status = imrotate(
            ctx.dst_buf,
            ctx.rot_dst_buf,
            lf_config.rotation_opts,
            1,
            nullptr
        );
        if(status != IM_STATUS::IM_STATUS_SUCCESS){
            return TransformResult();
        }
        return TransformResult(ctx.rot_dst_buf,true);
    }else if(ctx.rotate){
        status = imrotate(
            rga_vi_yuv,
            ctx.rot_dst_buf,
            lf_config.rotation_opts,
            1,
            nullptr
        );
        if(status != IM_STATUS::IM_STATUS_SUCCESS){
            LOGE("imrotate failed.%s",imStrError(status));
            return TransformResult();
        }
        return TransformResult(ctx.rot_dst_buf,true);
    }else{
        return TransformResult();
    }
}

void MpiSvc::set_venc_res(send_vi_frame_thread_ctx& ctx){
    bool vi_transform = lf_config.crop_vi_frame || lf_config.resize_vi_frame || lf_config.rotate_vi_frame;
    bool crop_and_rotate = lf_config.crop_vi_frame && lf_config.rotate_vi_frame;
    bool resize_and_rotate = lf_config.resize_vi_frame && lf_config.rotate_vi_frame;
    if(vi_transform){
        if(crop_and_rotate || resize_and_rotate){
            if(lf_config.rotation_opts == ROT_90 || lf_config.rotation_opts == ROT_270){
                ctx.venc_frame_res.assign_from_u32(lf_config.resize_or_crop_height,lf_config.resize_or_crop_width);
            }else{
                ctx.venc_frame_res.assign_from_u32(lf_config.resize_or_crop_width,lf_config.resize_or_crop_height);
            }
        }else if(lf_config.resize_vi_frame || lf_config.crop_vi_frame){
           ctx.venc_frame_res.assign_from_u32(lf_config.resize_or_crop_width,lf_config.resize_or_crop_height);
        }else if(lf_config.rotate_vi_frame){
            if(lf_config.rotation_opts == ROT_90 || lf_config.rotation_opts == ROT_270){
                ctx.venc_frame_res.assign_from_u32(lf_config.height,lf_config.width);
            }else{
                ctx.venc_frame_res.assign_from_u32(lf_config.width,lf_config.height);
            }
        }else{
            ctx.venc_frame_res.assign_from_u32(lf_config.width,lf_config.height);
        }
    }else{
        ctx.venc_frame_res.assign_from_u32(lf_config.width,lf_config.height);
    }
}

inline time_t MpiSvc::utils_get_next_minute(time_t start_time){
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