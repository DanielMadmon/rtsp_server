/****OSD functionality*****/


#include "config.h"
#include <time.h>
#include <memory>
#include <stdio.h>
#include <fstream>
#include <errno.h>
#include <string.h>
#include "generic_log.h"
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_RECT_PACK_IMPLEMENTATION
#include "osd.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

osd::text_osd::text_osd(uint32_t font_size)
{
    st_font_size = font_size;
}

osd::text_osd::~text_osd(){

}


bool osd::text_osd::load_ttf_file(const std::string &ttf_file_path)
{
    std::ifstream font_file_handle(ttf_file_path,std::ios::binary | std::ios::ate);
    if(!font_file_handle){
        LOGE("failed to open file %s",ttf_file_path.c_str());
        return false;
    }
    size_t file_size = font_file_handle.tellg();
    font_file_handle.seekg(0,std::ios::beg);
    ttf_file.reserve(file_size);
    font_file_handle.read(reinterpret_cast<char*>(&ttf_file[0]),file_size);
    if(font_file_handle.fail()){
        LOGE("failed to read ttf file, bytes read %d",font_file_handle.gcount());
        return false;
    }else{
        LOGD("read %d bytes from ttf file.",font_file_handle.gcount());
        return true;
    }
}


bool osd::text_osd::render_text_rgba(const std::string &text, std::vector<uint8_t> &buffer, bmp_resolution &res)
{
    
    int res_stb = stbtt_InitFont(&font_info,&ttf_file[0],0);
    if(!res_stb){
        LOGE("failed to initialize font.");
        return false;
    }
    float scale = 0.f,ascent_px = 0.f,descent_px = 0.f,total_width = 0.f,x_pos = 0.f;
    int ascent = 0, descent = 0, line_gap = 0,glyph_idx = 0, advance_width = 0,left_bearing = 0;
    int width = 0, height = 0;
    //get vertical scale for current height
    scale = stbtt_ScaleForPixelHeight(&font_info,st_font_size);

    //Measure the text    
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
    ascent_px = ascent * scale;
    descent_px = -descent * scale;// make positive
    
    for(char c : text){
        glyph_idx = stbtt_FindGlyphIndex(&font_info,c);
        stbtt_GetGlyphHMetrics(&font_info,glyph_idx,&advance_width,&left_bearing);
        total_width += advance_width * scale;
    }
    width = static_cast<int32_t>(std::ceil(total_width));
    height = static_cast<int32_t>(std::ceil(ascent_px + descent_px));

    if(width <= 0 || height <= 0){
        LOGE("invalid width or height params for font.");
        return false;
    }
    res.width = width;
    res.height = height;
    buffer.resize(width * height * 4,0);
    for (size_t idx = 0; idx < buffer.size(); idx++){
        buffer[idx + 3] = 255; //default alpha value
    }

    float y_baseline = ascent_px;
    int gx0 = 0, gx1 = 0, gy0 = 0, gy1 = 0;
    int glyph_w = 0, glyph_h = 0;
    int dst_x = 0, dst_y = 0;
    int px = 0, py = 0;
    uint8_t alpha = 0;
    size_t out_buf_idx = 0;
    for(char c:text){
        glyph_idx = stbtt_FindGlyphIndex(&font_info,c);
        stbtt_GetGlyphBitmapBox(&font_info,glyph_idx,scale,scale,&gx0,&gy0,&gx1,&gy1);
        glyph_w = gx1 - gx0;
        glyph_h = gy1 - gy0;
        //advance without rendering(e.g space)
        if(glyph_w <= 0 || glyph_h <= 0){
            stbtt_GetGlyphHMetrics(&font_info,glyph_idx,&advance_width,&left_bearing);
            x_pos += advance_width * scale;
            continue;
        }
        std::vector<uint8_t>glyph_grayscale_bmp(glyph_w * glyph_h,0);
        stbtt_MakeGlyphBitmap(&font_info,
                                glyph_grayscale_bmp.data(),
                                glyph_w,glyph_h,glyph_w,
                                scale,
                                scale,
                                glyph_idx);
        dst_x = static_cast<int>(x_pos + gx0);
        dst_y = static_cast<int>(y_baseline + gy0);
        //convert grayscale to rgba (white on black) and fill output buffer
        for(int y = 0; y < glyph_h; y++){
            for(int x = 0; x < glyph_w; x++){
                alpha = glyph_grayscale_bmp[y * glyph_w + x];
                if(alpha == 0) continue;
                px = dst_x + x;
                py = dst_y + y;
                if(px >= 0 && px < width && py >= 0 && py < height){
                    out_buf_idx = (py * width + px) * 4;
                    buffer[out_buf_idx] = 0;
                    buffer[out_buf_idx + 1] = 0;
                    buffer[out_buf_idx + 2] = 0;
                    buffer[out_buf_idx + 3] = alpha; 
                }
            }//end x loop
        }//end y loop
        stbtt_GetGlyphHMetrics(&font_info,glyph_idx,&advance_width,&left_bearing);
        x_pos += advance_width * scale;
    }//end input text string iter
    return true;
}
void osd::text_osd::set_font_size(uint32_t size)
{
    st_font_size = size;
}

bool osd::text_osd::save_rgba_to_bmp(const std::string &filename, std::vector<uint8_t> &buffer,bmp_resolution& res)
{
    int ret = 0;
    ret = stbi_write_bmp(filename.c_str(),res.width,res.height,4,&buffer[0]);
    return ret == 1;
}