#pragma once

#include <sample_comm.h>
#include <pthread.h>

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
*   flow : vin->vpss(OSD overlay & format conversion)->venc->rtsp in.
*/
struct _luckfox_mpi_ctx;
struct _luckfox_mpi_vi_ctx;
struct _luckfox_mpi_vpss_ctx;
struct _luckfox_mpi_venc_ctx;







class luckfox_mpi
{
public:
    luckfox_mpi(const char* rknn_path="/oem/usr/share/iqfiles");
    bool init_video_in(rk_aiq_working_mode_t mode,int32_t fps,uint32_t width,uint32_t height);
    bool init_vpss();
    bool init_video_encoder(RK_CODEC_ID_E codec);
    bool venc_get_stream(uint8_t* p_data,size_t* stream_len);
    bool venc_release_stream();
    bool bind_vin_vpss();
    bool bind_vpss_venc();
    ~luckfox_mpi();

private:
    bool init_vi();
    RK_S32 init_venc();
    void xcam_fourcc_str(char*buffer,uint32_t fourcc);


    const int32_t vi_buf_count = 4; 
    int32_t vi_dev_id = 0;
    const uint32_t vpss_max_width = 4096;
    const uint32_t vpss_max_height= 4096;
    struct _luckfox_mpi_vi_ctx{
        RK_BOOL bWrapIfEnable;
        RK_BOOL bIfOpenEptz;
        RK_BOOL bIfIspGroupInit;
        RK_U32 u32BufferLine;
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
        bool b_vi_channel_en;
   };    
   struct _luckfox_mpi_vpss_ctx{
        VPSS_GRP s32GrpId;
        VPSS_CHN s32ChnId;
        VPSS_GRP_ATTR_S stGrpVpssAttr;
        VIDEO_PROC_DEV_TYPE_E enVProcDevType;
        VPSS_CHN_ATTR_S stVpssChnAttr;
        VIDEO_FRAME_INFO_S stChnFrameInfos;
        bool b_vpss_en;
    };
    struct _luckfox_mpi_venc_ctx{
        VENC_RC_MODE_E enRcMode;
        VENC_CHN s32ChnId;
        VENC_CHN_ATTR_S stChnAttr;
        VENC_STREAM_S stFrame;
        bool b_venc_en;
    };
    struct _luckfox_mpi_ctx{
        const char* rknn_path;
        _luckfox_mpi_vi_ctx video_in;
        _luckfox_mpi_vpss_ctx vpss;
        _luckfox_mpi_venc_ctx video_encoder;
        SAMPLE_RGN_CTX_S rgn; //for OSD image modification
   };
   
    _luckfox_mpi_ctx mpi_ctx = {0};
   
};

