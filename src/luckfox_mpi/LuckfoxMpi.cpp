#include "LuckfoxMpi.hpp"
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

using namespace lf_mpi;

LuckfoxMpi::LuckfoxMpi(std::string rknn_path)
{
    mpi_ctx.rknn_path = rknn_path;
    mpi_ctx.osd_enable = false;
    mpi_ctx.video_encoder.b_venc_en = false;
    mpi_ctx.video_in.b_vi_channel_en = false;
}

bool LuckfoxMpi::init_video_in(rk_aiq_working_mode_t mode, int32_t fps,uint32_t width,uint32_t height)
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
bool LuckfoxMpi::init_video_encoder(RK_CODEC_ID_E codec,uint32_t width,uint32_t height)
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

bool LuckfoxMpi::init_vi()
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
    memset(&expSwAttr,0,sizeof(expSwAttr));
    expSwAttr.sync.sync_mode = rk_aiq_uapi_mode_sync_e::RK_AIQ_UAPI_MODE_DEFAULT;
    expSwAttr.sync.done = true;
    expSwAttr.Enable = true;
    expSwAttr.RawStatsMode = CalibDb_CamRawStatsModeV2_t::CAM_RAWSTATSV2_MODE_Y;
    expSwAttr.HistStatsMode = CalibDb_CamHistStatsModeV2_t::CAM_HISTV2_MODE_Y;
    expSwAttr.YRangeMode = CalibDb_CamYRangeModeV2_t::CAM_YRANGEV2_MODE_FULL;
    expSwAttr.AecRunInterval = 0;
    expSwAttr.AecOpType = RKAiqOPMode_t::RK_AIQ_OP_MODE_AUTO;

    //stAuto setting
    expSwAttr.stAuto.stAeSpeed.SmoothEn = true;
    expSwAttr.stAuto.stAeSpeed.DampOver = 0.15f;
    expSwAttr.stAuto.stAeSpeed.DampUnder = 0.45f;
    expSwAttr.stAuto.stAeSpeed.DampDark2Bright = 0.15f;
    expSwAttr.stAuto.stAeSpeed.DampBright2Dark = 0.45f;
    expSwAttr.stAuto.stAeSpeed.DyDamp.DyDampEn = true;
    expSwAttr.stAuto.stAeSpeed.DyDamp.SlowOPType = RKAiqOPMode_t::RK_AIQ_OP_MODE_AUTO;
    expSwAttr.stAuto.stAeSpeed.DyDamp.SlowRange = 10.0f;
    expSwAttr.stAuto.stAeSpeed.DyDamp.SlowDamp = 0.95f;
    expSwAttr.stAuto.DelayType = Uapi_DelayTypeV2_t::DELAY_TYPE_FRAME;
    expSwAttr.stAuto.BlackDelay = 2;
    expSwAttr.stAuto.WhiteDelay = 2;
    expSwAttr.stAuto.stFrmRate.isFpsFix = true;
    expSwAttr.stAuto.stFrmRate.FpsValue = 0;//????
    expSwAttr.stAuto.stAntiFlicker.enable = true;
    expSwAttr.stAuto.stAntiFlicker.Frequency = CalibDb_FlickerFreqV2_t::AECV2_FLICKER_FREQUENCY_50HZ;
    expSwAttr.stAuto.stAntiFlicker.Mode = CalibDb_AntiFlickerModeV2_t::AECV2_ANTIFLICKER_AUTO_MODE;
    expSwAttr.stAuto.LinAeRange.stExpTimeRange.Min = 2.45098035520641133E-5F;
    expSwAttr.stAuto.LinAeRange.stExpTimeRange.Max = 0.0299999993;
    expSwAttr.stAuto.LinAeRange.stGainRange.Min = 1;
    expSwAttr.stAuto.LinAeRange.stGainRange.Max = 512;
    expSwAttr.stAuto.LinAeRange.stIspDGainRange.Min = 1;
    expSwAttr.stAuto.LinAeRange.stIspDGainRange.Max = 1;
    expSwAttr.stAuto.LinAeRange.stPIrisRange.Min = 128;
    expSwAttr.stAuto.LinAeRange.stPIrisRange.Max = 512;
    expSwAttr.stAuto.HdrAeRange.stPIrisRange.Min = 128;
    expSwAttr.stAuto.HdrAeRange.stPIrisRange.Max = 512;

    //stManual settings
    expSwAttr.stManual.LinearAE.ManualTimeEn = true;
    expSwAttr.stManual.LinearAE.ManualGainEn = true;
    expSwAttr.stManual.LinearAE.ManualIspDgainEn = true;
    expSwAttr.stManual.LinearAE.TimeValue = 1.0f / 100.0f;
    expSwAttr.stManual.LinearAE.GainValue = 1;
    expSwAttr.stManual.LinearAE.IspDGainValue = 1;
    expSwAttr.stManual.HdrAE.ManualTimeEn = true;
    expSwAttr.stManual.HdrAE.ManualGainEn = true;
    expSwAttr.stManual.HdrAE.ManualIspDgainEn = true;
    expSwAttr.stManual.HdrAE.TimeValue[0] = 1.0f / 100.0f;
    expSwAttr.stManual.HdrAE.TimeValue[1] = 1.0f / 50.0f;
    expSwAttr.stManual.HdrAE.TimeValue[2] = 1.0f / 25.0f;
    expSwAttr.stManual.HdrAE.GainValue[0] = 1.0f;
    expSwAttr.stManual.HdrAE.GainValue[1] = 1.0f;
    expSwAttr.stManual.HdrAE.GainValue[2] = 1.0f;
    expSwAttr.stManual.HdrAE.IspDGainValue[0] = 1.0f;
    expSwAttr.stManual.HdrAE.IspDGainValue[1] = 1.0f;
    expSwAttr.stManual.HdrAE.IspDGainValue[2] = 1.0f;
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
bool LuckfoxMpi::init_vpss(){
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

LuckfoxMpi::~LuckfoxMpi()
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


bool LuckfoxMpi::bind_vin_vpss(){
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

bool LuckfoxMpi::bind_vin_venc()
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



bool LuckfoxMpi::bind_vpss_venc()
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
uint8_t* LuckfoxMpi::venc_get_stream(bool restart,size_t *stream_len,uint64_t* timestamp)
{
    int32_t rk_result = 0;
    uint8_t * stream_ptr = NULL;
    if(!stream_len || !timestamp){
        LOGE("Null pointer passed to luckfox_mpi::venc_get_stream!");
        return NULL;
    }
    if(!enabled_flags.venc_enabled || !enabled_flags.venc_start_rcv){
        LOGE("call to luckfox_mpi::venc_get_stream before enabling/starting video encoder!");
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

bool LuckfoxMpi::venc_release_stream(){
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

MB_BLK LuckfoxMpi::vi_get_frame(VIDEO_FRAME_INFO_S* out_frame_info)
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
bool lf_mpi::LuckfoxMpi::venc_send_frame(VIDEO_FRAME_INFO_S &frame_info)
{
    
    int32_t res = RK_MPI_VENC_SendFrame(mpi_ctx.video_encoder.s32ChnId,&frame_info,1000);
    return res == RK_SUCCESS;
}

void LuckfoxMpi::vi_release_frame()
{
    if(mpi_ctx.video_in.frame_info.stVFrame.pMbBlk){
        RK_MPI_VI_ReleaseChnFrame(0,mpi_ctx.video_in.s32ChnId,&mpi_ctx.video_in.frame_info);
        mpi_ctx.video_in.frame_info.stVFrame.pMbBlk = nullptr;
    }
}

/// @brief request IDR from venc
/// @return true on success
bool LuckfoxMpi::venc_restart()
{
    int32_t rk_result = 0;
    rk_result = RK_MPI_VENC_RequestIDR(mpi_ctx.video_encoder.s32ChnId,RK_FALSE);
    return rk_result == RK_SUCCESS;
}


/// @brief must be called after init_video_encoder and before venc_get_stream
/// @return false on fail
bool LuckfoxMpi::start_video_encoder(){
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
    return true;
}