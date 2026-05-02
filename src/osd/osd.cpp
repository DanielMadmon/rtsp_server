/****OSD functionality*****/


#include "config.h"
#include <time.h>
#include <memory>
#include <stdio.h>
#include <fstream>
#include <errno.h>
#include <string.h>
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
    ttf_file.resize(file_size);
    std::vector<uint8_t>tmp_buf(file_size);
    font_file_handle.read(reinterpret_cast<char*>(&tmp_buf[0]),file_size);
    ttf_file.swap(tmp_buf);
    if(font_file_handle.fail()){
        LOGE("failed to read ttf file, bytes read %d",font_file_handle.gcount());
        return false;
    }else{
        LOGD("read %d bytes from ttf file.",font_file_handle.gcount());
    }
    if(!stbtt_InitFont(&font_info,ttf_file.data(),0)){
        LOGE("failed to initialize font");
        return false;
    }else{
        return true;
    }
    
}
void osd::text_osd::set_font_size(uint32_t size)
{
    st_font_size = size;
}

bool osd::text_osd::init_glyph_map(std::string glyphs)
{
    font_scale = stbtt_ScaleForPixelHeight(&font_info,(float)st_font_size);
    font_atlas.resize(512 * 512);
    if(!stbtt_PackBegin(&font_pack_ctx,font_atlas.data(),font_atlas_w,font_atlas_h,0,1,nullptr)){
        LOGE("in %s. failed begin packing",__FUNCTION__);
        return false;
    }
    stbtt_PackSetOversampling(&font_pack_ctx,1,1);
    stbtt_pack_range range = {static_cast<float>(st_font_size), char_begin, NULL, char_num, chardata};
    stbtt_PackFontRanges(&font_pack_ctx, ttf_file.data(), 0, &range, 1);
    stbtt_PackEnd(&font_pack_ctx);
    return init_font_size_info(char_begin,char_num,font_scale);
}

size_t osd::text_osd::get_pxbuf_size(const std::string& text,bmp_resolution& res){
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &linegap);  // EM units
    float line_height = (ascent - descent + linegap) * font_scale;  // Pixels
    float x = 0;
    int prev = 0;
    for (char c : text) {
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font_info, c, &advance, &lsb);
        x += advance * font_scale;
        int kern = stbtt_GetCodepointKernAdvance(&font_info, prev,c);
        LOGD("lsb:%d",lsb);
        x += kern * font_scale;
        prev = c;
    }
    x = (x + 16.0 * 2);
    line_height = (line_height + 4 * 2);
    res.width = static_cast<int32_t>(ceilf(x));
    res.width = res.width % 4 == 0 ? res.width : res.width + res.width % 4;
    res.height = static_cast<int32_t>(ceilf(line_height));
    res.height = res.height % 4 == 0 ? res.height : res.height + res.height % 4;
    return res.width * res.height * RGBA_SIZE;
}


bool osd::text_osd::init_font_size_info(const int32_t& char_begin, const int32_t& char_num,float scale){
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &linegap);  // EM units
    m_font_size_info.scale = scale;
    m_font_size_info.line_height = (ascent - descent + linegap) * font_scale;
    m_font_size_info.char_begin = char_begin;
    m_font_size_info.char_num = char_num;
    const size_t num_chars = m_font_size_info.char_num - m_font_size_info.char_begin;
    try{
        m_font_size_info.size_info.resize(num_chars);
    }catch(const std::bad_alloc& e){
        LOGE("%s.%s,%d",e.what(),__FUNCTION__,__LINE__);
        return false;
    }
    int32_t advance = 0, lsb = 0;
    for(size_t i = 0; i < num_chars; ++i){
        stbtt_GetCodepointHMetrics(&font_info, i + m_font_size_info.char_begin, &advance, &lsb);
        m_font_size_info.size_info[i].advance = advance;
        m_font_size_info.size_info[i].lsb = lsb;
    }
    return true;
}

size_t osd::text_osd::get_pxbuf_size_pre_calc(const struct font_size_info& szi,
                                              const std::string& text, bmp_resolution& res){
    float x = 0.0f;
    for(char c : text){
        if(c < szi.char_begin + szi.char_num && c >= szi.char_begin){
            size_t idx = c - get_char_idx(c,szi);
            x += szi.size_info[idx].advance * szi.scale;
        }
    }
    x = (x + szi.x_padding * 2);
    float y = szi.line_height + szi.y_padding * 2; 
    int32_t width = static_cast<int32_t>(ceil(x));
    int32_t height = static_cast<int32_t>(ceil(y));
    width = (width + 3) &~ 0x03;
    height = (height + 3) &~ 0x03;
    res.width = width;
    res.height = height;
    return width * height * RGBA_SIZE;
}

bool osd::save_rgba_to_bmp(const char* filename, uint8_t* buffer,bmp_resolution& res)
{
    int ret = 0;
    ret = stbi_write_bmp(filename,res.width,res.height,4,&buffer[0]);
    return ret == 1;
}



std::string osd::get_local_time(AtcTimeZone& tz,time_t& out_raw_time){
    AtcZonedDateTime zdt;
    char time_str[80] = {0};
    AtcStringBuffer atc_buf = {
        .p = time_str,
        .capacity = sizeof(time_str),
        .size = 0
    };
    out_raw_time = 0;
    time(&out_raw_time);
    atc_zoned_date_time_from_epoch_seconds(&zdt, out_raw_time, &tz);
    atc_zoned_date_time_print(&atc_buf,&zdt);
    std::string ret_str = std::string(time_str);
    size_t idx = ret_str.find('+');
    if(idx != std::string::npos){
        ret_str.erase(idx);
    }
    return ret_str;
}

void osd::init_local_time(const AtcZoneInfo* zone_info,AtcZoneProcessor* processor, AtcTimeZone *tz)
{
    *tz = {zone_info,processor};
    atc_processor_init(processor);
    atc_set_current_epoch_year(1970);
}

