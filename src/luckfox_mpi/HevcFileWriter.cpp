#include <limits>
#include "HevcFileWriter.hpp"

namespace lf_mpi {
namespace hevc_file_writer {

HevcFileWriter::HevcFileWriter() : 
    m_out_ctx(nullptr),
    m_stream_p(nullptr),
    m_header_written(false),
    m_extradata_extracted(false),
    m_err_buf{0},
    m_pts_us_base({false,0}),
    m_config(HevcFileWriterConfig{}),
    m_last_dts{AV_NOPTS_VALUE},
    m_trailer_written{false},
    m_pool_alloc()
{}

/// @brief initialize ffmpeg and open file for output
/// @param config HevcFileWriter with params
/// @return true on success
bool HevcFileWriter::init(HevcFileWriterConfig config)
{
    m_config = config;
    if(!m_pool_alloc.init(150 * 1024)){
        LOGE("in %s failed to initialize pool",LOC_FFNAME);
        return false;
    }
    // Initialize output context for MP4
    AVFormatContext* _av_format_ctx_raw{nullptr};
    int ret = avformat_alloc_output_context2(&_av_format_ctx_raw, nullptr,
        "mp4", m_config.storage_path.c_str());
    if(ret != 0 || !_av_format_ctx_raw){
        av_strerror(ret, m_err_buf, sizeof(m_err_buf));
        LOGE("%s", m_err_buf);
        return false;
    }
    m_out_ctx.reset(_av_format_ctx_raw);
    
    // Create video stream
    m_stream_p = avformat_new_stream(m_out_ctx.get(), nullptr);
    if(!m_stream_p){
        LOGE("avformat_new_stream failed");
        return false;
    }
    
    m_stream_p->id = m_out_ctx->nb_streams - 1;
    m_stream_p->time_base = AVRational{1, 90000};  // MP4 standard timebase
    
    // Set codec parameters
    AVCodecParameters* stream_par = m_stream_p->codecpar;
    stream_par->codec_type = AVMEDIA_TYPE_VIDEO;
    stream_par->codec_id = AV_CODEC_ID_HEVC;
    stream_par->format = AV_PIX_FMT_YUVJ420P;  // after testing is equal to RK_FMT_YUV420SP
    stream_par->width = static_cast<int>(m_config.width);
    stream_par->height = static_cast<int>(m_config.height);
    stream_par->bit_rate = 8192 * 1024;  // current encoder bitrate
    
    // Open output file
    if(!(m_out_ctx->oformat->flags & AVFMT_NOFILE)){
        ret = avio_open(&m_out_ctx->pb, m_config.storage_path.c_str(), AVIO_FLAG_WRITE);
        if(ret != 0){
            av_strerror(ret, m_err_buf, sizeof(m_err_buf));
            LOGE("%s", m_err_buf);
            return false;
        }
    }
    m_finalize_done = false;
    m_trailer_written = false;
    return true;
}
/// @brief write annexb_data to a file
/// @warning must be called after successfull call to init()
/// @param annexb_data pointer to annexb formated data
/// @param len size in bytes
/// @param pts_us microseconds timestamp from encoder
/// @return true on success
bool HevcFileWriter::write(uint8_t *annexb_data, size_t len, uint64_t pts_us)
{
    if(!m_out_ctx || !m_stream_p){
        LOGE("HevcFileWriter is not properly initialized");
        return false;
    }
    if(!annexb_data || len == 0){
        LOGE("(!annexb_data || len == 0) == true");
        return false;
    }
    if(len > static_cast<size_t>(INT_MAX)){
        LOGE("len > static_cast<size_t>(std::numeric_limits<int>::max() == true");
        return false;
    }
    int ret = -1;
    if(!m_pts_us_base.init_done){
        m_pts_us_base.init_done = true;
        m_pts_us_base.pts_us = pts_us;
    }
    
    // Convert timestamp from microseconds to MP4 timebase (90kHz)
    int64_t pts = AV_NOPTS_VALUE;
    if(pts_us != 0 && pts_us != UINT64_MAX && pts_us <= static_cast<uint64_t>(INT64_MAX)){
        // Convert microseconds to 90kHz clock
        ///TODO: replace with monotonic clock?
        pts = av_rescale_q(static_cast<int64_t>(pts_us - m_pts_us_base.pts_us), 
                          AVRational{1, 1000000},  // microseconds
                          AVRational{1, 90000});   // MP4 timebase
    } else {
        LOGW("invalid timestamp received, timestamp:%llu", 
             static_cast<unsigned long long>(pts_us));
    }
    
    // Create packet with copied data
    AvPacketPtr packet(av_packet_alloc());
    if(!packet){
        LOGE("failed to allocate packet");
        return false;
    }
    
    AVBufferRef* buffer = m_pool_alloc.get_buffer(len);
    // Copy data to FFmpeg-managed buffer
    if(!buffer){
        LOGE("failed to allocate data buffer, size:%zu", len);
        return false;
    }
    if(buffer->size < static_cast<int>(len)){
        LOGE("failed to allocate data buffer, size:%zu", len);
        av_buffer_unref(&buffer);
        return false;
    }
    memcpy(buffer->data, annexb_data, len);
    
    packet->data = buffer->data;
    packet->size = static_cast<int>(len);
    packet->pts = pts;
    packet->dts = pts;  // after testing RV1106 outputs only I/P frames
    packet->stream_index = m_stream_p->index;
    packet->buf = buffer;
    
    // Detect keyframes to set flag
    if(len >= 5){
        int32_t offset = (annexb_data[0] == 0 && annexb_data[1] == 0) ?
                         (annexb_data[2] == 1 ? 3 :
                         (annexb_data[2] == 0 && annexb_data[3] == 1 ? 4 : 0)) : 0;
        if(offset > 0 && len > static_cast<size_t>(offset)){
            uint8_t nal_header = annexb_data[offset];
            uint8_t nal_type = (nal_header >> 1) & 0x3F;
            if(nal_type == 19 || nal_type == 20){
                packet->flags |= AV_PKT_FLAG_KEY;
            }
        }
    }
    
    // Extract extradata
    if(!m_extradata_extracted && !(packet->flags & AV_PKT_FLAG_KEY)){
        // Find parameter sets in the AnnexB stream
        // For H.265, look for NAL types: VPS(32), SPS(33), PPS(34)
        std::vector<uint8_t> extradata;
        size_t i = 0;
        while(i < len){
            // Find start code
            if(i + 3 < len && annexb_data[i] == 0 && annexb_data[i+1] == 0){
                int start_code_len = (annexb_data[i+2] == 1) ? 3 : 
                                    (i+3 < len && annexb_data[i+2] == 0 && annexb_data[i+3] == 1) ? 4 : 0;
                if(start_code_len > 0){
                    i += start_code_len;
                    if(i < len){
                        uint8_t nal_type = (annexb_data[i] >> 1) & 0x3F;
                        // VPS(32), SPS(33), PPS(34)
                        if(nal_type == 32 || nal_type == 33 || nal_type == 34){
                            // Find end of this NAL unit
                            size_t nal_start = i - start_code_len;
                            size_t nal_end = len;
                            for(size_t j = i; j < len - 3; j++){
                                if(annexb_data[j] == 0 && annexb_data[j+1] == 0 && 
                                   (annexb_data[j+2] == 1 || (annexb_data[j+2] == 0 && annexb_data[j+3] == 1))){
                                    nal_end = j;
                                    break;
                                }
                            }
                            size_t nal_len = nal_end - nal_start;
                            extradata.push_back(static_cast<uint8_t>((nal_len >> 24) & 0xFF));
                            extradata.push_back(static_cast<uint8_t>((nal_len >> 16) & 0xFF));
                            extradata.push_back(static_cast<uint8_t>((nal_len >> 8) & 0xFF));
                            extradata.push_back(static_cast<uint8_t>((nal_len) & 0xFF));

                            // Add to extradata
                            extradata.insert(extradata.end(), 
                                           annexb_data + nal_start, 
                                           annexb_data + nal_end);
                            i = nal_end;
                            continue;
                        }
                    }
                }
            }
            i++;
        }
        
        if(!extradata.empty()){
            if(m_stream_p->codecpar->extradata){
                av_freep(&m_stream_p->codecpar->extradata);
            }
            // Set extradata for the stream
            m_stream_p->codecpar->extradata = 
                static_cast<uint8_t*>(av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
            if(m_stream_p->codecpar->extradata){
                memcpy(m_stream_p->codecpar->extradata, extradata.data(), extradata.size());
                m_stream_p->codecpar->extradata_size = static_cast<int>(extradata.size());
                m_extradata_extracted = true;
            }
        }
    }
    
    // Write header on first packet (once we have extradata)
    if(!m_header_written){
        // Ensure we have extradata, otherwise create dummy
        if(!m_stream_p->codecpar->extradata){
            // skip packet if extradata does not exist yet
            LOGW("No extradata available yet, skipping packet");
            return false;
        }
        
        ret = avformat_write_header(m_out_ctx.get(), nullptr);
        if(ret != 0){
            av_strerror(ret, m_err_buf, sizeof(m_err_buf));
            LOGE("avformat_write_header failed: %s", m_err_buf);
            return false;
        }
        m_header_written = true;
    }
    
    // Ensure DTS doesn't go backwards or stay at the same point
    if(packet->dts != AV_NOPTS_VALUE && 
       m_last_dts != AV_NOPTS_VALUE &&
       packet->dts <= m_last_dts){
        LOGW("dts decreased/same from %lld to %lld - adjusting", 
             static_cast<long long>(m_last_dts), 
             static_cast<long long>(packet->dts));
        packet->dts = m_last_dts + 1;
        if(packet->pts < packet->dts){
            packet->pts = packet->dts;
        }
    }
    
    if(packet->dts != AV_NOPTS_VALUE){
        m_last_dts = packet->dts;
    }
    
    // Write packet
    ret = av_interleaved_write_frame(m_out_ctx.get(), packet.get());
    if(ret != 0){

        av_strerror(ret, m_err_buf, sizeof(m_err_buf));
        LOGE("av_interleaved_write_frame failed: %s", m_err_buf);
        return false;
    }
    
    return true;
}

/// @brief close file and write trailer
/// @warning after call, must call init() before write()
/// @return true on success
bool HevcFileWriter::finalize()
{
    int ret{-1};
    if(!m_out_ctx || !m_header_written || !m_extradata_extracted){
        return true;
    }
    if(m_out_ctx->pb && m_header_written && !m_trailer_written){
        ret = av_write_trailer(m_out_ctx.get());
    if(ret != 0){
        av_strerror(ret, m_err_buf, AV_ERROR_MAX_STRING_SIZE);
        LOGE("av_write_trailer failed: %s", m_err_buf);
    }
    m_trailer_written = ret == 0;
    if(!(m_out_ctx->oformat && !(m_out_ctx->oformat->flags & AVFMT_NOFILE))){
        ret = avio_closep(&m_out_ctx->pb);
        if(ret != 0){
            av_strerror(ret, m_err_buf, AV_ERROR_MAX_STRING_SIZE);
            LOGE("avio_closep failed: %s", m_err_buf);
        }
    }
    if(m_stream_p && m_stream_p->codecpar && m_stream_p->codecpar->extradata){
        av_freep(&m_stream_p->codecpar->extradata);
        m_stream_p->codecpar->extradata_size = 0;
    }
    LOGI("MP4 file finalized successfully");
    }
    m_out_ctx.reset();
    m_stream_p = nullptr;
    m_header_written = false;
    m_extradata_extracted = false;
    m_last_dts = AV_NOPTS_VALUE;
    m_pts_us_base.init_done = false;
    m_pts_us_base.pts_us = 0;
    m_finalize_done = true;
    return ret;
}

void HevcFileWriter::cleanup()
{
    if(!m_finalize_done){
        finalize();
    }
}

HevcFileWriter::~HevcFileWriter() noexcept
{
    try{
        cleanup();
    }catch(const std::exception& e){
        LOGE("caught %s in HevcFileWriter destructor", e.what());
    }
}

} // namespace hevc_file_writer
} // namespace lf_mpi
