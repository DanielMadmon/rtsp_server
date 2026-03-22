## Rtsp Server
- RTSP server for luckfox pico
- make && upload build/bin/rtsp_server to your luckfox pico && run
- ffplay rtsp://luckfox pico address/live

## Config
- modify [rtsp_server/config.h](src/rtsp_server/config.h) to your specific configuration
## Features
 - Support Linux and Windows platforms.
 - Support H.265/G711A/AAC
 - Support rtp over udp, rtp over tcp
 - Support digest authentication