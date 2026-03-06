// PHZ
// 2018-6-7
// http://ffmpeg.org/doxygen/trunk/rtpenc__hevc_8c_source.html
// https://www.twblogs.net/a/5e554018bd9eee211684990e

#if defined(WIN32) || defined(_WIN32) 
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include "H265Source.h"
#include <cstdio>
#include <chrono>
#if defined(__linux) || defined(__linux__) 
#include <sys/time.h>
#endif
#include <generic_log.h>

using namespace xop;
using namespace std;

H265Source::H265Source(uint32_t framerate)
: framerate_(framerate)
{
	payload_    = 96;
	media_type_ = H265;
	clock_rate_ = 90000;
}

H265Source* H265Source::CreateNew(uint32_t framerate)
{
	return new H265Source(framerate);
}

H265Source::~H265Source()
{
	
}

string H265Source::GetMediaDescription(uint16_t port)
{
	char buf[100] = {0};
	sprintf(buf, "m=video %hu RTP/AVP/TCP 96", port);
	return string(buf);
}
	
string H265Source::GetAttribute()
{
	return string("a=rtpmap:96 H265/90000");
}

bool H265Source::HandleFrame(MediaChannelId channel_id, AVFrame frame)
{
	uint8_t *frame_buf  = frame.buffer.get();
	uint32_t frame_size = frame.size;

	if (frame.timestamp == 0) {
	    GetTimestamp(&frame.timeNow, &frame.timestamp);
	}
        
	if (frame_size <= MAX_RTP_PAYLOAD_SIZE)
	{
	    uint32_t start_code = 0;
	    if(frame_buf[0] == 0 && frame_buf[1] == 0 && frame_buf[2] == 1)
        {
            start_code = 3;
        }
        else if(frame_buf[0] == 0 && frame_buf[1] == 0 && frame_buf[2] == 0 && frame_buf[3] == 1)
        {
            start_code = 4; //after test start_code always 4
        }
	    //fixed for VLC [hevc @ 0x7fc85c04b680] Invalid NAL unit 0, skipping.
	    frame_buf  += start_code; //shift to nalu position
	    frame_size -= start_code;

	    RtpPacket rtp_pkt;

	    //rtp_pkt.data        = new uint8_t[RTP_PACKET_SIZE];
	    rtp_pkt.data        = std::shared_ptr<uint8_t>(new uint8_t[RTP_PACKET_SIZE]);
		//rtp_pkt.type        = frame.type;
		rtp_pkt.timestamp   = frame.timestamp;
		rtp_pkt.timeNow     = frame.timeNow;
		rtp_pkt.size        = RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE + frame_size;
		rtp_pkt.last        = 1;

		memcpy(rtp_pkt.data.get()+RTP_TCP_HEAD_SIZE+RTP_HEADER_SIZE, frame_buf, frame_size);

        if (m_pThis && m_pCallMember)
        {
            (m_pThis->*m_pCallMember)(channel_id, rtp_pkt);
        }

		rtp_pkt.data.reset();
	}	
	else
	{
	    /*
         7 6 5 4 3 2 1 0
        +-+-+-+-+-+-+-+-+
        |F|    Type     |
        +---------------+
        Type 49 is fragmentation unit
         7 6 5 4 3 2 1 0
        +-+-+-+-+-+-+-+-+
        | LayerId | TID |
        +---------------+
         7 6 5 4 3 2 1 0
        +-+-+-+-+-+-+-+-+
        |S|E|  FuType   |
        +---------------+
        */

	    uint32_t start_code = 0;
        if(frame_buf[0] == 0 && frame_buf[1] == 0 && frame_buf[2] == 1)
        {
            start_code = 3;
        }
        else if(frame_buf[0] == 0 && frame_buf[1] == 0 && frame_buf[2] == 0 && frame_buf[3] == 1)
        {
            start_code = 4; //after test start_code always 4
        }
	    frame_buf  += start_code; //shift to nalu position
	    frame_size -= start_code;

	    //refer live555 void H264or5Fragmenter::doGetNextFrame() of H264or5VideoRTPSink.cpp
	    uint8_t PL_FU[3]    = {0}; //payload 2 bytes, FU 1 byte
	    uint8_t nalUnitType = (frame_buf[0] & 0x7e) >> 1;
		PL_FU[0] = (frame_buf[0] & 0x81) | (49<<1);
		PL_FU[1] = frame_buf[1];
		PL_FU[2] = 0x80 | nalUnitType;
        if((PL_FU[2] >> 6) & (0xC0)){
			LOGE("error S&E bits set in rtp packets");
		}
		frame_buf  += 2;
		frame_size -= 2;
        
		while (frame_size + 3 > MAX_RTP_PAYLOAD_SIZE)
		{
			RtpPacket rtp_pkt;

			rtp_pkt.data        = std::shared_ptr<uint8_t>(new uint8_t[RTP_PACKET_SIZE]);
			rtp_pkt.timestamp   = frame.timestamp;
			rtp_pkt.timeNow     = frame.timeNow;
			rtp_pkt.size        = RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE + MAX_RTP_PAYLOAD_SIZE; //3 FU in
			rtp_pkt.last        = 0;
			if((PL_FU[2] >> 6) & (0xC0)){
				LOGE("error S&E bits set in rtp packets");
			}
			rtp_pkt.data.get()[RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE + 0] = PL_FU[0];
			rtp_pkt.data.get()[RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE + 1] = PL_FU[1];
			rtp_pkt.data.get()[RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE + 2] = PL_FU[2];

			memcpy(rtp_pkt.data.get()+RTP_TCP_HEAD_SIZE+RTP_HEADER_SIZE+3, frame_buf, MAX_RTP_PAYLOAD_SIZE-3);

	        if (m_pThis && m_pCallMember)
	        {
	            (m_pThis->*m_pCallMember)(channel_id, rtp_pkt);
	        }

			rtp_pkt.data.reset();
            
			frame_buf  += (MAX_RTP_PAYLOAD_SIZE - 3);
			frame_size -= (MAX_RTP_PAYLOAD_SIZE - 3);
        
			PL_FU[2] &= ~0x80; //FU header (no Start bit)
		}
        
		//final packet
		{
		    RtpPacket rtp_pkt;

		    //rtp_pkt.data        = new uint8_t[RTP_PACKET_SIZE];
		    rtp_pkt.data        = std::shared_ptr<uint8_t>(new uint8_t[RTP_PACKET_SIZE]);
			//rtp_pkt.type        = frame.type;
			rtp_pkt.timestamp   = frame.timestamp;
			rtp_pkt.timeNow     = frame.timeNow;
			rtp_pkt.size        = RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE + 3 + frame_size;
			rtp_pkt.last        = 1;

			PL_FU[2] |= 0x40; //set the E bit in the FU header
			rtp_pkt.data.get()[RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE + 0] = PL_FU[0];
			rtp_pkt.data.get()[RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE + 1] = PL_FU[1];
			rtp_pkt.data.get()[RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE + 2] = PL_FU[2];

			memcpy(rtp_pkt.data.get()+RTP_TCP_HEAD_SIZE+RTP_HEADER_SIZE+3, frame_buf, frame_size);

	        if (m_pThis && m_pCallMember)
	        {
	            (m_pThis->*m_pCallMember)(channel_id, rtp_pkt);
	        }

			rtp_pkt.data.reset();
		}            
	}

	return true;
}

/*
 https://datatracker.ietf.org/doc/html/rfc3984
 Timestamp: 32 bits
 The RTP timestamp is set to the sampling timestamp of the content.
 A 90 kHz clock rate MUST be used.
 */
void H265Source::GetTimestamp(struct timeval* tv, uint32_t* ts)
{

    uint32_t timestamp;
    gettimeofday(tv, NULL);
    timestamp   = (tv->tv_sec + tv->tv_usec/1000000.0)*90000 + 0.5; //90khz sound
    *ts         = htonl(timestamp);
}
