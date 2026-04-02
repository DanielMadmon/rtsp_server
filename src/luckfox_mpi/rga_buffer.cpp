#include "rga_buffer.hpp"
#include "generic_log.h"
#include "RgaUtils.h"


rga_buf::rga_buffer::rga_buffer()
{
}

bool rga_buf::rga_buffer::create_buffer(uint32_t width, uint32_t height, 
                                        RgaSURF_FORMAT format, rga_buffer::rga_buf_addr_t address, 
                                        rga_buffer_t *out_buffer)
{
    if(!out_buffer){
        LOGE("null passed to rga_buf::rga_buffer::create_buffer");
        return false;
    }
    if(current_handle_idx >= max_handles){
        LOGE("can't create rga buffer. max buffers count is %d",max_handles);
        return true;
    }
    if(width == 0 && height == 0){
        LOGE("rga buffer width and height can't both be zero");
        return false;
    }
   
    rga_buffer_handle_t handle = 0;
    im_handle_param_t handle_param{
        .width = width,
        .height = height,
        .format = format
    };
    switch (address.buffer_type)
    {
    case BUFFER_TYPE_PHYSICAL:
        handle = importbuffer_physicaladdr(address.phy_address,&handle_param);
        break;
    case BUFFER_TYPE_VIRTUAL:
        handle = importbuffer_virtualaddr(address.vir_address,&handle_param);
        break;
    case BUFFER_TYPE_FD:
        handle = importbuffer_fd(address.fd,&handle_param);
        break;
    default:
        break;
    }
    if(handle == 0){
        LOGE("failed to create rga buffer. importbuffer failed");
        return false;
    }
    rga_buffer_handles[current_handle_idx] = handle;
    current_handle_idx += 1;
    *out_buffer = wrapbuffer_handle(
        handle,
        width,
        height,
        format
    );

    return true;
}

size_t rga_buf::rga_buffer::get_buffer_size(uint32_t width, uint32_t height, RgaSURF_FORMAT format)
{
    uint32_t fixed_width = width == 0 ? 1 : width;
    uint32_t fixed_height = height == 0 ? 1 : height;
    size_t ret = fixed_width * fixed_height * get_bpp_from_format(format);
    return ret;
}

rga_buf::rga_buffer::~rga_buffer()
{
    for(rga_buffer_handle_t handle : rga_buffer_handles){
        if(handle){
            releasebuffer_handle(handle);
        }
    }
}

im_osd_t rga_buf::rga_buffer::get_osd_config(uint32_t width)
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

im_rect rga_buf::rga_buffer::get_osd_rect(int32_t width, int32_t height)
{
    return im_rect{
        .x = 16,
        .y = 16,
        .width = width,
        .height = height
    };
}