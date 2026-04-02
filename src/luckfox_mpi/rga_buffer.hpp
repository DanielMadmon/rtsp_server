#pragma once
#include <cstddef>
#include <cstdint>
#include "rga.h"
#include "im2d.h"
#include <array>
namespace rga_buf{
    class rga_buffer{
        
        public:
            enum rga_buffer_type_t{
                BUFFER_TYPE_PHYSICAL,
                BUFFER_TYPE_FD,
                BUFFER_TYPE_VIRTUAL
            };
            typedef struct rga_buffer_address{
                rga_buffer_type_t buffer_type;
                union{
                    uint64_t phy_address;
                    void*    vir_address;
                    int32_t  fd;
                };
            }rga_buf_addr_t;

            rga_buffer();
            bool create_buffer(
                uint32_t width,
                uint32_t height,
                RgaSURF_FORMAT format,
                rga_buf_addr_t addr,
                rga_buffer_t* out_buffer
            );
            size_t get_buffer_size(
                uint32_t width,
                uint32_t height,
                RgaSURF_FORMAT format
            );
            ~rga_buffer();
            static im_osd_t get_osd_config(uint32_t width);
            static im_rect get_osd_rect(int32_t width, int32_t height);
        private:
            uint32_t _width = 0;
            uint32_t height = 0;
            static inline constexpr size_t max_handles = 64;
            rga_buffer_handle_t rga_buffer_handles[max_handles] = {0};
            size_t current_handle_idx = 0;
    };
}