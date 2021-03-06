/******************************************************************************
 * Copyright 2015 Piotr Sperka (http://www.piotrsperka.info)
 * Copyright 2016 karawin (http://www.karawin.fr)
 *
 * FileName: user_main.c
 *
 * Description: entry file of user application
*******************************************************************************/
#include "esp_common.h"
#include "esp_softap.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "el_uart.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "interface.h"
#include "webserver.h"
#include "webclient.h"
#include "buffer.h"
#include "extram.h"
#include "vs1053.h"
#include "ntp.h"

#include "eeprom.h"
#include <time.h>

void uart_div_modify(int no, unsigned int freq);

//	struct station_config config;
uint8_t FlashOn = 5,FlashOff = 5;
uint8_t FlashCount = 0xFF;
os_timer_t ledTimer;
bool ledStatus = true; // true: normal blink, false: led on when playing
sc_status status = 0;
	
void cb(sc_status stat, void *pdata)
{
	printf("SmartConfig status received: %d\n",status);
	status = stat;
	if (stat == SC_STATUS_LINK_OVER) if (pdata) printf("SmartConfig: %d:%d:%d:%d\n",((char*) pdata)[0],((char*)pdata)[1],((char*)pdata)[2],((char*)pdata)[3]);
}



void testtask(void* p) {
struct device_settings *device;	
/*
	int uxHighWaterMark;
	uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
	printf("watermark testtask: %x  %d\n",uxHighWaterMark,uxHighWaterMark);
*/

	gpio2_output_conf();
	vTaskDelay(10);
	
	while(FlashCount==0xFF) {
		if (ledStatus) gpio2_output_set(0);
		vTaskDelay(FlashOff);
		
		if (ledStatus) // on led and save volume if changed
		{		
			gpio2_output_set(1);
			vTaskDelay(FlashOn);
		}	

		// save volume if changed		
		device = getDeviceSettings();
		if (device != NULL)
		{	
			if (device->vol != clientIvol){ 
				device->vol = clientIvol;
				saveDeviceSettings(device);

//	uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
//	printf("watermark testtask: %d  heap:%d\n",uxHighWaterMark,xPortGetFreeHeapSize( ));

			}
			free(device);	
		}	
	}
//	printf("t0 end\n");
	vTaskDelete( NULL ); // stop the task
}


ICACHE_FLASH_ATTR void ledCallback(void *pArg) {
struct device_settings *device;	

		FlashCount++;
		
		if ((ledStatus)&&(FlashCount == FlashOff)) gpio2_output_set(1);
		if (FlashCount == FlashOn)
		{
			// save volume if changed		
			device = getDeviceSettings();
			if (device != NULL)
			{	
				if (device->vol != clientIvol)
				{ 
					device->vol = clientIvol;
					saveDeviceSettings(device);
				}
				free(device);	
			}
		}		
		if ((ledStatus)&&(FlashCount == FlashOn)) // on led and save volume if changed
		{		
			gpio2_output_set(0);
			FlashCount = 0;
		}	
		
}

ICACHE_FLASH_ATTR void initLed(void) 
{	
	os_timer_disarm(&ledTimer);
	FlashCount = 0;
	os_timer_setfn(&ledTimer, ledCallback, NULL);
	os_timer_arm(&ledTimer, 10, true); //  and rearm
	vTaskDelay(1);	
//	printf("initLed done\n");
}



void uartInterfaceTask(void *pvParameters) {
	char tmp[255];
//	bool conn = false;
	uint8 ap = 0;
	int i = 0;	
	uint8 maxap;
	
//	int uxHighWaterMark;
//	uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
//	printf("watermark uartInterfaceTask: %x  %d\n",uxHighWaterMark,uxHighWaterMark);

	int t = 0;
	for(t = 0; t<sizeof(tmp); t++) tmp[t] = 0;
	t = 0;
	uart_rx_init();
	printf("UART READY\n");
	
//-------------------------
// AP Connection management
//-------------------------
	struct ip_info *info;
	struct device_settings *device;
	struct device_settings1* device1;
	struct station_config* config;
	wifi_station_set_hostname("WifiWebRadio");
	
	device = getDeviceSettings();
	device1 = getDeviceSettings1();  // extention of saved data
	config = malloc(sizeof(struct station_config));
	info = malloc(sizeof(struct ip_info));
	
// if device1 not initialized, erase it and copy pass2 to the new place
	if (device1->cleared != 0xAABB)
	{		
		eeErasesettings1();
		device1->cleared = 0xAABB; //marker init done
		memcpy(device1->pass2,device->pass2, 60);
		saveDeviceSettings1(device1);	
	}		

	
	wifi_get_ip_info(STATION_IF, info); // ip netmask dw
	wifi_station_get_config_default(config); //ssid passwd
	if ((device->ssid[0] == 0xFF)&& (device->ssid2[0] == 0xFF) )  {eeEraseAll(); device = getDeviceSettings();} // force init of eeprom
	if (device->ssid2[0] == 0xFF) {device->ssid2[0] = 0; device->pass2[0] = 0; }
	printf("AP1: %s, AP2: %s\n",device->ssid,device->ssid2);
		
	if ((strlen(device->ssid)==0)||(device->ssid[0]==0xff)/*||(device->ipAddr[0] ==0)*/) // first use
	{
		printf("first use\n");
		IP4_ADDR(&(info->ip), 192, 168, 1, 254);
		IP4_ADDR(&(info->netmask), 0xFF, 0xFF,0xFF, 0);
		IP4_ADDR(&(info->gw), 192, 168, 1, 254);
		IPADDR2_COPY(&device->ipAddr, &info->ip);
		IPADDR2_COPY(&device->mask, &info->netmask);
		IPADDR2_COPY(&device->gate, &info->gw);
		strcpy(device->ssid,config->ssid);
		strcpy(device->pass,config->password);
		device->dhcpEn = true;
		wifi_set_ip_info(STATION_IF, info);
		saveDeviceSettings(device);	
	}
	
// set for AP1 //
//-------------//
	IP4_ADDR(&(info->ip), device->ipAddr[0], device->ipAddr[1],device->ipAddr[2], device->ipAddr[3]);
	IP4_ADDR(&(info->netmask), device->mask[0], device->mask[1],device->mask[2], device->mask[3]);
	IP4_ADDR(&(info->gw), device->gate[0], device->gate[1],device->gate[2], device->gate[3]);
	strcpy(config->ssid,device->ssid);
	strcpy(config->password,device->pass);
	wifi_station_set_config(config);
	if (!device->dhcpEn) {
//		if ((strlen(device->ssid)!=0)&&(device->ssid[0]!=0xff)&&(!device->dhcpEn))
//			conn = true;	//static ip
		wifi_station_dhcpc_stop();
		wifi_set_ip_info(STATION_IF, info);
	} 
	printf(" AP1:Station Ip: %d.%d.%d.%d\n",(info->ip.addr&0xff), ((info->ip.addr>>8)&0xff), ((info->ip.addr>>16)&0xff), ((info->ip.addr>>24)&0xff));
//----------------
	
	
//	printf("DHCP: 0x%x\n Device: Ip: %d.%d.%d.%d\n",device->dhcpEn,device->ipAddr[0], device->ipAddr[1], device->ipAddr[2], device->ipAddr[3]);
//	printf("\nI: %d status: %d\n",i,wifi_station_get_connect_status());
	wifi_station_connect();
	i = 0;
	while ((wifi_station_get_connect_status() != STATION_GOT_IP))
	{	
		printf("Trying %s,  I: %d status: %d\n",config->ssid,i,wifi_station_get_connect_status());
		FlashOn = FlashOff = 40;

		vTaskDelay(400);//  ms
		if (( strlen(config->ssid) ==0)||  (wifi_station_get_connect_status() == STATION_WRONG_PASSWORD)||(wifi_station_get_connect_status() == STATION_CONNECT_FAIL)||(wifi_station_get_connect_status() == STATION_NO_AP_FOUND))
		{ 
			// try AP2 //
			if ((strlen(device->ssid2) > 0)&& (ap <1))
			{
				IP4_ADDR(&(info->ip), device->ipAddr[0], device->ipAddr[1],device->ipAddr[2], device->ipAddr[3]);
				IP4_ADDR(&(info->netmask), device->mask[0], device->mask[1],device->mask[2], device->mask[3]);
				IP4_ADDR(&(info->gw), device->gate[0], device->gate[1],device->gate[2], device->gate[3]);
				
				strcpy(config->ssid,device->ssid2);
				strcpy(config->password,device1->pass2);
				wifi_station_set_config(config);
				
				if (!device->dhcpEn) {
					wifi_station_dhcpc_stop();
					wifi_set_ip_info(STATION_IF, info);
				} 				
				printf(" AP2:Station Ip: %d.%d.%d.%d\n",(info->ip.addr&0xff), ((info->ip.addr>>8)&0xff), ((info->ip.addr>>16)&0xff), ((info->ip.addr>>24)&0xff));		
				ap++;
			}
			else i = 6; // go to SOFTAP_MODE
		}
		i++;
	
		if (i >= 6)
		{
					printf("\n");
//					smartconfig_stop();
					wifi_station_disconnect();
					FlashOn = 10;FlashOff = 200;
					vTaskDelay(200);
					printf("Config not found\n\n");
					saveDeviceSettings(device);	
					printf("The default AP is  WifiWebRadio. Connect your wifi to it.\nThen connect a webbrowser to 192.168.4.1 and go to Setting\n");
					printf("Erase the database and set ssid, password and ip's field\n");
					struct softap_config *apconfig;
					apconfig = malloc(sizeof(struct softap_config));
					wifi_set_opmode_current(SOFTAP_MODE);
					wifi_softap_get_config(apconfig);
					strcpy (apconfig->ssid,"WifiWebRadio");
					apconfig->ssid_len = 12;					
					wifi_softap_set_config(apconfig);
//					conn = true; 
					free(apconfig);
					break;
		}//

	}
//	wifi_station_set_reconnect_policy(true);
	// update device info
	wifi_get_ip_info(STATION_IF, info);
//	wifi_station_get_config(config);
	IPADDR2_COPY(&device->ipAddr, &info->ip);
	IPADDR2_COPY(&device->mask, &info->netmask);
	IPADDR2_COPY(&device->gate, &info->gw);
//	strcpy(device->ssid,config->ssid);
//	strcpy(device->pass,config->password);
	saveDeviceSettings(device);	

//autostart	
	printf("autostart: playing:%d, currentstation:%d\n",device->autostart,device->currentstation);
	currentStation = device->currentstation;
	VS1053_I2SRate(device->i2sspeed);
	clientIvol = device->vol;

	if (device->autostart ==1)
	{	
		vTaskDelay(10); 
		playStationInt(device->currentstation);
	}
//
	ledStatus = ((device->options & T_LED)== 0);
//
	
	free(info);
	free(device);
	free(device1);
	free(config);
	
	ap = 0;
	ap =system_adc_read();
	vTaskDelay(10);
	ap += system_adc_read();
	
	if (ap <15) // two time
	{		
		adcdiv = 0; // no panel adc grounded
		printf("No panel\n");
	}
	else
	{
	// read adc to see if it is a nodemcu with adc dividor
		if (ap < 800) adcdiv = 3;
			else adcdiv = 1;	
	}
	FlashOn = 190;FlashOff = 10;
	initLed(); // start the timer for led. This will kill the ttest task to free memory
	
	while(1) {
		while(1) {
			int c = uart_getchar_ms(100);
			if (c!= -1)
			{
				if((char)c == '\r') break;
				if((char)c == '\n') break;
				tmp[t] = (char)c;
				t++;
				if(t == sizeof(tmp)) t = 0;
			}
			switchCommand() ;  // hardware panel of command
		}
		checkCommand(t, tmp);
//	uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
//	printf("watermark uartInterfaceTask: %d  heap:%d\n",uxHighWaterMark,xPortGetFreeHeapSize( ));
		
		
		for(t = 0; t<sizeof(tmp); t++) tmp[t] = 0;
		t = 0;
	}
}

/*
UART_SetBaudrate(uint8 uart_no, uint32 baud_rate) {
	uart_div_modify(uart_no, UART_CLK_FREQ / baud_rate);
}
*/


/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 user_rf_cal_sector_set(void)
{
    uint32 rf_cal_sec = 0;
    flash_size_map size_map = system_get_flash_size_map();
    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}


/******************************************************************************
 * FunctionName : test_upgrade
 * Description  : check if it is an upgrade. Convert if needed
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
/*void test_upgrade(void)
{
	uint8 autotest;
	struct device_settings *settings;
	struct shoutcast_info* station;
	int j;
	eeGetOldData(0x0C070, &autotest, 1);
	printf ("Upgrade autotest before %d\n",autotest);
	if (autotest == 3) // old bin before 1.0.6
	{
		autotest = 0; //patch espressif 1.4.2 see http://bbs.espressif.com/viewtopic.php?f=46&t=2349
		eeSetOldData(0x0C070, &autotest, 1);
		printf ("Upgrade autotest after %d\n",autotest);
		settings = getOldDeviceSettings();
		saveDeviceSettings(settings);
		free(settings);
		eeEraseStations();
		for(j=0; j<192; j++){
			station = getOldStation(j) ;	
			saveStation(station, j);
			free(station);			
			vTaskDelay(1); // avoid watchdog
		}
		
	}		
}
*/
/******************************************************************************
 * FunctionName : checkUart
 * Description  : Check for a valid uart baudrate
 * Parameters   : baud
 * Returns      : baud
*******************************************************************************/
uint32_t checkUart(uint32_t speed)
{
	uint32_t valid[] = {1200,2400,4800,9600,14400,19200,28800,38400,57600,76880,115200,230400};
	int i = 0;
	for (i;i<12;i++){
		if (speed == valid[i]) return speed;
	}
	return 115200; // default
}
/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void user_init(void)
{
	struct device_settings *device;
	uint32_t uspeed;
//	REG_SET_BIT(0x3ff00014, BIT(0));
//	system_update_cpu_freq(SYS_CPU_160MHZ);
//	system_update_cpu_freq(160); //- See more at: http://www.esp8266.com/viewtopic.php?p=8107#p8107
	char msg[] = {"%s task: %x\n"};
	xTaskHandle pxCreatedTask;
    Delay(200);
	device = getDeviceSettings();
	uspeed = device->uartspeed;
	free(device);
	uspeed = checkUart(uspeed);
	uart_div_modify(0, UART_CLK_FREQ / uspeed);//UART_SetBaudrate(0,uspeed);
	VS1053_HW_init(); // init spi
//	test_upgrade();
	extramInit();
	initBuffer();
	wifi_set_opmode_current(STATION_MODE);
//	Delay(10);	
	printf("Release 1.3.2\n");
	printf("SDK %s\n",system_get_sdk_version());
	system_print_meminfo();
	printf ("Heap size: %d\n",xPortGetFreeHeapSize( ));
	clientInit();
	Delay(10);	
	
    flash_size_map size_map = system_get_flash_size_map();
	printf ("size_map: %d\n",size_map);
	
	xTaskCreate(testtask, "t0", 110, NULL, 1, &pxCreatedTask); // DEBUG/TEST 110
	printf(msg,"t0",pxCreatedTask);
	xTaskCreate(uartInterfaceTask, "t1", 300, NULL, 2, &pxCreatedTask); // 304
	printf(msg,"t1",pxCreatedTask);
	xTaskCreate(vsTask, "t4", 380, NULL,4, &pxCreatedTask); //370
	printf(msg,"t4",pxCreatedTask);
	xTaskCreate(clientTask, "t3", 750, NULL, 5, &pxCreatedTask); // 830
	printf(msg,"t3",pxCreatedTask);
	xTaskCreate(serverTask, "t2", 230, NULL, 3, &pxCreatedTask); //230
	printf(msg,"t2",pxCreatedTask);
//	xTaskCreate(ntpTask, "t5", 210, NULL, 2, &pxCreatedTask); // NTP
//	printf("t5 task: %x\n",pxCreatedTask);
	printf ("Heap size: %d\n",xPortGetFreeHeapSize( ));
}
