#pragma once
#include "stb_rect_pack.h"
#include "stb_truetype.h"
#include <string>
#include <vector>
#define FONT_ATLAS_W 512
#define FONT_ATLAS_H 512
#define FONT_ATLAS_DEPTH 4
#define TEXT_W 400
#define TEXT_H 64
#define TEXT_IMAGE_BUFFER_SIZE TEXT_W*TEXT_H*FONT_ATLAS_DEPTH
#define TEXT_X_POS 16
#define TEXT_Y_POS 16

namespace osd{

    typedef struct{
        int32_t width;
        int32_t height;
    }bmp_resolution;

    class text_osd{
        public:
            text_osd(uint32_t font_size);
            ~text_osd();
            void set_font_size(uint32_t size);
            bool load_ttf_file(const std::string& ttf_file_path);
            bool render_text_rgba8888(const std::string& text,std::vector<uint8_t>& buffer,bmp_resolution& res);
            bool save_rgba8888_to_bmp(const std::string& filename, std::vector<uint8_t>& buffer,bmp_resolution& res);
        private:
            std::vector<uint8_t> ttf_file;
            uint32_t st_font_size = 16;  
            stbtt_fontinfo font_info = {0};   
            stbrp_context pack_ctx;
    };
}