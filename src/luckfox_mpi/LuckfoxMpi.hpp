#pragma once

#include "sample_comm.h"
#include <pthread.h>
#include <atomic>
#include <string>
#include "im2d.h"
#include "osd.hpp"
/*
* RK_API:
*   *_channel = module output/input interface. 
*   each module has an output channel that can be binded to the input
*   channel of the next module.
*   data access can be done manuly to retrive the data on each channel
*   or by binding.
*   usually the for last module in the chain the channel will be accessed 
*   manually. 
*   sc3336 pixel format : RK_FMT_RGB_BAYER_SBGGR_10BPP
*   flow : vin->rga(OSD overlay)->venc->rtsp in.
*/

namespace lf_mpi {


class LuckfoxMpi
{
public:
    LuckfoxMpi(std::string rknn_path="/oem/usr/share/iqfiles");
    bool init_video_in(rk_aiq_working_mode_t mode,int32_t fps,uint32_t width,uint32_t height);
    bool init_vpss();
    bool init_video_encoder(RK_CODEC_ID_E codec,uint32_t width,uint32_t height);
    bool start_video_encoder(bool osd_enable);
    uint8_t* venc_get_stream(bool restart,size_t *stream_len,uint64_t* timestamp);
    bool venc_release_stream();
    MB_BLK vi_get_frame(VIDEO_FRAME_INFO_S* out_frame_info);
    bool venc_send_frame(VIDEO_FRAME_INFO_S& frame_info);
    void vi_release_frame();
    bool venc_restart();
    bool bind_vin_vpss();
    bool bind_vin_venc();
    bool bind_vpss_venc();
    ~LuckfoxMpi();

private:
    typedef struct {
        VI_DEV s32DevId;
        VI_PIPE u32PipeId;
        VI_CHN s32ChnId;
        VI_DEV_ATTR_S stDevAttr;
        VI_DEV_BIND_PIPE_S stBindPipe;
        VI_CHN_ATTR_S stChnAttr;
        VIDEO_FRAME_INFO_S stViFrame;
        VI_CHN_STATUS_S stChnStatus;
        rk_aiq_working_mode_t hdr_mode;
        RK_U32 vi_width;
        RK_U32 vi_height;
        RK_U32 vi_fps;
        rk_aiq_sys_ctx_t* aiq_ctx;
        rk_aiq_static_info_t aiq_static_info;
        VIDEO_FRAME_INFO_S frame_info;
    bool b_vi_channel_en;
    }_luckfox_mpi_vi_ctx;
    typedef struct {
        VPSS_GRP s32GrpId;
        VPSS_CHN s32ChnId;
        VPSS_GRP_ATTR_S stGrpVpssAttr;
        VIDEO_PROC_DEV_TYPE_E enVProcDevType;
        VPSS_CHN_ATTR_S stVpssChnAttr;
        VIDEO_FRAME_INFO_S stChnFrameInfos;
        bool b_vpss_en;
    }_luckfox_mpi_vpss_ctx;
    typedef struct{
        VENC_RC_MODE_E enRcMode;
        VENC_CHN s32ChnId;
        VENC_CHN_ATTR_S stChnAttr;
        VENC_STREAM_S stFrame;
        bool b_venc_en;
    }_luckfox_mpi_venc_ctx;
    bool init_vi();
    const int32_t vi_buf_count = 1; 
    int32_t vi_dev_id = 0;
    const uint32_t vpss_max_width = 4096;
    const uint32_t vpss_max_height= 4096;
    int32_t channel_id = 0;   
   
    
    struct _luckfox_mpi_ctx{
        std::string rknn_path;
        _luckfox_mpi_vi_ctx video_in;
        _luckfox_mpi_vpss_ctx vpss;
        _luckfox_mpi_venc_ctx video_encoder;
        bool osd_enable;
   };
   struct {
    bool vi_enabled;
    bool venc_enabled;
    bool venc_start_rcv;
    bool vi_bind_venc;
    std::atomic<bool>stream_locked{false};
   }enabled_flags = {0};
    VENC_STREAM_S pstStream = {0};
    VENC_PACK_S pstPack = {0};
    _luckfox_mpi_ctx mpi_ctx;

    struct{
        rga_buffer_handle_t vi_rgba_buf_handle = 0;
        rga_buffer_handle_t venc_yuv_buf_handle = 0;
        rga_buffer_t vi_rga_buf = {0};
        rga_buffer_t venc_rga_buf = {0};
        MB_BLK cvt_image_blk = nullptr;
        MB_BLK venc_image_blk = nullptr;
        MB_BLK osd_image_blk;
        uint64_t cvt_image_phy_address = 0;
        uint64_t venc_image_phy_address = 0;
        bool init_done = false;
        const RgaSURF_FORMAT rgba_format = RgaSURF_FORMAT::RK_FORMAT_RGBA_8888;
        const RgaSURF_FORMAT yuv_format = RgaSURF_FORMAT::RK_FORMAT_YCbCr_420_SP;
        const size_t MAX_OSD_WIDTH = 1024;
        const size_t MAX_OSD_HEIGHT = 128;
    }osd_handles = {};
   
};

    
}
