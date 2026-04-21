#ifndef __RTSP_SRV_CONF__
#define __RTSP_SRV_CONF__

#define CONF_AIQ_FILES_PATH "/oem/usr/share/iqfiles"
#define CONF_SENSOR_WIDTH 2304
#define CONF_SENSOR_HEIGHT 1296
#define CONF_HDR_MODE RK_AIQ_WORKING_MODE_NORMAL
#define CONF_IP_ADDR "127.0.0.1"
#define CONF_RTSP_PATH "rtsp://" CONF_IP_ADDR ":554/live"
#define CONF_OSD_ENABLE 1
#define CONF_OSD_FONT_PATH "/oem/usr/share/simsun_en.ttf"
#define CONF_DAEMON_PATH "/root/rtsp_daemon"
#endif