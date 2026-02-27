#include "luckfox_mpi.hpp"
#include <iostream>
#include "generic_log.h"


#define RK_ALIGN(x, a) (((x) + (a)-1) & ~((a)-1))
#define RK_ALIGN_2(x) RK_ALIGN(x, 2)

luckfox_mpi::luckfox_mpi(const char *rknn_path)
{
    mpi_ctx.rknn_path = rknn_path;
}

bool luckfox_mpi::init_video_in(rk_aiq_working_mode_t mode, int32_t fps,uint32_t width,uint32_t height)
{
    mpi_ctx.video_in.vi_width = width;
    mpi_ctx.video_in.vi_height = height;
    mpi_ctx.video_in.hdr_mode = mode;
    mpi_ctx.video_in.vi_fps   = fps;
    bool result = init_vi();
    if(result){
        LOGI("init_video_in done.");
    }else{
        LOGE("init video failed");
    }
    return result;
}
bool luckfox_mpi::init_video_encoder(RK_CODEC_ID_E codec)
{   
    
    (void)(codec);//TODO:enable setting codec(H264/H265/JPEG)
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
    mpi_ctx.video_encoder.s32ChnId = 0;
    mpi_ctx.video_encoder.enRcMode = VENC_RC_MODE_H265CBR;
    mpi_ctx.video_encoder.stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32Profile = H265E_PROFILE_MAIN10; //main 10 profile
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32PicWidth = 2304;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32PicHeight= 1296;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32MaxPicWidth = 2304;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32MaxPicHeight= 1296;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.u32Gop = 50;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateDen= 1;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = 25;//TODO:pass from vpss
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = 25;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32VirWidth = 2304;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32VirHeight = 1296;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32StreamBufCnt = 4;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32BufSize = 2304 * 1296 / 2;
    int32_t result = RK_MPI_VENC_CreateChn(mpi_ctx.video_encoder.s32ChnId,
                        &mpi_ctx.video_encoder.stChnAttr);
    if(result != RK_SUCCESS){
        LOGE("failed to create video encoder channel. error code:%d",result);
        return false;
    }
    VENC_CHN_REF_BUF_SHARE_S stVencChnRefBufShare = {RK_TRUE};
    result = RK_MPI_VENC_SetChnRefBufShareAttr(0,&stVencChnRefBufShare);
    if(result != RK_SUCCESS){
        LOGE("failed to set venc channel ref buffer share. error code:%d",result);
        return false;
    }
    VENC_RC_PARAM_S pstRcParam = {0};
    pstRcParam.s32FirstFrameStartQp = 26;
    pstRcParam.stParamH265.u32StepQp = 8;
    pstRcParam.stParamH265.u32MaxQp  = 51;
    pstRcParam.stParamH265.u32MinQp  = 20;
    pstRcParam.stParamH265.u32FrmMaxIQp = 46;
    pstRcParam.stParamH265.u32MinIQp = 24;
    pstRcParam.stParamH265.s32DeltIpQp = 2;
    pstRcParam.stParamH265.s32MaxReEncodeTimes = 1;
    
    result = RK_MPI_VENC_SetRcParam(mpi_ctx.video_encoder.s32ChnId,&pstRcParam);
    if(result != RK_SUCCESS){
        LOGE("failed to set video encoder rc params. error code:%d",result);
        return false;
    }
    VENC_RC_PARAM_S test_rc = {0};
    result = RK_MPI_VENC_GetRcParam(0,&test_rc);
    if(result != RK_SUCCESS){
        LOGE("failed to set video encoder rc params. error code:%d",result);
        return false;
    }
    
    RK_MPI_VENC_EnableSvc(0,RK_TRUE);
    bind_vpss_venc();
    VENC_RECV_PIC_PARAM_S stRecvParam = {0};
    stRecvParam.s32RecvPicNum = 1;

	result = RK_MPI_VENC_StartRecvFrame(0, &stRecvParam);
	if (result != RK_SUCCESS) {
		LOGE("create ch venc failed. error code:%d", result);
		return false;
	}
    return true;
}


bool luckfox_mpi::init_vi()
{
    //1. init device
    int32_t result = RK_FAILURE;
    char hdr_env_var[16] = {0};
    snprintf(hdr_env_var,sizeof(hdr_env_var),"%d",(int)mpi_ctx.video_in.hdr_mode);
    setenv("HDR_MODE",hdr_env_var,1);
    LOGD("enumarating static metas by phy ID");
    result = 
        rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(vi_dev_id,&mpi_ctx.video_in.aiq_static_info);
    if(result != XCAM_RETURN_NO_ERROR){
        LOGE("failed to enumarate sensor by phy ID. \
                result:%d, line:%d, file:%s",result,__LINE__,__FILE__);
        return false;
    }
    LOGD("sensor enumarated by ID.");
    //2. set video in device buffer count
    result = 
        rk_aiq_uapi2_sysctl_preInit_devBufCnt(mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name,
                                             "rkraw_rx",vi_buf_count);
    if(result != XCAM_RETURN_NO_ERROR){
        LOGE("failed to set buffers for sensor.\
                result:%d, line:%d, file:%s",result,__LINE__,__FILE__);
        return false;
    }
    LOGD("device buffer count init");
    //2.5 set preinit scene
    result =  rk_aiq_uapi2_sysctl_preInit_scene(mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name,
                                            "normal",
                                            "day");
    if(result != RK_SUCCESS){
        LOGE("failed to set preinit scene. error code:%d",result);
    }
    //3. initialize control system context
    mpi_ctx.video_in.aiq_ctx = 
        rk_aiq_uapi2_sysctl_init(mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name,
                                mpi_ctx.rknn_path,
                                NULL,NULL);
    if(!mpi_ctx.video_in.aiq_ctx){
        LOGE("failed to initialize sysctl.\
             line: %d,file:%s",__LINE__,__FILE__);
        return false;
    }
    LOGD("init sysctl done.");
    LOGD("sensor name:%s",mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name);
    LOGD("width:%d",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->width);
    LOGD("height:%d",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->height);
    LOGD("fps:%d",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->fps);
    LOGD("multi_isp_extended_pixel:%d",mpi_ctx.video_in.aiq_static_info.multi_isp_extended_pixel);
    LOGD("fourcc :%u",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt[0].format);
    char fourcc_buf[5] = {0};
    xcam_fourcc_str(fourcc_buf,mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt[0].format);
    LOGD("sensor pixel format:%s",fourcc_buf);
    result = RK_MPI_SYS_Init();
    if(result != RK_SUCCESS){
        LOGE("failed to initialize mpi_sys.\
             line: %d,file:%s",__LINE__,__FILE__);
    }
    LOGD("RK_MPI_SYS_Init done.");
    
    /*
    *4. set device attributes?? unclear why do we need to call this function if the API specifies
    *   that the attr struct is currently unused and can be passed in uninitialized.
    */
    result = RK_MPI_VI_GetDevAttr(vi_dev_id, &mpi_ctx.video_in.stDevAttr);
    if(result == RK_ERR_VI_NOT_CONFIG){
        LOGD("device not configured setting up.");
        result = RK_MPI_VI_SetDevAttr(vi_dev_id,&mpi_ctx.video_in.stDevAttr);
        if(result != RK_SUCCESS){
            LOGE("failed to Set VI dev attributes. error code:%d",result);
            return false;
        }else{
            result = RK_MPI_VI_GetDevIsEnable(vi_dev_id);
            if(result != RK_SUCCESS){
                //5.enable device
                result = RK_MPI_VI_EnableDev(vi_dev_id);
                if(result != RK_SUCCESS){
                    LOGE("failed to enable vi device. error code:%d",result);
                    return false;
                }else{
                    mpi_ctx.video_in.stBindPipe.u32Num = 1;
                    mpi_ctx.video_in.stBindPipe.bUserStartPipe[0] = RK_FALSE;
                    mpi_ctx.video_in.stBindPipe.PipeId[0] = vi_dev_id;
                    //6.bind device output to pipe. again currently does nothing and use in bypass mode.
                    result = RK_MPI_VI_SetDevBindPipe(vi_dev_id,&mpi_ctx.video_in.stBindPipe);
                    if(result != RK_SUCCESS){
                        LOGE("failed to set device bind pipe. error code:%d",result);
                        return false;
                    }else{
                        LOGD("set device bind pipe done.");
                    }
                }
            }
        }
    }else{
        LOGE("video in device is already configured.");
    }
    //7.configure vi output channel
    mpi_ctx.video_in.stChnAttr.stSize.u32Height = mpi_ctx.video_in.vi_height;
    mpi_ctx.video_in.stChnAttr.stSize.u32Width = mpi_ctx.video_in.vi_width;
    mpi_ctx.video_in.stChnAttr.stIspOpt.u32BufCount = vi_buf_count;
    mpi_ctx.video_in.stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    mpi_ctx.video_in.stChnAttr.u32Depth = 2;
    mpi_ctx.video_in.stChnAttr.enPixelFormat = RK_FMT_YUV422_UYVY;
    mpi_ctx.video_in.stChnAttr.stFrameRate.s32SrcFrameRate = -1;
    mpi_ctx.video_in.stChnAttr.stFrameRate.s32DstFrameRate = -1;

    //8, set vi output channel attributes
    result = RK_MPI_VI_SetChnAttr(
        mpi_ctx.video_in.stBindPipe.PipeId[0],
        vi_dev_id,
        &mpi_ctx.video_in.stChnAttr
    );
    if(result != RK_SUCCESS){
        LOGE("failed to set vi channel attribute. error code: %d",result);
        return false;
    }
    result = RK_MPI_VI_EnableChn(
        mpi_ctx.video_in.stBindPipe.PipeId[0],
        vi_dev_id
    );
    if(result != RK_SUCCESS){
        LOGE("failed to enable video in channel. error code %d", result);
        return false;
    }
    mpi_ctx.video_in.b_vi_channel_en = true;
    return true;
}

/// @brief initialize vpss and bind to vi
///        must be called after init_vi()
/// @return false on fail
bool luckfox_mpi::init_vpss(){
    int32_t result = RK_FAILURE;
    mpi_ctx.vpss.s32ChnId = 0;
    mpi_ctx.vpss.s32GrpId = 0;

    mpi_ctx.vpss.enVProcDevType = VIDEO_PROC_DEV_RGA;
    mpi_ctx.vpss.stVpssChnAttr.enPixelFormat = RK_FMT_YUV420SP;
    mpi_ctx.vpss.stVpssChnAttr.enDynamicRange= DYNAMIC_RANGE_SDR8;
    mpi_ctx.vpss.stVpssChnAttr.u32Height     = mpi_ctx.video_in.vi_height;
    mpi_ctx.vpss.stVpssChnAttr.u32Width      = mpi_ctx.video_in.vi_width;
    mpi_ctx.vpss.stVpssChnAttr.stFrameRate.s32SrcFrameRate   = 
        mpi_ctx.video_in.stChnAttr.stFrameRate.s32DstFrameRate;
    mpi_ctx.vpss.stVpssChnAttr.stFrameRate.s32DstFrameRate   =
        mpi_ctx.video_in.stChnAttr.stFrameRate.s32DstFrameRate;

    mpi_ctx.vpss.stGrpVpssAttr.enPixelFormat = mpi_ctx.video_in.stChnAttr.enPixelFormat;
    mpi_ctx.vpss.stGrpVpssAttr.enCompressMode= mpi_ctx.video_in.stChnAttr.enCompressMode;
    mpi_ctx.vpss.stGrpVpssAttr.u32MaxH = vpss_max_width;
    mpi_ctx.vpss.stGrpVpssAttr.u32MaxW = vpss_max_width;

    result = RK_MPI_VPSS_CreateGrp(mpi_ctx.vpss.s32GrpId,&mpi_ctx.vpss.stGrpVpssAttr);
    if(result != RK_SUCCESS){
        LOGE("failed to create vpss group. error code:%d",result);
        return false;
    }
    LOGD("Created vpss group.");
    result = RK_MPI_VPSS_SetVProcDev(mpi_ctx.vpss.s32GrpId,mpi_ctx.vpss.enVProcDevType);
    if(result != RK_SUCCESS){
        LOGE("failed to set vpss video proc device. error code:%d",result);
        return false;
    }
    LOGD("created video proc device.");

    result = RK_MPI_VPSS_SetChnAttr(mpi_ctx.vpss.s32GrpId,
                                    mpi_ctx.vpss.s32ChnId,
                                    &mpi_ctx.vpss.stVpssChnAttr);
    if(result != RK_SUCCESS){
        LOGE("failed to set vpss channel attribute. error code:%d",result);
        return false;
    }
    LOGD("set vpss channel attribute.");
    result = RK_MPI_VPSS_EnableChn(mpi_ctx.vpss.s32GrpId,
                                   mpi_ctx.vpss.s32ChnId);
    if(result != RK_SUCCESS){
        LOGE("failed to enable vpss channel. error code:%d",result);
        return false;
    }
    result = RK_MPI_VPSS_StartGrp(mpi_ctx.vpss.s32GrpId);
    if(result != RK_SUCCESS){
        LOGE("failed to start vpss group. error code %d",result);
        return false;
    }
    LOGI("init vpss done.");
    mpi_ctx.vpss.b_vpss_en = true;
    return true;
}

RK_S32 luckfox_mpi::init_venc()
{

    return -1;
}

luckfox_mpi::~luckfox_mpi()
{
    int32_t result_dev = RK_MPI_VI_GetDevIsEnable(vi_dev_id);
    if(result_dev == RK_SUCCESS){
        RK_MPI_VI_DisableDev(vi_dev_id);
    }
    if(mpi_ctx.video_in.b_vi_channel_en){
        RK_MPI_VI_DisableChn(vi_dev_id,vi_dev_id);
    }
}


bool luckfox_mpi::bind_vin_vpss(){
    MPP_CHN_S vi_channel = {.enModId = RK_ID_VI,
                            .s32DevId = mpi_ctx.video_in.s32DevId,
                            .s32ChnId = mpi_ctx.video_in.s32ChnId};
    MPP_CHN_S vpss_channel = {.enModId = RK_ID_VPSS,
                            .s32DevId = mpi_ctx.vpss.s32GrpId,
                            .s32ChnId = mpi_ctx.video_in.s32ChnId};

    int32_t result = RK_MPI_SYS_Bind(&vi_channel,&vpss_channel);
    if(result != RK_SUCCESS){
        LOGE("failed to bind vi to vpss. error code:%d",result);
        return false;
    }else{
        LOGI("vi is binded to vpss.");
        return true;
    }
}

bool luckfox_mpi::bind_vpss_venc()
{
    MPP_CHN_S vpss_channel = {.enModId = RK_ID_VPSS,
                            .s32DevId = mpi_ctx.video_in.s32DevId,
                            .s32ChnId = mpi_ctx.video_in.s32ChnId};
    MPP_CHN_S venc_channel = {.enModId = RK_ID_VENC,
                            .s32DevId = mpi_ctx.vpss.s32GrpId,
                            .s32ChnId = mpi_ctx.video_in.s32ChnId};

    int32_t result = RK_MPI_SYS_Bind(&vpss_channel,&venc_channel);
    if(result != RK_SUCCESS){
        LOGE("failed to bind vi to vpss. error code:%d",result);
        return false;
    }else{
        LOGI("vi is binded to vpss.");
        return true;
    }
}

void luckfox_mpi::xcam_fourcc_str(char* buffer,uint32_t fourcc){

    buffer[0] = (fourcc) & 0xff;
    buffer[1] = (fourcc >> 8) & 0xff;
    buffer[2] = (fourcc >> 16) & 0xff;
    buffer[3] = (fourcc >> 24) & 0xff;
    buffer[4] = '\0';
}