#include "luckfox_mpi.hpp"
#include <iostream>
#include "generic_log.h"



luckfox_mpi::luckfox_mpi(const char *rknn_path)
{
    this->mpi_ctx.rknn_path = rknn_path;
}

bool luckfox_mpi::init_video_in(rk_aiq_working_mode_t mode, int32_t fps,uint32_t width,uint32_t height)
{
    this->mpi_ctx.video_in.vi_width = width;
    this->mpi_ctx.video_in.vi_height = height;
    this->mpi_ctx.video_in.hdr_mode = mode;
    this->mpi_ctx.video_in.vi_fps   = fps;
    bool result = this->init_vi();
    if(result){
        LOGI("init_video_in done.");
    }else{
        LOGE("init video failed");
    }
    return result;
}

SAMPLE_VENC_CTX_S* luckfox_mpi::init_video_encoder(pthread_t get_frame_cb_thread)
{

    return nullptr;
}


bool luckfox_mpi::init_vi()
{
    //1. init device
    int32_t result = RK_FAILURE;
    char hdr_env_var[16] = {0};
    snprintf(hdr_env_var,sizeof(hdr_env_var),"%d",(int)this->mpi_ctx.video_in.hdr_mode);
    setenv("HDR_MODE",hdr_env_var,1);
    LOGD("enumarating static metas by phy ID");
    result = 
        rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(this->vi_dev_id,&this->mpi_ctx.video_in.aiq_static_info);
    if(result != XCAM_RETURN_NO_ERROR){
        LOGE("failed to enumarate sensor by phy ID. \
                result:%d, line:%d, file:%s",result,__LINE__,__FILE__);
        return false;
    }
    LOGD("sensor enumarated by ID.");
    //2. set video in device buffer count
    result = 
        rk_aiq_uapi2_sysctl_preInit_devBufCnt(this->mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name,
                                             "rkraw_rx",this->vi_buf_count);
    if(result != XCAM_RETURN_NO_ERROR){
        LOGE("failed to set buffers for sensor.\
                result:%d, line:%d, file:%s",result,__LINE__,__FILE__);
        return false;
    }
    LOGD("device buffer count init");
    //2.5 set preinit scene
    result =  rk_aiq_uapi2_sysctl_preInit_scene(this->mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name,
                                            "normal",
                                            "day");
    if(result != RK_SUCCESS){
        LOGE("failed to set preinit scene. error code:%d",result);
    }
    //3. initialize control system context
    this->mpi_ctx.video_in.aiq_ctx = 
        rk_aiq_uapi2_sysctl_init(this->mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name,
                                this->mpi_ctx.rknn_path,
                                NULL,NULL);
    if(!this->mpi_ctx.video_in.aiq_ctx){
        LOGE("failed to initialize sysctl.\
             line: %d,file:%s",__LINE__,__FILE__);
        return false;
    }
    LOGD("init sysctl done.");
    LOGD("sensor name:%s",this->mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name);
    LOGD("width:%d",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->width);
    LOGD("height:%d",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->height);
    LOGD("fps:%d",mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->fps);
    LOGD("sensor pixel format:%s",xcam_fourcc_to_string(this->mpi_ctx.video_in.aiq_static_info.sensor_info.support_fmt->format));
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
    result = RK_MPI_VI_GetDevAttr(this->vi_dev_id, &this->mpi_ctx.video_in.stDevAttr);
    if(result == RK_ERR_VI_NOT_CONFIG){
        LOGD("device not configured setting up.");
        result = RK_MPI_VI_SetDevAttr(this->vi_dev_id,&this->mpi_ctx.video_in.stDevAttr);
        if(result != RK_SUCCESS){
            LOGE("failed to Set VI dev attributes. error code:%d",result);
            return false;
        }else{
            result = RK_MPI_VI_GetDevIsEnable(this->vi_dev_id);
            if(result != RK_SUCCESS){
                //5.enable device
                result = RK_MPI_VI_EnableDev(this->vi_dev_id);
                if(result != RK_SUCCESS){
                    LOGE("failed to enable vi device. error code:%d",result);
                    return false;
                }else{
                    this->mpi_ctx.video_in.stBindPipe.u32Num = 1;
                    this->mpi_ctx.video_in.stBindPipe.bUserStartPipe[0] = RK_FALSE;
                    this->mpi_ctx.video_in.stBindPipe.PipeId[0] = this->vi_dev_id;
                    //6.bind device output to pipe. again currently does nothing and use in bypass mode.
                    result = RK_MPI_VI_SetDevBindPipe(this->vi_dev_id,&this->mpi_ctx.video_in.stBindPipe);
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
    this->mpi_ctx.video_in.stChnAttr.stSize.u32Height = this->mpi_ctx.video_in.vi_height;
    this->mpi_ctx.video_in.stChnAttr.stSize.u32Width = this->mpi_ctx.video_in.vi_width;
    this->mpi_ctx.video_in.stChnAttr.stIspOpt.u32BufCount = this->vi_buf_count;
    this->mpi_ctx.video_in.stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    this->mpi_ctx.video_in.stChnAttr.u32Depth = 2;
    this->mpi_ctx.video_in.stChnAttr.enPixelFormat = RK_FMT_YUV422_UYVY;
    this->mpi_ctx.video_in.stChnAttr.stFrameRate.s32SrcFrameRate = -1;
    this->mpi_ctx.video_in.stChnAttr.stFrameRate.s32DstFrameRate = -1;

    //8, set vi output channel attributes
    result = RK_MPI_VI_SetChnAttr(
        this->mpi_ctx.video_in.stBindPipe.PipeId[0],
        this->vi_dev_id,
        &this->mpi_ctx.video_in.stChnAttr
    );
    if(result != RK_SUCCESS){
        LOGE("failed to set vi channel attribute. error code: %d",result);
        return false;
    }
    result = RK_MPI_VI_EnableChn(
        this->mpi_ctx.video_in.stBindPipe.PipeId[0],
        this->vi_dev_id
    );
    if(result != RK_SUCCESS){
        LOGE("failed to enable video in channel. error code %d", result);
        return false;
    }
    this->mpi_ctx.video_in.b_vi_channel_en = true;
    return true;
}

bool init_vpss(){
    return false;
}

RK_S32 luckfox_mpi::init_venc()
{

    return -1;
}

luckfox_mpi::~luckfox_mpi()
{
    int32_t result_dev = RK_MPI_VI_GetDevIsEnable(this->vi_dev_id);
    if(result_dev == RK_SUCCESS){
        RK_MPI_VI_DisableDev(this->vi_dev_id);
    }
    if(this->mpi_ctx.video_in.b_vi_channel_en){
        RK_MPI_VI_DisableChn(this->vi_dev_id,this->vi_dev_id);
    }
}