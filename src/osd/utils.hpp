#pragma once
#include <vector>
#include <array>
#include <cstddef>

namespace osd_utils{
    template<typename Tp, 
    typename Alloc = std::allocator<Tp>,
    std::size_t Stride>
    void vec_stride_assign(std::vector<Tp,Alloc>& vec,
                           size_t begin,
                           size_t end,
                           std::array<Tp,Stride> const& t){
        if(t.size() == 0 || begin >= end){
            return;
        }
        if(end > vec.size()){
            vec.resize(end);
        }
        for(size_t base = begin; base + Stride <= end; base += Stride){
            for(size_t s = 0; s < Stride && (base + s) < end; ++s){
                vec[base + s] = t[s];
            }
        }
    }
    int32_t write_rgba_png(const std::string& filename ,const std::vector<uint8_t> pixels, int32_t width, int32_t height);
}