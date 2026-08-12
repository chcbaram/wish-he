#ifndef ESP_H_
#define ESP_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_ESP


typedef struct
{
  uint16_t topic_len;
  uint8_t  topic_buf[1024];

  uint16_t messge_len;
  uint8_t  message_buf[1024];
} esp_mqtt_sub_msg_t;


bool espInit(void);
bool espOpen(uint8_t ch, uint32_t baud);
bool espIsOpen(void);

uint32_t espAvailable(void);
uint32_t espWrite(uint8_t *p_data, uint32_t length);
uint8_t  espRead(void);
uint32_t espPrintf(char *fmt, ...);

void espLogEnable(void);
void espLogDisable(void);
bool espCmd(const char *cmd_str, uint32_t timeout);
bool espPing(uint32_t timeout);
bool espWaitOK(uint32_t timeout);
bool espConnectWifi(char *ssd_str, char *pswd_str, uint32_t timeout);
bool espClientBegin(char *ip_str, char *port_str, uint32_t timeout);
bool espClientEnd(void);
bool espClientUpdate(void);

bool espMqttConnect(const char *host_url, uint32_t port, uint32_t timeout);
bool espMqttIsConnected(void);
bool espMqttPub(const char *topic, const char *data, uint8_t qos);
bool espMqttPubRaw(const char *topic, const char *data, uint8_t qos);
bool espMqttSub(const char *topic, uint8_t qos);
bool espMqttUpdateSub(void);
bool espMqttGetSubInfo(esp_mqtt_sub_msg_t *p_sub_msg);

#endif


#ifdef __cplusplus
}
#endif

#endif /* SRC_COMMON_HW_INCLUDE_ESP_H_ */