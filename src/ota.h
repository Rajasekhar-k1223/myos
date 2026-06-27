#ifndef OTA_H
#define OTA_H

void ota_init(void);
int  ota_check_update(void);
int  ota_download_and_install(void);

#endif
