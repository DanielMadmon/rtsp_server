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
    return result;
}

SAMPLE_VENC_CTX_S* luckfox_mpi::init_video_encoder(pthread_t get_frame_cb_thread)
{

    return nullptr;
}


bool luckfox_mpi::init_vi()
{
    int32_t result = RK_FAILURE;
    char hdr_env_var[16] = {0};
    snprintf(hdr_env_var,sizeof(hdr_env_var),"%d",(int)this->mpi_ctx.video_in.hdr_mode);
    setenv("HDR_MODE",hdr_env_var,1);
    result = 
        rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(0,&this->mpi_ctx.video_in.aiq_static_info);
    if(result != XCAM_RETURN_NO_ERROR){
        LOGE("failed to enumarate sensor by phy ID. \
                result:%d, line:%s, file:%s",result,__LINE__,__FILE__);
        return false;
    }
    result = 
        rk_aiq_uapi2_sysctl_preInit_devBufCnt(this->mpi_ctx.video_in.aiq_static_info.sensor_info.sensor_name,
                                             "rkraw_rx",this->vi_buf_count);
    if(result != XCAM_RETURN_NO_ERROR){
        LOGE("failed to set buffers for sensor.\
                result:%d, line:%s, file:%s",result,__LINE__,__FILE__);
        return false;
    }
    return result;
}

RK_S32 luckfox_mpi::init_venc()
{
    return -1;
}

luckfox_mpi::~luckfox_mpi()
{
}