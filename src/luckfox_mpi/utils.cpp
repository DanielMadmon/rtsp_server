#include "utils.hpp"
#include <filesystem>
namespace lf_mpi::utils{
bool file_is_larger_than(std::string &filename, uintmax_t size)noexcept
{
    namespace fs = std::filesystem;
    const fs::path file_path(filename);
    std::error_code ec{};
    const bool exist = fs::exists(file_path,ec);
    if(!exist || ec.value() != 0){
        return false;
    }
    const std::uintmax_t fsize = fs::file_size(file_path,ec);
    if(ec.value() != 0){
        return false;
    }
    if(fsize > size){
        return true;
    }else{
        return false;
    }
}
bool file_is_larger_than(std::string &filename, uintmax_t size, const size_t count_max, size_t &count)noexcept
{
    if(count_max > count){
        ++count;
        return false;
    }else{
        count = 0;
        return file_is_larger_than(filename,size);
    }
}
}