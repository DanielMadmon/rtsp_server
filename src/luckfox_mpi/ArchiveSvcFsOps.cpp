#include "ArchiveSvc.hpp"
#include "generic_log.h"
#include "utils.hpp"
#include <regex>
#include <filesystem>
#include <boost/lexical_cast.hpp>
namespace lf_mpi{
namespace archive_svc{

using std::regex;
namespace fs = std::filesystem;

ArchiveSvcFsOps::ArchiveSvcFsOps(const ArchiveSvcConfig &config) : 
m_config([&]{
    if(!config.storage_path.ends_with('/')){
        ArchiveSvcConfig cfg_fixed(config);
        cfg_fixed.storage_path.append(1,'/');
        return cfg_fixed;
    }else{
        return config;
    }
}()),
m_init_done(m_config.storage_path.ends_with('/')),
m_max_file_size_bytes(static_cast<uint64_t>(m_config.max_file_size_mb) * (1024ULL * 1024ULL))
{
}

option<fs_string> ArchiveSvcFsOps::get_next_full_file_name()
{
    if(!m_init_done){
        LOGE("must call ArchiveSvcFsOps::ArchiveSvcFsOps(const ArchiveSvcConfig&)"
              "before calling %s",LOC_FFNAME);
        return {};
    }
    /// if no space left remove files from low to high until we have
    /// enough free space
    /// if there is enough space get num files list else nullopt_t
    /// increament by one concat and return
    option<uintmax_t>space_opt = get_available_space_mb();
    const option<vector<size_t>>files_list_opt = get_num_files_list();
    bool new_file_do{false};
    if(!space_opt){
        LOGE("in %s failed to get free space info",LOC_FFNAME);
        return{};
    }
    uintmax_t space_free_mb = *space_opt;
    const uintmax_t max_file_size_mb = static_cast<uintmax_t>(m_config.max_file_size_mb);
    if(space_free_mb >= max_file_size_mb){
        //no files found
        if(!files_list_opt){
            return build_full_file_name(0); 
        }else{
            uintmax_t next_filenum = files_list_opt->back() + 1;
            return build_full_file_name(next_filenum);
        }
    }else if(!files_list_opt){
        LOGE("in %s no space for files and no files found",LOC_FFNAME);
        return {};
    }else{
        for(const size_t& filenum : *files_list_opt){
            if(!rm_file(filenum)){
                LOGW("failed to delete file");
                continue;
            }
            space_opt = get_available_space_mb();
            if(!space_opt){
                LOGE("Failed to get available space in mb");
                return {};
            }
            space_free_mb = *space_opt;
            if(space_free_mb >= max_file_size_mb){
                new_file_do = true;
                break;
            }
        }
    }
    if(new_file_do){
        const option<vector<size_t>>files_list_opt_2 = get_num_files_list();
         if(!files_list_opt_2){
            return build_full_file_name(0); 
        }else{
            uintmax_t next_filenum_2 = files_list_opt_2->back() + 1;
            return build_full_file_name(next_filenum_2);
        }
    }
    return {};
}

option<bool> ArchiveSvcFsOps::need_new_file(const fs_string &full_filename) const
{
    uintmax_t file_size{0};
    fs::path file_path{};
    std::error_code ec;
    try{
        file_path.assign(full_filename.c_str());
    }catch(const std::exception& e){
        LOGE("caught %s in %s",e.what(),LOC_FFNAME);
        return {};
    }
    file_size = fs::file_size(full_filename.c_str(),ec);
    if(ec.value() != 0){
        return{};
    }
    if(file_size >= m_max_file_size_bytes){
        return true;
    }else{
        return false;
    }
}

/// @brief get numbers of existing files sorted low to high
/// @return optional vector of size_t representing every existing file
option<vector<size_t>> ArchiveSvcFsOps::get_num_files_list() const
{
    if(!m_init_done){
        LOGE("must call ArchiveSvcFsOps::ArchiveSvcFsOps(const ArchiveSvcConfig&)"
              "before calling %s",LOC_FFNAME);
        return {};
    }
    regex_str re_str(m_config.filename_prefix);
    vector<size_t> ret_files;
    fs::path storage_path;
    std::regex re;
    try{
        ret_files.reserve(512);
        re_str.insert(re_str.begin(),{'^'});
        re_str.append("(\\d+)\\.mp4$");
        re.assign(re_str.c_str());
        storage_path.assign(m_config.storage_path.c_str());
    }catch(const std::exception& e){
        LOGE("caught %s in %s",e.what(),LOC_FFNAME);
        return {};
    }
    
    for(const fs::directory_entry& entry : fs::directory_iterator(storage_path)){
        if(!entry.is_regular_file() || entry.is_directory()){
            continue;
        }
        std::smatch match;
        try{
            std::string name(entry.path().filename().c_str());
           
            if(std::regex_match(name,match,re)){
                int32_t i32_match = std::stoi(match[1].str());
                if(i32_match < 0) continue;
                ret_files.push_back(static_cast<size_t>(i32_match));
            }
        }catch(const std::exception& e){
            LOGE("caught %s in %s",e.what(),LOC_FFNAME);
            return {};
        }
    }
    if(!ret_files.empty()){
        std::sort(ret_files.begin(),ret_files.end());
        return ret_files;
    }
    return {};
}

/// @brief get available space in megabytes
/// @return optional uintmax_t value, if error occures returns nullopt_t
option<uintmax_t> ArchiveSvcFsOps::get_available_space_mb() const
{
    if(!m_init_done){
        LOGE("must call ArchiveSvcFsOps::ArchiveSvcFsOps(const ArchiveSvcConfig&)"
              "before calling %s",LOC_FFNAME);
        return {};
    }
    fs::path storage_path;
    try{
        storage_path.assign(m_config.storage_path.c_str());
    }catch(const std::exception& e){
        LOGE("caught %s in %s",e.what(),LOC_FFNAME);
        return {};
    }
    std::error_code ec;
    fs::space_info sinfo = fs::space(storage_path,ec);
    if(ec.value() == 0){
        return (sinfo.available / (1024 * 1024));
    }
    return {};
}

bool ArchiveSvcFsOps::rm_file(size_t filenum) const
{
    option<fs_string> filename_opt = build_full_file_name(filenum);
    if(!filename_opt){
        return false;
    }
    fs::path filepath{};
    try{
        filepath.assign(filename_opt->c_str());
    }catch(const std::exception& e){
        LOGE("caught %s in %s",e.what(),LOC_FFNAME);
        return false;
    }
    std::error_code ec{};
    bool ret = fs::remove(filepath,ec);
    if(!ret || ec.value() != 0){
        LOGE("fs::remove() returned %s for file:%s",ec.message().c_str(),filename_opt->c_str());
        return false;
    }
    return true;
}

option<fs_string> ArchiveSvcFsOps::build_full_file_name(size_t filenum) const
{
    if(!m_init_done){
        LOGE("must call ArchiveSvcFsOps::ArchiveSvcFsOps(const ArchiveSvcConfig&)"
              "before calling %s",LOC_FFNAME);
        return {};
    }
    fs_string ret_filename;
    std::string tmp_str;
    try{
        ret_filename.append(m_config.storage_path);
        ret_filename.append(m_config.filename_prefix);
        tmp_str = boost::lexical_cast<std::string>(filenum).c_str();        
        if(tmp_str.empty()){
            LOGE("in %s failed to cast filenum to string",LOC_FFNAME);
            return {};
        }
        ret_filename.append(tmp_str);
        ret_filename.append(".mp4");
        return ret_filename;
    }catch(const std::exception& e){
        LOGE("caught %s in %s",e.what(),LOC_FFNAME);
        return {};
    }
}

}//namespace archive_svc
}//namespace lf_mpi