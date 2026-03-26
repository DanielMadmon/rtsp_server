#pragma once
#include "stb_rect_pack.h"
#include "stb_truetype.h"
#include <string>
#include <vector>
#include "acetimec/src/acetimec.h"

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
            bool render_text_rgba(const std::string& text,std::vector<uint8_t>& buffer,bmp_resolution& res);
            bool save_rgba_to_bmp(const std::string& filename, std::vector<uint8_t>& buffer,bmp_resolution& res);
        private:
            std::vector<uint8_t> ttf_file;
            uint32_t st_font_size = 16;  
            stbtt_fontinfo font_info = {0};   
            stbrp_context pack_ctx;
    };

    std::string get_local_time(AtcTimeZone& tz);
}