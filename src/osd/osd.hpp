#pragma once
#include "stb_rect_pack.h"
#include "stb_truetype.h"
#include <string>
#include <map>
#include <vector>
#include "acetimec/src/acetimec.h"
#include <atomic>
#include <vector>
#include "generic_log.h"
#include "utils.hpp"
#include "stb_image_write.h"
namespace osd{
    constexpr char SPACE_CHAR = ' ';
    constexpr size_t RGBA_SIZE = 4;
    typedef struct{
        int32_t width;
        int32_t height;
    }bmp_resolution;
    struct __font_size_info{
        /// unscaled x distance between glyphs
        int32_t advance;
        /// currently unused. unscaled x distance between current glyph begin to current glyph end
        int32_t lsb;
    };
    struct font_size_info{
        std::vector<__font_size_info>size_info;
        int32_t char_begin = 32;
        int32_t char_num = 95;
        float line_height;
        float scale;
        float x_padding = 16.0;
        float y_padding = 4.0;
    };
    void init_local_time(const AtcZoneInfo* zone_info,AtcZoneProcessor* processor, AtcTimeZone* tz);
    class text_osd{
        private:
            size_t get_pxbuf_size(const std::string& text,bmp_resolution& res);
            size_t get_pxbuf_size_pre_calc(const struct font_size_info& szi,const std::string& text, bmp_resolution& res);
            bool init_font_size_info(const int32_t& char_begin, const int32_t& char_num,float scale);
            std::vector<uint8_t> ttf_file;
            /// font size in vertical pixels(i.e height)
            uint32_t st_font_size = 16;  
            stbtt_fontinfo font_info = {0};
            stbtt_pack_context font_pack_ctx{};   
            stbrp_context pack_ctx;
            std::vector<uint8_t>font_atlas{};
            float font_scale = 0.0;
            int st_space_char_width = 32;
            uint8_t st_default_alpha = 0xff;
            uint8_t st_default_rgb   = 0;
            const int32_t font_atlas_w = 512;
            const int32_t font_atlas_h = 512;
            stbtt_packedchar chardata[95]{0};
            struct font_size_info m_font_size_info{};
        public:
            text_osd(uint32_t font_size);
            ~text_osd();
            void set_font_size(uint32_t size);
            bool load_ttf_file(const std::string& ttf_file_path);
            bool init_glyph_map(std::string glyphs);
            template <typename CharType>
            int32_t get_char_idx(CharType ch){
                return ch - m_font_size_info.char_begin;
            }
            template<typename CharType>
            size_t get_char_idx(CharType ch,const struct font_size_info& szi){
                return ch - szi.char_begin;
            }
            template<typename Alloc>
            bool render_text_rgba(std::string& text,
                                  std::vector<uint8_t,Alloc>& buffer,bmp_resolution& res){
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
                size_t new_buf_size = width * height * 4;
                buffer.resize(new_buf_size,0);
                for (size_t idx = 0; idx < new_buf_size; idx = idx + 4){
                    buffer[idx] = 0;
                    buffer[idx + 1] = 0;
                    buffer[idx + 2] = 0;
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
                                buffer[out_buf_idx] = 255;
                                buffer[out_buf_idx + 1] = 255;
                                buffer[out_buf_idx + 2] = 255;
                                buffer[out_buf_idx + 3] = alpha; 
                            }
                        }//end x loop
                    }//end y loop
                    stbtt_GetGlyphHMetrics(&font_info,glyph_idx,&advance_width,&left_bearing);
                    x_pos += advance_width * scale;
                }//end input text string iter
                return true;
            }
            template<typename Alloc>
            bool render_text_rgba_with_glyph_map(const std::string& text,
                                  std::vector<uint8_t,Alloc>& buffer,bmp_resolution& res){
                size_t buf_size = get_pxbuf_size_pre_calc(m_font_size_info,text,res);
                const size_t width = res.width;
                const size_t height = res.height;
                try{
                    buffer.resize(buf_size);
                }catch(const std::bad_alloc& e){
                    LOGE("%s.%s,%d",e.what(),__FUNCTION__,__LINE__);
                    return false;
                }
                osd_utils::vec_stride_assign(buffer,0,buf_size,std::array<uint8_t,4>{0,0,0,0xff});
                float x = 16.0f;
                float y = std::ceil(0.8 * height + 0.5);
                stbtt_aligned_quad q = {0};

                for(char ch : text){
                    stbtt_GetPackedQuad(chardata,
                        font_atlas_w,
                        font_atlas_h,
                        static_cast<int>(get_char_idx(ch)),
                        &x,
                        &y,
                        &q,
                        1);
                    int ix0 = static_cast<int>(q.x0);
                    int iy0 = static_cast<int>(q.y0);
                    int ix1 = static_cast<int>(q.x1);
                    int iy1 = static_cast<int>(q.y1);

                    // Clamp to buffer boundaries
                    ix0 = std::max(ix0, 0);
                    iy0 = std::max(iy0, 0);
                    ix1 = std::min(ix1, static_cast<int>(width));
                    iy1 = std::min(iy1, static_cast<int>(height));

                    // Texture coordinates (normalized 0..1)
                    float s0 = q.s0, t0 = q.t0;
                    float s1 = q.s1, t1 = q.t1;
                    float ds = (s1 - s0) / (ix1 - ix0);
                    float dt = (t1 - t0) / (iy1 - iy0);
                    for(int32_t row = iy0; row < iy1; ++row){
                        float v = t0 + dt * (row - iy0 + 0.5f);
                        int32_t ty = static_cast<int32_t>(v * font_atlas_h);
                        ty = std::max(0,std::min(ty,font_atlas_h - 1));

                        for(int32_t col = ix0; col < ix1; ++col){
                            float u = s0 + ds * (col - ix0 + 0.5f);
                            int32_t tx = static_cast<int32_t>(u * font_atlas_w);
                            tx = std::max(0,std::min(tx,font_atlas_w - 1));
                            
                            uint8_t alpha = 0;
                            try{
                                alpha = font_atlas.at(ty * font_atlas_w + tx);
                            }catch(const std::out_of_range& e){
                                LOGE("%s,%s,%d",e.what(),__FUNCTION__,__LINE__);
                                return false;
                            }
                            size_t buf_idx = (row * width + col) * RGBA_SIZE;
                            if(buf_idx > buf_size){
                                LOGE("caught in:%s:%d, idx:%d .out of bound access!",__FUNCTION__,__LINE__,buf_idx);
                                LOGD("row:%d,col:%d",row,col);
                                return false;
                            }
                            buffer[buf_idx] = alpha;
                            buffer[buf_idx + 1] = alpha;
                            buffer[buf_idx + 2] = alpha;
                            buffer[buf_idx + 3] = 0xff;
                        }
                    }
                }
                return true;
            }

    };



    bool save_rgba_to_bmp(const char* filename, uint8_t* buffer,bmp_resolution& res);
    std::string get_local_time(AtcTimeZone& tz,time_t& out_raw_time);
}