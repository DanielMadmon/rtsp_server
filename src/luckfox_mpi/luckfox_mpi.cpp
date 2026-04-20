#include "luckfox_mpi.hpp"
#include <iostream>
#include "generic_log.h"
#include "im2d.hpp"
#include <string>
#include <fstream>
#include <memory>
#include <thread>
#include "RgaUtils.h"
#include "rk_mpi_mmz.h"
#include "osd.hpp"

#define RK_ALIGN(x, a) (((x) + (a)-1) & ~((a)-1))
#define RK_ALIGN_2(x) RK_ALIGN(x, 2)

using namespace lf_mpi;

luckfox_mpi::luckfox_mpi(std::string rknn_path)
{
    mpi_ctx.rknn_path = rknn_path;
    mpi_ctx.osd_enable = false;
    mpi_ctx.video_encoder.b_venc_en = false;
    mpi_ctx.video_in.b_vi_channel_en = false;
}

bool luckfox_mpi::init_video_in(rk_aiq_working_mode_t mode, int32_t fps,uint32_t width,uint32_t height)
{
    memset(&mpi_ctx.video_in,0,sizeof(mpi_ctx.video_in));
    mpi_ctx.video_in.vi_width = width;
    mpi_ctx.video_in.vi_height = height;
    mpi_ctx.video_in.hdr_mode = mode;
    mpi_ctx.video_in.vi_fps   = fps;
    bool result = init_vi();
    if(result){
        LOGI("init_video_in done.");
        enabled_flags.vi_enabled = true;
    }else{
        LOGE("init video failed");
    }
    return result;
}
/// @brief initialize video encoder
/// @param codec currently unused fixed to RK_VIDEO_ID_HEVC
/// @param width input image width
/// @param height input image height
/// @return false on fail
bool luckfox_mpi::init_video_encoder(RK_CODEC_ID_E codec,uint32_t width,uint32_t height)
{   
    memset(&mpi_ctx.video_encoder,0,sizeof(mpi_ctx.video_encoder));
    //TODO:enable setting codec(H264 codec)
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.enType = codec;
    mpi_ctx.video_encoder.s32ChnId = channel_id;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.u32BitRate = 8192;
    mpi_ctx.video_encoder.stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32Profile = H265E_PROFILE_MAIN; //main 10 profile
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32PicWidth = width;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32PicHeight= height;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32MaxPicWidth = width;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32MaxPicHeight= height;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.u32Gop = 10; //evevry 50 frames
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateDen= 1;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = 
        mpi_ctx.vpss.stGrpVpssAttr.stFrameRate.s32DstFrameRate;
    mpi_ctx.video_encoder.stChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = 
         mpi_ctx.vpss.stGrpVpssAttr.stFrameRate.s32DstFrameRate;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32VirWidth = width;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32VirHeight = height;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32StreamBufCnt = 4;
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32BufSize = width * height / 2;
    int32_t result = RK_MPI_VENC_CreateChn(mpi_ctx.video_encoder.s32ChnId,
                        &mpi_ctx.video_encoder.stChnAttr);
    if(result != RK_SUCCESS){
        LOGE("failed to create video encoder channel. error code:%d",result);
        return false;
    }
    enabled_flags.venc_enabled = true;
    VENC_RC_PARAM_S pstRcParam;
    memset(&pstRcParam,0,sizeof(pstRcParam));
    pstRcParam.s32FirstFrameStartQp = 26;
    pstRcParam.stParamH265.u32StepQp = 8;
    pstRcParam.stParamH265.u32MaxQp  = 35;
    pstRcParam.stParamH265.u32MinQp  = 20;
    pstRcParam.stParamH265.u32FrmMaxIQp = 30;
    pstRcParam.stParamH265.u32MinIQp = 24;
    pstRcParam.stParamH265.s32DeltIpQp = 2;
    pstRcParam.stParamH265.s32MaxReEncodeTimes = 1;
    
    result = RK_MPI_VENC_SetRcParam(mpi_ctx.video_encoder.s32ChnId,&pstRcParam);
    if(result != RK_SUCCESS){
        LOGE("failed to set video encoder rc params. error code:%d",result);
        return false;
    }
    VENC_RC_PARAM_S test_rc = {0};
    result = RK_MPI_VENC_GetRcParam(mpi_ctx.video_encoder.s32ChnId,&test_rc);
    if(result != RK_SUCCESS){
        LOGE("failed to set video encoder rc params. error code:%d",result);
        return false;
    }
    VENC_RC_PARAM2_S pstRcParam2 = {
        .u32ThrdI = {0,0,0,0,3,3,5,5,8,8,8,15,15,20,25,25},
        .u32ThrdP = {0,0,0,0,3,3,5,5,8,8,8,15,15,20,25,25},
        .s32AqStepI = {-8,-7,-6,-5,-4,-3,-2,-1,0,1,2,3,4,5,7,8},
        .s32AqStepP = {-8,-7,-6,-5,-4,-3,-2,-1,0,1,2,3,4,5,7,8}
    };
    
    result = RK_MPI_VENC_SetRcParam2(mpi_ctx.video_encoder.s32ChnId,&pstRcParam2);
    if(result != RK_SUCCESS){
        LOGE("failed to set video encoder rc params2. error code:%d",result);
        return false;
    }
    VENC_H265_QBIAS_S pstQbias = {
        .bEnable = RK_TRUE,
        .u32QbiasI = 171,
        .u32QbiasP = 85
    };
    result = RK_MPI_VENC_SetH265Qbias(mpi_ctx.video_encoder.s32ChnId,&pstQbias);
    if(result != RK_SUCCESS){
        LOGE("failed to set H265Qbias. error code:%d",result);
        return false;
    }
    VENC_H265_CU_DQP_S pstCuDqp = {
        .u32CuDqp = 1
    };
    result = RK_MPI_VENC_SetH265CuDqp(mpi_ctx.video_encoder.s32ChnId,&pstCuDqp);
    if(result != RK_SUCCESS){
        LOGE("failed to set H265CuDqp. error code:%d",result);
        return false;
    }
    VENC_ANTI_RING_S pAntiRing = {
        .u32AntiRing = 2
    };
    result = RK_MPI_VENC_SetAntiRing(mpi_ctx.video_encoder.s32ChnId,&pAntiRing);

    VENC_ANTI_LINE_S pAntiLine = {
        .u32AntiLine = 2
    };

    result |= RK_MPI_VENC_SetAntiLine(mpi_ctx.video_encoder.s32ChnId,&pAntiLine);

    VENC_LAMBDA_S pLambda = {
        .u32Lambda = 4
    };
    result |= RK_MPI_VENC_SetLambda(mpi_ctx.video_encoder.s32ChnId,&pLambda);


    result |= RK_MPI_VENC_SetMotionDeblurStrength(mpi_ctx.video_encoder.s32ChnId,3);

    if(result != RK_SUCCESS){
        LOGE("failed to set Lambda or AntiRing or AntiLine or MotionDeblurStrength");
        return false;
    }
    return true;
}
static XCamReturn aiq_error_cb(rk_aiq_err_msg_t* err_msg){
    LOGE("got error:%d",err_msg->err_code);
    return XCamReturn::XCAM_RETURN_ERROR_FAILED;
}

bool luckfox_mpi::init_vi()
{
    //1. init device
    int32_t result = RK_FAILURE;
    char hdr_env_var[16] = {0};
    char env_var[] = "HDR_MODE";
    snprintf(hdr_env_var,sizeof(hdr_env_var),"%u",mpi_ctx.video_in.hdr_mode);
    result = setenv(env_var,hdr_env_var,1);
    LOGD("enumarating static metas by phy ID. result setenv hdr_mode:%d",result);
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
                                mpi_ctx.rknn_path.c_str(),
                                aiq_error_cb,NULL);
    if(!mpi_ctx.video_in.aiq_ctx){
        LOGE("failed to initialize sysctl.\
             line: %d,file:%s",__LINE__,__FILE__);
        return false;
    }
    rk_aiq_uapi2_sysctl_prepare(mpi_ctx.video_in.aiq_ctx,
                                mpi_ctx.video_in.vi_width,
                                mpi_ctx.video_in.vi_height,
                                mpi_ctx.video_in.hdr_mode);
    rk_aiq_uapi2_sysctl_start(mpi_ctx.video_in.aiq_ctx);
    LOGD("init sysctl done.");
    LOGD("sensor name:%s",mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name);
    LOGD("width:%d",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->width);
    LOGD("height:%d",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->height);
    LOGD("fps:%d",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->fps);
    LOGD("multi_isp_extended_pixel:%d",mpi_ctx.video_in.aiq_static_info.multi_isp_extended_pixel);
    
    acp_attrib_t attrib;
	result = rk_aiq_user_api2_acp_GetAttrib(mpi_ctx.video_in.aiq_ctx, &attrib);
    LOGI("got brightness %x",attrib.brightness);
    LOGI("got brightness %x",attrib.contrast);
    LOGI("got hue %x",attrib.hue);
    LOGI("got saturation %x",attrib.saturation);
	attrib.contrast = 50 * 2.55; // value[0,255]
    attrib.brightness = 50 * 2.55;
    attrib.hue = 50 * 2.55;
    attrib.saturation = 50 * 2.55;
	result |= rk_aiq_user_api2_acp_SetAttrib(mpi_ctx.video_in.aiq_ctx, &attrib);

    Uapi_ExpSwAttrV2_t expSwAttr;
    expSwAttr.sync.done = false;
    expSwAttr.AecOpType = RK_AIQ_OP_MODE_AUTO;
    //LinearAE
    expSwAttr.stManual.LinearAE.ManualGainEn = false;
    expSwAttr.stManual.LinearAE.ManualTimeEn = false;
    expSwAttr.stManual.LinearAE.GainValue = 1.0f; /*gain = 1x*/
    expSwAttr.stManual.LinearAE.TimeValue = 0.02f; /*time = 1/50s*/
    expSwAttr.Enable = true;
    expSwAttr.stAuto.LinAeRange.stExpTimeRange.Min = 1.0f / 500.0f;
    expSwAttr.stAuto.LinAeRange.stExpTimeRange.Max = 1.0f / 50.0f;
    expSwAttr.stAuto.LinAeRange.stGainRange.Min = 1.0f;
    expSwAttr.stAuto.LinAeRange.stGainRange.Max = 4.0f;
    expSwAttr.sync.sync_mode = RK_AIQ_UAPI_MODE_SYNC;
    //HdrAE (should set all frames)
    expSwAttr.stManual.HdrAE.ManualGainEn = false;
    expSwAttr.stManual.HdrAE.ManualTimeEn = false;
    expSwAttr.stManual.HdrAE.GainValue[0] = 1.0f; /*sframe gain = 1x*/
    expSwAttr.stManual.HdrAE.TimeValue[0] = 0.002f; /*sframe time = 1/500s*/
    expSwAttr.stManual.HdrAE.GainValue[1] = 2.0f; /*mframe gain = 2x*/
    expSwAttr.stManual.HdrAE.TimeValue[1] = 0.01f; /*mframe time = 1/100s*/
    expSwAttr.stManual.HdrAE.GainValue[2] = 4.0f; /*lframe gain = 4x*/
    expSwAttr.stManual.HdrAE.TimeValue[2] = 0.02f; /*lframe time = 1/50s*/

    result |= rk_aiq_user_api2_ae_setExpSwAttr(mpi_ctx.video_in.aiq_ctx,expSwAttr);
    
    memset(&expSwAttr,0,sizeof(expSwAttr));
    result |= rk_aiq_user_api2_ae_getExpSwAttr(mpi_ctx.video_in.aiq_ctx,&expSwAttr);
    LOGI("got AecOpType:%d",expSwAttr.AecOpType);
    LOGI("got HdrAE.GainValue[0]:%f", expSwAttr.stManual.HdrAE.GainValue[0]);
    LOGI("got HdrAE.TimeValue[0]:%f",expSwAttr.stManual.HdrAE.TimeValue[0]);
    LOGI("got HdrAE.GainValue[2]:%f",expSwAttr.stManual.HdrAE.GainValue[2]);
    LOGI("got HdrAE.TimeValue[2]%f",expSwAttr.stManual.HdrAE.TimeValue[2]);
    
   
    float fPercent = 0.0f;
	fPercent = 50 / 100.0f;
	rk_aiq_sharp_strength_v33_t sharpV33Strength;
	sharpV33Strength.sync.sync_mode = RK_AIQ_UAPI_MODE_SYNC;
	sharpV33Strength.percent = fPercent;
	sharpV33Strength.strength_enable = true;
    result |= rk_aiq_user_api2_asharpV33_SetStrength(mpi_ctx.video_in.aiq_ctx,&sharpV33Strength);
    
    rk_aiq_uapiV2_wb_opMode_t awbAttr{.mode = RK_AIQ_WB_MODE_AUTO};
    result |= rk_aiq_user_api2_awb_SetWpModeAttrib(mpi_ctx.video_in.aiq_ctx,awbAttr);

    //TODO: spatial and temporal noise reduction + dehazing
    if(result != RK_SUCCESS){
        LOGE("failed to set isp attributes. error code %d", result);
        return false;
    }
    
    
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
    mpi_ctx.video_in.stChnAttr.stIspOpt.u32BufSize = mpi_ctx.video_in.vi_height * mpi_ctx.video_in.vi_width * 3 / 2;
    mpi_ctx.video_in.stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    mpi_ctx.video_in.stChnAttr.u32Depth = 3;
    mpi_ctx.video_in.stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
    mpi_ctx.video_in.stChnAttr.stFrameRate.s32SrcFrameRate = mpi_ctx.video_in.vi_fps;
    mpi_ctx.video_in.stChnAttr.stFrameRate.s32DstFrameRate = mpi_ctx.video_in.vi_fps;
    mpi_ctx.video_in.stChnAttr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
    mpi_ctx.video_in.stChnAttr.stIspOpt.stMaxSize.u32Height = mpi_ctx.video_in.vi_height;
    mpi_ctx.video_in.stChnAttr.stIspOpt.stMaxSize.u32Width  = mpi_ctx.video_in.vi_width;
    //8, set vi output channel attributes
    result = RK_MPI_VI_SetChnAttr(
        mpi_ctx.video_in.stBindPipe.PipeId[0],
        mpi_ctx.video_in.s32ChnId,
        &mpi_ctx.video_in.stChnAttr
    );
    if(result != RK_SUCCESS){
        LOGE("failed to set vi channel attribute. error code: %d",result);
        return false;
    }
    result = RK_MPI_VI_EnableChn(
        mpi_ctx.video_in.stBindPipe.PipeId[0],
        mpi_ctx.video_in.s32ChnId
    );
    if(result != RK_SUCCESS){
        LOGE("failed to enable video in channel. error code %d", result);
        return false;
    }
    enabled_flags.vi_enabled = true;
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

luckfox_mpi::~luckfox_mpi()
{
    int32_t rk_res = 0;
    LOGD("deleting luckfox mpi handle");
    //TODO:proper cleanup
    if(enabled_flags.stream_locked.load() == true){
        rk_res = RK_MPI_VENC_ReleaseStream(mpi_ctx.video_encoder.s32ChnId,&pstStream);
        LOGD("releasing stream in cleanup. res:%d",rk_res);
    }
    if(enabled_flags.vi_bind_venc && !mpi_ctx.osd_enable){
        MPP_CHN_S vin = {.enModId = RK_ID_VI,
                            .s32DevId = mpi_ctx.video_in.s32DevId,
                            .s32ChnId = mpi_ctx.video_in.s32ChnId};
        MPP_CHN_S venc_channel = {.enModId = RK_ID_VENC,
                            .s32DevId = mpi_ctx.video_in.s32DevId,
                            .s32ChnId = mpi_ctx.video_encoder.s32ChnId};

        rk_res = RK_MPI_SYS_UnBind(&vin,&venc_channel);
        LOGD("calling RK_MPI_SYS_UnBind vin->venc. res:%d",rk_res);
    }
    if(enabled_flags.venc_start_rcv && rk_res == RK_SUCCESS){
        rk_res = RK_MPI_VENC_StopRecvFrame(mpi_ctx.video_encoder.s32ChnId);
        LOGD("calling RK_MPI_VENC_StopRecvFrame. res:%d",rk_res);
    }
    if(enabled_flags.venc_enabled && rk_res == RK_SUCCESS){
        rk_res |= RK_MPI_VENC_DestroyChn(mpi_ctx.video_encoder.s32ChnId);
        LOGD("calling RK_MPI_VENC_DestroyChn(venc).res:%d",rk_res);
    }
    
    if(enabled_flags.vi_enabled && rk_res == RK_SUCCESS){
        rk_res = RK_MPI_VI_DisableChn(mpi_ctx.video_in.u32PipeId,mpi_ctx.video_in.s32ChnId);
        LOGD("calling RK_MPI_VI_DisableChn. res:%d",rk_res);
        rk_res = RK_MPI_VI_DisableDev(mpi_ctx.video_in.s32DevId);
        LOGD("calling RK_MPI_VI_DisableDev. res%d",rk_res);
    }
    if(rk_res == RK_SUCCESS){
        rk_res = rk_aiq_uapi2_sysctl_stop(mpi_ctx.video_in.aiq_ctx,true);
        rk_aiq_uapi2_sysctl_deinit(mpi_ctx.video_in.aiq_ctx);
        LOGD("calling  rk_aiq_uapi2_sysctl_stop. res:%d",rk_res);
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

bool luckfox_mpi::bind_vin_venc()
{
    MPP_CHN_S vin = {.enModId = RK_ID_VI,
                            .s32DevId = mpi_ctx.video_in.s32DevId,
                            .s32ChnId = mpi_ctx.video_in.s32ChnId};
    MPP_CHN_S venc_channel = {.enModId = RK_ID_VENC,
                            .s32DevId = mpi_ctx.video_in.s32DevId,
                            .s32ChnId = mpi_ctx.video_encoder.s32ChnId};

    int32_t result = RK_MPI_SYS_Bind(&vin,&venc_channel);
    if(result != RK_SUCCESS){
        LOGE("failed to bind vin to venc. error code:%d",result);
        return false;
    }else{
        LOGI("vin is binded to venc.");
        enabled_flags.vi_bind_venc = true;
        return true;
    }
}



bool luckfox_mpi::bind_vpss_venc()
{
    MPP_CHN_S vpss_channel = {.enModId = RK_ID_VPSS,
                            .s32DevId = mpi_ctx.vpss.s32ChnId,
                            .s32ChnId = mpi_ctx.vpss.s32ChnId};
    MPP_CHN_S venc_channel = {.enModId = RK_ID_VENC,
                            .s32DevId = mpi_ctx.video_encoder.s32ChnId,
                            .s32ChnId = mpi_ctx.video_encoder.s32ChnId};

    int32_t result = RK_MPI_SYS_Bind(&vpss_channel,&venc_channel);
    if(result != RK_SUCCESS){
        LOGE("failed to bind venc to vpss. error code:%d",result);
        return false;
    }else{
        LOGI("venc is binded to vpss.");
        return true;
    }
}


/// @brief get bytestream from video encoder
/// @param restart [in] true on restarting the encoder(best way to ensure IDR)
/// @param stream_len [out] the returned stream length in bytes
/// @param timestamp [out] 64 bits timestamp for the stream from venc
/// @return NULL on fail
uint8_t* luckfox_mpi::venc_get_stream(bool restart,size_t *stream_len,uint64_t* timestamp)
{
    int32_t rk_result = 0;
    uint8_t * stream_ptr = NULL;
    if(!stream_len || !timestamp){
        LOGE("Null pointer passed to luckfox_mpi::venc_get_stream!");
        return NULL;
    }
    if(!enabled_flags.vi_bind_venc 
        || !enabled_flags.vi_bind_venc 
        || !enabled_flags.venc_enabled 
        || !enabled_flags.vi_enabled){
        LOGE("call to luckfox_mpi::venc_get_stream before setting upstream!");
        return NULL;
    }
    if(enabled_flags.stream_locked.load() == true){
        LOGE("tried to get stream before releasing stream!");
        return NULL;
    }
    if(restart){
        if(!venc_restart()){
            LOGW("failed to restart venc");
        }
    }
    
    memset(&pstStream,0,sizeof(pstStream));
    memset(&pstPack,0,sizeof(pstPack));
    pstStream.pstPack = &pstPack;
    rk_result = RK_MPI_VENC_GetStream(mpi_ctx.video_encoder.s32ChnId,&pstStream,120);
    if(rk_result == RK_SUCCESS){
        enabled_flags.stream_locked.store(true);
        stream_ptr = (uint8_t*)(RK_MPI_MB_Handle2VirAddr(pstStream.pstPack->pMbBlk));
        if(stream_ptr){
            *stream_len = pstStream.pstPack->u32Len;
            *timestamp = pstStream.pstPack->u64PTS;
            return stream_ptr;
        }else{
            return nullptr;
        }
    }else{
        LOGE("failed to get video encoder stream. error code:%d",rk_result);
        return nullptr;
    }
}

bool luckfox_mpi::venc_release_stream(){
    int32_t rk_result = 0;
    if(enabled_flags.stream_locked.load() == true){
        rk_result = RK_MPI_VENC_ReleaseStream(mpi_ctx.video_encoder.s32ChnId,&pstStream);
        enabled_flags.stream_locked.store(false);
        return rk_result == RK_SUCCESS;
    }else{
        LOGE("called venc_release_stream before calling venc_get_stream");
        return false;
    }
    return false;
}

MB_BLK luckfox_mpi::vi_get_frame(VIDEO_FRAME_INFO_S* out_frame_info)
{
    int32_t res = RK_MPI_VI_GetChnFrame(0,mpi_ctx.video_in.s32ChnId,&mpi_ctx.video_in.frame_info,1000);
    *out_frame_info = mpi_ctx.video_in.frame_info;
    if(res == RK_SUCCESS){
        return mpi_ctx.video_in.frame_info.stVFrame.pMbBlk;
    }
    return nullptr;
}

/// @brief send frame to venc
/// @param frame_info 
/// @return 
bool lf_mpi::luckfox_mpi::venc_send_frame(VIDEO_FRAME_INFO_S &frame_info)
{
    
    int32_t res = RK_MPI_VENC_SendFrame(mpi_ctx.video_encoder.s32ChnId,&frame_info,1000);
    return res == RK_SUCCESS;
}

void luckfox_mpi::vi_release_frame()
{
    if(mpi_ctx.video_in.frame_info.stVFrame.pMbBlk){
        RK_MPI_VI_ReleaseChnFrame(0,mpi_ctx.video_in.s32ChnId,&mpi_ctx.video_in.frame_info);
        mpi_ctx.video_in.frame_info.stVFrame.pMbBlk = nullptr;
    }
}

bool luckfox_mpi::venc_restart()
{
    int32_t rk_result = 0;
    if(enabled_flags.vi_bind_venc && !mpi_ctx.osd_enable){
        MPP_CHN_S vin = {.enModId = RK_ID_VI,
                            .s32DevId = mpi_ctx.video_in.s32DevId,
                            .s32ChnId = mpi_ctx.video_in.s32ChnId};
        MPP_CHN_S venc_channel = {.enModId = RK_ID_VENC,
                            .s32DevId = mpi_ctx.video_in.s32DevId,
                            .s32ChnId = mpi_ctx.video_encoder.s32ChnId};

        
        rk_result = RK_MPI_SYS_UnBind(&vin,&venc_channel);
        LOGD("calling RK_MPI_SYS_UnBind vin->venc. res:%d",rk_result);
    }
    if(enabled_flags.venc_start_rcv && rk_result == RK_SUCCESS){
        rk_result = RK_MPI_VENC_StopRecvFrame(mpi_ctx.video_encoder.s32ChnId);
        LOGD("calling RK_MPI_VENC_StopRecvFrame. res:%d",rk_result);
    }
    if(enabled_flags.venc_enabled && rk_result == RK_SUCCESS){
        rk_result |= RK_MPI_VENC_DestroyChn(mpi_ctx.video_encoder.s32ChnId);
        LOGD("calling RK_MPI_VENC_DestroyChn(venc).res:%d",rk_result);
    }
    init_video_encoder(mpi_ctx.video_encoder.stChnAttr.stVencAttr.enType,
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32PicWidth,
    mpi_ctx.video_encoder.stChnAttr.stVencAttr.u32PicHeight);
    return start_video_encoder(mpi_ctx.osd_enable);   
}


/// @brief must be called after init_video_encoder and before venc_get_stream
/// @return false on fail
bool luckfox_mpi::start_video_encoder(bool osd_enable){
    VENC_RECV_PIC_PARAM_S stRecvParam;
    memset(&stRecvParam,0,sizeof(stRecvParam));
    stRecvParam.s32RecvPicNum = -1;
    LOGD("calling start recv frame");
    fflush(stdout);
	int32_t result = RK_MPI_VENC_StartRecvFrame(0, &stRecvParam);
	if (result != RK_SUCCESS) {
		LOGE("create ch venc failed. error code:%d", result);
		return false;
	}
    enabled_flags.venc_start_rcv = true;
    bool res = true;
    if(!osd_enable){
        res = bind_vin_venc();
    }
    mpi_ctx.osd_enable = osd_enable;
    ///TODO:better flag name
    enabled_flags.vi_bind_venc = res;
    return res;
}

bool luckfox_mpi::osd_init()
{
    const float cvt_bpp = get_bpp_from_format(osd_handles.rgba_format);
    const float venc_bpp = get_bpp_from_format(osd_handles.yuv_format);
    const size_t cvt_image_size = mpi_ctx.video_in.vi_width * mpi_ctx.video_in.vi_height * cvt_bpp;
    const size_t venc_image_size = mpi_ctx.video_in.vi_width * mpi_ctx.video_in.vi_height * venc_bpp;
    const size_t osd_image_max_size = osd_handles.MAX_OSD_WIDTH * osd_handles.MAX_OSD_HEIGHT * cvt_bpp;
    bool ret = false;
    osd_handles.cvt_image_blk = mmz_alloc(cvt_image_size);
    if(!osd_handles.cvt_image_blk){
        LOGE("failed to allocate mmz buffer for converted image (rgba8888)");
        goto exit_err;
    }
    osd_handles.cvt_image_phy_address = RK_MPI_MMZ_Handle2PhysAddr(osd_handles.cvt_image_blk);
    if(!osd_handles.cvt_image_phy_address){
        LOGE("failed to get phy addres from cvt image block");
        goto exit_err;
    }
    osd_handles.vi_rgba_buf_handle = importbuffer_physicaladdr(osd_handles.cvt_image_phy_address,
                                                               cvt_image_size);
    if(!osd_handles.vi_rgba_buf_handle){
        LOGE("failed to import physical addr buffer vi to rga");
        goto exit_err;
    }
    osd_handles.vi_rga_buf = wrapbuffer_handle(osd_handles.vi_rgba_buf_handle,
                                                mpi_ctx.video_in.vi_width,
                                                mpi_ctx.video_in.vi_height,
                                                RgaSURF_FORMAT::RK_FORMAT_RGBA_8888);
    
    
    osd_handles.venc_image_blk = mmz_alloc(venc_image_size);
    if(!osd_handles.venc_image_blk){
        LOGE("failed to allocate mmz block for venc");
        goto exit_err;
    }
    osd_handles.venc_image_phy_address = RK_MPI_MMZ_Handle2PhysAddr(osd_handles.venc_image_blk);
    if(!osd_handles.venc_image_phy_address){
        LOGE("failed to get phy address from Venc mb blk");
        goto exit_err;
    }
    osd_handles.venc_yuv_buf_handle = importbuffer_physicaladdr(osd_handles.venc_image_phy_address,
                                                                venc_image_size);
    if(!osd_handles.venc_yuv_buf_handle){
        LOGE("failed to import rga venc buf with phy address");
        goto exit_err;
    }
    osd_handles.venc_rga_buf = wrapbuffer_handle(osd_handles.venc_yuv_buf_handle,
                                                mpi_ctx.video_in.vi_width,
                                                mpi_ctx.video_in.vi_height,
                                                osd_handles.yuv_format);

    osd_handles.osd_image_blk = mmz_alloc(osd_image_max_size);
    if(!osd_handles.osd_image_blk){
        LOGE("failed to allocate osd_image_blk");
        goto exit_err;
    }
    ret = true;
    LOGD("rga init done");
    goto exit_ok;
    exit_err:
        if(osd_handles.vi_rgba_buf_handle)
            releasebuffer_handle(osd_handles.vi_rgba_buf_handle);
        if(osd_handles.venc_yuv_buf_handle)
            releasebuffer_handle(osd_handles.venc_yuv_buf_handle);
        if(osd_handles.venc_image_blk)
            mmz_free(osd_handles.venc_image_blk);
        if(osd_handles.cvt_image_blk)
            mmz_free(osd_handles.cvt_image_blk);
        osd_handles.init_done = ret;
        return ret;
    exit_ok:
        osd_handles.init_done = ret;
        return ret;
}

/// @brief run osd thread.
/// fetches buffer from vi -> converts yuv2rgb->osd impose->rgb2yuv->send to video encoder
/// @param osd_rgba_pixels current osd rgba bitmap 
/// @param size bmp size info struct
/// @param update_osd_f set when updating osd cleared by osd_run thread.
/// causes reimporting of buffer to rga with updated size and data 
/// @param stop_f set when stopping the osd thread
/// @return none
void luckfox_mpi::osd_thread(std::vector<uint8_t> &osd_rgba_pixels, 
                            osd::bmp_resolution &size, 
                            osd::flag &update_osd_f, 
                            osd::flag &stop_f)
{
    if(!osd_handles.init_done){
        if(!osd_init()){
            LOGE("failed to initialize osd. osd thread is closed");
            return;
        }
    }

    rga_buffer_handle_t osd_img_buf_handle = 0;
    rga_buffer_handle_t vi_image_buf_handle = 0;
    rga_buffer_t osd_img_buf = {0};
    rga_buffer_t vi_img_buf = {0};
    VIDEO_FRAME_INFO_S venc_frame_info = {0};
    VIDEO_FRAME_INFO_S vin_frame_info = {0};
    void* osd_image_blk_vir_p = nullptr;
    uint64_t osd_image_blk_phy_p = 0;
    uint64_t vi_image_blk_phy_p = 0;
    size_t osd_buf_size = 0;
    int32_t rk_res = -1;
    IM_STATUS rga_res = IM_STATUS::IM_STATUS_FAILED;
    im_rect osd_rect = {0};
    im_osd_t osd_cfg = {0};
    const size_t vi_image_buf_size = mpi_ctx.video_in.vi_width * mpi_ctx.video_in.vi_height * 3 / 2;

    while(stop_f.is_clear()){
        if(update_osd_f.is_set()||!osd_img_buf_handle){
            if(osd_img_buf_handle){
                releasebuffer_handle(osd_img_buf_handle);
            }
            //copy osd_rgba_pixels buffer into internal buffer
            osd_buf_size = size.width * size.height * 4;
            if(osd_rgba_pixels.size() != osd_buf_size){
                LOGE("osd_rgba_pixels.size() != osd_buf_size");
                LOGE("osd_rgba_pixels.size() = %d",osd_rgba_pixels.size());
                LOGE("osd buf size = %d",osd_buf_size);
                break;
            }
            osd_image_blk_vir_p = RK_MPI_MMZ_Handle2VirAddr(osd_handles.osd_image_blk);
            if(!osd_image_blk_vir_p){
                LOGE("failed to convert osd_image_blk to virtual address");
                break;            
            }
            memcpy(osd_image_blk_vir_p,&osd_rgba_pixels[0],osd_buf_size);
            osd_cfg = get_osd_config(size.width);
            osd_rect = get_osd_rect(size.width,size.height);
            update_osd_f.clear_update();
            //import osd internal buffer MP_BLK to rga
            osd_image_blk_phy_p = RK_MPI_MMZ_Handle2PhysAddr(osd_handles.osd_image_blk);
            if(!osd_image_blk_phy_p){
                LOGE("failed to convert osd_image_blk to phy address");
                break;            
            }
            osd_img_buf_handle = importbuffer_physicaladdr(osd_image_blk_phy_p,osd_buf_size);
            if(!osd_img_buf_handle){
                LOGE("failed to import osd phy address to rga");
                break;
            }
            osd_img_buf = wrapbuffer_handle(osd_img_buf_handle,
                                            osd_rect.width,
                                            osd_rect.height,
                                            osd_handles.rgba_format);
        }
        rk_res = RK_MPI_VI_GetChnFrame(0,mpi_ctx.video_in.s32ChnId,&vin_frame_info,1000);
        if(rk_res != RK_SUCCESS){
            LOGE("failed to get vi chan frame");
            break;
        }
        vi_image_blk_phy_p = RK_MPI_MMZ_Handle2PhysAddr(vin_frame_info.stVFrame.pMbBlk);
        if(!vi_image_blk_phy_p){
            LOGE("failed to get phy address from vi MB_BLK");
            break;
        }
        vi_image_buf_handle = importbuffer_physicaladdr(vi_image_blk_phy_p,vi_image_buf_size);
        if(!vi_image_buf_handle){
            LOGE("failed to import vi image buf handle");
            break;
        }
        vi_img_buf = wrapbuffer_handle(vi_image_buf_handle,
                                        mpi_ctx.video_in.vi_width,
                                        mpi_ctx.video_in.vi_height,
                                        osd_handles.yuv_format);
        rga_res = imcvtcolor(vi_img_buf,osd_handles.vi_rga_buf,
                            osd_handles.yuv_format,
                            osd_handles.rgba_format,
                            IM_YUV_TO_RGB_BT601_LIMIT,
                            1,nullptr);
        
        releasebuffer_handle(vi_image_buf_handle);
        RK_MPI_VI_ReleaseChnFrame(0,mpi_ctx.video_in.s32ChnId,&vin_frame_info);
        if(rga_res != IM_STATUS::IM_STATUS_SUCCESS){
            LOGE("imcvtcolor yuv2rgb failed. error:%s",imStrError(rga_res));
            break;
        }
        rga_res = imosd(osd_img_buf,osd_handles.vi_rga_buf,
                        osd_rect,&osd_cfg,1,nullptr);
        if(rga_res != IM_STATUS::IM_STATUS_SUCCESS){
            LOGE("imosd failed error:%s",imStrError(rga_res));
            break;
        }
        rga_res = imcvtcolor(osd_handles.vi_rga_buf,osd_handles.venc_rga_buf,
                            osd_handles.rgba_format,osd_handles.yuv_format,
                            IM_RGB_TO_YUV_BT601_LIMIT,1,nullptr);
        if(rga_res != IM_STATUS::IM_STATUS_SUCCESS){
            LOGE("imcvtcolor rgba->yuv420 failed. error:%s",imStrError(rga_res));
            break;
        }
        memcpy(&venc_frame_info,&vin_frame_info,sizeof(vin_frame_info));
        venc_frame_info.stVFrame.pMbBlk = osd_handles.venc_image_blk;
        rk_res = RK_MPI_VENC_SendFrame(mpi_ctx.video_encoder.s32ChnId,
                                        &venc_frame_info,1000);
        if(rk_res != RK_SUCCESS){
            LOGE("failed to send frame to venc");
            break;
        }
    }
    
    if(osd_img_buf_handle){
        releasebuffer_handle(osd_img_buf_handle);
    }
    if(vi_image_buf_handle){
        releasebuffer_handle(vi_image_buf_handle);
    }
    LOGD("vi_thread exiting");
}



void luckfox_mpi::osd_bmp_update_thread(osd::text_osd &osd_handle,
                                        AtcTimeZone& tz,
                                        std::vector<uint8_t>&pixel_buffer, 
                                        osd::bmp_resolution &size, 
                                        osd::flag &update_osd_f, 
                                        osd::flag &stop_f
                                        )
{
    time_t raw_time = 0;
    ///TODO:sleep until next minute boundry
    while(stop_f.is_clear()){
        std::string time_str = osd::get_local_time(tz,raw_time);
        if(raw_time % 60 == 0 || raw_time == 0){
            osd_handle.render_text_rgba(time_str,pixel_buffer,size);
            update_osd_f.set_update();
            update_osd_f.wait_clear();
        }
    }
}
void luckfox_mpi::osd_run(std::vector<uint8_t>& osd_rgba_pixels,
                          osd::bmp_resolution& size,
                          osd::flag& update_osd_f,
                          osd::flag& stop_f)
{
    /**
     * TODO:add extra vi channel?
     * **/
    IM_STATUS res;

    //get vi frame
    VIDEO_FRAME_INFO_S frame_info;
    rga_buffer_handle_t cvt_image_buf_handle = 0;
    rga_buffer_handle_t vi_img_buff_handle = 0; 
    rga_buffer_handle_t osd_img_buf_handle = 0;
    rga_buffer_handle_t venc_img_buf_handle = 0;
    
    rga_buffer_t vi_img_buf = {0};
    rga_buffer_t cvt_image_buf = {0};
    rga_buffer_t osd_img_buf = {0};
    rga_buffer_t vecn_img_buf = {0};

    int vi_buff_fd = 0;
    size_t vi_buf_size = 0;
    uint64_t cvt_image_phy_addr = 0;
    uint64_t venc_image_phy_addr = 0;
    size_t cvt_image_size = 0;
    MB_BLK cvt_image_blk = nullptr;
    MB_BLK venc_image_blk = nullptr;
    int32_t result = 0;
    im_rect osd_rect = {0};
    im_osd_t osd_config = {0};
    uint8_t* test_osd_buf = nullptr;
    VIDEO_FRAME_INFO_S venc_frame_info = {0};
    
    std::fstream test_file("/mnt/sdcard/test_osd.yuv",std::ios::out | std::ios::binary);
    memset(&frame_info,0,sizeof(frame_info));
    result = RK_MPI_VI_GetChnFrame(0,mpi_ctx.video_in.s32ChnId,&frame_info,1000);
    if(result != RK_SUCCESS || !frame_info.stVFrame.pMbBlk){
        LOGE("failed to get vi channel frame");
        goto exit;
    }
    LOGI("got vi channel frame,width:%d,height:%d",frame_info.stVFrame.u32Width,frame_info.stVFrame.u32Height);
    //convert yuv420sp to rgba
    cvt_image_size = mpi_ctx.video_in.vi_height * mpi_ctx.video_in.vi_width * 4; 
    cvt_image_blk = mmz_alloc(cvt_image_size);
    if(!cvt_image_blk){
        LOGE("failed to allocate cvt_image_vec");
        goto exit;
    }
    cvt_image_phy_addr = RK_MPI_MMZ_Handle2PhysAddr(cvt_image_blk);
    
    if(!cvt_image_phy_addr){
        LOGE("failed to conver mpi handle to physical address");
        goto exit;
    }
    cvt_image_buf_handle = importbuffer_physicaladdr(
        cvt_image_phy_addr,
        cvt_image_size);
    if(!cvt_image_buf_handle){
        LOGE("failed to import cvt image buf");
        goto exit;
    }
    cvt_image_buf = wrapbuffer_handle(cvt_image_buf_handle,
                                                   mpi_ctx.video_in.vi_width,
                                                   mpi_ctx.video_in.vi_height,
                                                   RgaSURF_FORMAT::RK_FORMAT_RGBA_8888);
    //import buffer from vi to rga
    vi_buff_fd = RK_MPI_MMZ_Handle2Fd(frame_info.stVFrame.pMbBlk);
    vi_buf_size = frame_info.stVFrame.u32Width * frame_info.stVFrame.u32Height * 3 / 2;
    LOGI("vi buf fd:%d",vi_buff_fd);
    vi_img_buff_handle = importbuffer_fd(
        vi_buff_fd,
        vi_buf_size
    );
    if(!vi_img_buff_handle){
        LOGE("failed to import vi buff_fd");
        goto exit;
    }
    vi_img_buf = wrapbuffer_handle(vi_img_buff_handle,
                                    frame_info.stVFrame.u32Width,
                                    frame_info.stVFrame.u32Height,
                                    RgaSURF_FORMAT::RK_FORMAT_YCbCr_420_SP);
    
    
    res = imcheck(vi_img_buf,cvt_image_buf,{},{});
    if(res != IM_STATUS::IM_STATUS_NOERROR){
        LOGE("imcheck failed for (vi_img_buf,cvt_img_buf). error:%s",imStrError(res));
    }
    res = imcvtcolor(vi_img_buf,cvt_image_buf,
                    RgaSURF_FORMAT::RK_FORMAT_YCbCr_420_SP,
                    RgaSURF_FORMAT::RK_FORMAT_RGBA_8888,
                    IM_YUV_TO_RGB_BT601_LIMIT,1,nullptr);
    if(res != IM_STATUS::IM_STATUS_SUCCESS){
        LOGE("failed to convert vi buff color. res:%s",imStrError(res));
        goto exit;
    }
    //import osd buffer to rga
    osd_img_buf_handle = importbuffer_virtualaddr(osd_rgba_pixels.data(),osd_rgba_pixels.size());
    if(!osd_img_buf_handle){
        LOGE("failed to import osd image buffer to rga");
        goto exit;
    }
    osd_img_buf = wrapbuffer_handle(osd_img_buf_handle,
                                    size.width,
                                    size.height,
                                    RgaSURF_FORMAT::RK_FORMAT_RGBA_8888);
    osd_rect.x = 16;
    osd_rect.y = 16;
    osd_config.osd_mode = IM_OSD_MODE_STATISTICS | IM_OSD_MODE_AUTO_INVERT;

    osd_config.block_parm.width_mode = IM_OSD_BLOCK_MODE_NORMAL;
    osd_config.block_parm.width = osd_rect.width;
    osd_config.block_parm.block_count = 1;
    osd_config.block_parm.background_config = IM_OSD_BACKGROUND_DEFAULT_BRIGHT;
    osd_config.block_parm.direction = IM_OSD_MODE_HORIZONTAL;
    osd_config.block_parm.color_mode = IM_OSD_COLOR_PIXEL;

    osd_config.invert_config.invert_channel = IM_OSD_INVERT_CHANNEL_COLOR;
    osd_config.invert_config.flags_mode = IM_OSD_FLAGS_EXTERNAL;
    osd_config.invert_config.invert_flags = 0x000000000000002a;
    osd_config.invert_config.flags_index = 1;
    osd_config.invert_config.threash = 40;
    osd_config.invert_config.invert_mode = IM_OSD_INVERT_USE_SWAP;

    res = imcheck(osd_img_buf,cvt_image_buf,{},osd_rect);
    if(res != IM_STATUS::IM_STATUS_NOERROR){
        LOGE("imcheck failed for osd. error:%s",imStrError(res));
        goto exit;
    }
    res = imosd(osd_img_buf,cvt_image_buf,osd_rect,&osd_config,1,nullptr);
    if(res != IM_STATUS::IM_STATUS_SUCCESS){
        LOGE("imosd failed: error:%s",imStrError(res));
        goto exit;
    }

    // TODO: convert back to RgaSURF_FORMAT::RK_FORMAT_YCbCr_420_SP and send to venc
    venc_image_blk = mmz_alloc(vi_buf_size);
    if(!venc_image_blk){
        LOGE("failed to allocate venc image blk");
        goto exit;
    }
    venc_image_phy_addr = RK_MPI_MMZ_Handle2PhysAddr(venc_image_blk);
    if(!venc_image_phy_addr){
        LOGE("failed to get phy address from venc_image_blk");
        goto exit;
    }
    venc_img_buf_handle = importbuffer_physicaladdr(venc_image_phy_addr,vi_buf_size);
    if(!venc_img_buf_handle){
        LOGE("failed to import venc_image_phyaddress");
        goto exit;
    }
    vecn_img_buf = wrapbuffer_handle(venc_img_buf_handle,
                                     mpi_ctx.video_in.vi_width,
                                     mpi_ctx.video_in.vi_height,
                                     RgaSURF_FORMAT::RK_FORMAT_YCbCr_420_SP);
    res = imcvtcolor(cvt_image_buf,vecn_img_buf,
                    RgaSURF_FORMAT::RK_FORMAT_RGBA_8888,
                    RgaSURF_FORMAT::RK_FORMAT_YCbCr_420_SP,
                    IM_RGB_TO_YUV_BT601_LIMIT,
                    1,nullptr);
    if(res != IM_STATUS::IM_STATUS_SUCCESS){
        LOGE("failed to cvt image back to yuv420sp.error:%s",imStrError(res));
        goto exit;
    }
    test_osd_buf = (uint8_t*) RK_MPI_MMZ_Handle2VirAddr(venc_image_blk);
    test_file.write((char*)test_osd_buf,vi_buf_size);
    memcpy(&venc_frame_info,&frame_info,sizeof(frame_info));
    venc_frame_info.stVFrame.pMbBlk = venc_image_blk;
    
    exit:
        if(frame_info.stVFrame.pMbBlk)
            RK_MPI_VI_ReleaseChnFrame(0,mpi_ctx.video_in.s32ChnId,&frame_info);
        if (cvt_image_blk)
            mmz_free(cvt_image_blk);
        if(venc_image_blk)
            mmz_free(venc_image_blk);
        if(cvt_image_buf_handle)
            releasebuffer_handle(cvt_image_buf_handle);
        if(vi_img_buff_handle)
            releasebuffer_handle(vi_img_buff_handle);
        if(osd_img_buf_handle)
            releasebuffer_handle(osd_img_buf_handle);
        if(venc_img_buf_handle)
            releasebuffer_handle(venc_img_buf_handle);
}

void luckfox_mpi::osd_deinit()
{
     if(osd_handles.vi_rgba_buf_handle)
        releasebuffer_handle(osd_handles.vi_rgba_buf_handle);
    if(osd_handles.venc_yuv_buf_handle)
        releasebuffer_handle(osd_handles.venc_yuv_buf_handle);
    if(osd_handles.venc_image_blk)
        mmz_free(osd_handles.venc_image_blk);
    if(osd_handles.cvt_image_blk)
        mmz_free(osd_handles.cvt_image_blk);
    if(osd_handles.osd_image_blk)
        mmz_free(osd_handles.osd_image_blk);
    osd_handles.init_done = false;
}

inline im_osd_t luckfox_mpi::get_osd_config(uint32_t width)
{
    im_osd_t osd_config = {0};
    osd_config.osd_mode = IM_OSD_MODE_STATISTICS | IM_OSD_MODE_AUTO_INVERT;
    osd_config.block_parm.width_mode = IM_OSD_BLOCK_MODE_NORMAL;
    osd_config.osd_mode = IM_OSD_MODE_STATISTICS | IM_OSD_MODE_AUTO_INVERT;

    osd_config.block_parm.width_mode = IM_OSD_BLOCK_MODE_NORMAL;
    osd_config.block_parm.width = width;
    osd_config.block_parm.block_count = 1;
    osd_config.block_parm.background_config = IM_OSD_BACKGROUND_DEFAULT_BRIGHT;
    osd_config.block_parm.direction = IM_OSD_MODE_HORIZONTAL;
    osd_config.block_parm.color_mode = IM_OSD_COLOR_PIXEL;

    osd_config.invert_config.invert_channel = IM_OSD_INVERT_CHANNEL_COLOR;
    osd_config.invert_config.flags_mode = IM_OSD_FLAGS_EXTERNAL;
    osd_config.invert_config.invert_flags = 0x000000000000002a;
    osd_config.invert_config.flags_index = 1;
    osd_config.invert_config.threash = 40;
    osd_config.invert_config.invert_mode = IM_OSD_INVERT_USE_SWAP;
    return osd_config;
}

inline im_rect luckfox_mpi::get_osd_rect(int32_t width, int32_t height)
{
    return im_rect{
        .x = 16,
        .y = 16,
        .width = width,
        .height = height
    };
}

MB_BLK luckfox_mpi::mmz_alloc(size_t size)
{
    void* ret = nullptr;
    int32_t res = 
        RK_MPI_MMZ_Alloc(&ret,size,
                        RK_MMZ_ALLOC_TYPE_CMA|RK_MMZ_SYNC_RW);
        if(res != RK_SUCCESS){
            LOGE("failed to allocate mmz block. error:%d",res);
            return nullptr;
        }
    return ret;
}

void luckfox_mpi::mmz_free(void *ptr)
{
    RK_MPI_MMZ_Free(ptr);
}