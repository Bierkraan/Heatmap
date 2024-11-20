#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <HTTPClient.h>
#include <Arduino_JSON.h>

#define WIFI_CHANNEL_SWITCH_INTERVAL  (500)
#define WIFI_CHANNEL_MAX               (13)

#define ESP_WIFI_SSID      "projectJarno"
#define ESP_WIFI_PASS      "nuc12345"
#define ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

//Your Domain name with URL path or IP address with path
String serverName = "http://192.168.88.254:1880/update-sensor";

// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastTime = 0;
// Timer set to 10 minutes (600000)
//unsigned long timerDelay = 600000;
// Set timer to 5 seconds (5000)
unsigned long timerDelay = 5000;

uint8_t level = 0, channel = 1;

static wifi_country_t wifi_country = {.cc="CN", .schan = 1, .nchan = 13}; //Most recent esp32 library struct

typedef struct {
	unsigned frame_ctrl:16;
	unsigned duration_id:16;
	uint8_t addr1[6]; /* receiver address */
	uint8_t addr2[6]; /* sender address */
	uint8_t addr3[6]; /* filtering address */
	unsigned sequence_ctrl:16;
	uint8_t addr4[6]; /* optional */
} wifi_ieee80211_mac_hdr_t;

typedef struct {
	wifi_ieee80211_mac_hdr_t hdr;
	uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;

static esp_err_t event_handler(void *ctx, system_event_t *event);
static void wifi_sniffer_init(void);
static void wifi_sniffer_set_channel(uint8_t channel);
static const char *wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type);
static void wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type);

esp_err_t event_handler(void *ctx, system_event_t *event)
{
  return ESP_OK;
}

void wifi_sniffer_init(void)
{
	nvs_flash_init();
	tcpip_adapter_init();
	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
	
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	wifi_config_t wifi_config = {
        .sta = {
            {.ssid = ESP_WIFI_SSID},
            {.password = ESP_WIFI_PASS},
        },
    };
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
	ESP_ERROR_CHECK( esp_wifi_start() );
	esp_wifi_set_promiscuous(true);
	esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
}

void wifi_sniffer_set_channel(uint8_t channel)
{
  	esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

const char * wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type)
{
	switch(type) {
	case WIFI_PKT_MGMT: return "MGMT";
	case WIFI_PKT_DATA: return "DATA";
	default:  
	case WIFI_PKT_MISC: return "MISC";
	}
}

void wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type)
{
  	if (type != WIFI_PKT_MGMT)
   		return;

  	const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
  	const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload; // wifi_pkt_rx_ctrl_t (https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_wifi.html#_CPPv418wifi_pkt_rx_ctrl_t)
  	const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;

	char packet[200];
	sprintf(packet,
		"PACKET TYPE=%s, CHAN=%02d, RSSI=%02d,"
		" ADDR1=%02x:%02x:%02x:%02x:%02x:%02x,"
		" ADDR2=%02x:%02x:%02x:%02x:%02x:%02x,"
		" ADDR3=%02x:%02x:%02x:%02x:%02x:%02x",
		wifi_sniffer_packet_type2str(type),
		ppkt->rx_ctrl.channel,
		ppkt->rx_ctrl.rssi,
		/* ADDR1 */
		hdr->addr1[0],hdr->addr1[1],hdr->addr1[2],
		hdr->addr1[3],hdr->addr1[4],hdr->addr1[5],
		/* ADDR2 */
		hdr->addr2[0],hdr->addr2[1],hdr->addr2[2],
		hdr->addr2[3],hdr->addr2[4],hdr->addr2[5],
		/* ADDR3 */
		hdr->addr3[0],hdr->addr3[1],hdr->addr3[2],
		hdr->addr3[3],hdr->addr3[4],hdr->addr3[5]
  	);
 	printf("%s\n", packet);
  	strcpy(packet, "");
}


// the setup function runs once when you press reset or power the board
void setup()
{
  	// initialize digital pin 5 as an output.
	Serial.begin(115200);
	delay(100);

	wifi_sniffer_init();

	esp_wifi_connect();
}

// the loop function runs over and over again forever
void loop()
{
	// Bezig met EEPROM https://randomnerdtutorials.com/arduino-eeprom-explained-remember-last-led-state/ en https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html
	//esp_wifi_disconnect();
	Serial.print("inside loop\n");
	delay(500); // wait for half a second
	
	vTaskDelay(WIFI_CHANNEL_SWITCH_INTERVAL / portTICK_PERIOD_MS);
	channel = (channel % WIFI_CHANNEL_MAX) + 1;
	wifi_sniffer_set_channel(channel);

	//Send an HTTP POST request every 10 minutes
	if ((millis() - lastTime) > timerDelay) {
		//Check WiFi connection status
		if(WiFi.status()== WL_CONNECTED){
			HTTPClient http;

			String serverPath = serverName + "?temperature=24.37";

			// Your Domain name with URL path or IP address with path
			http.begin(serverPath.c_str());

			// If you need Node-RED/server authentication, insert user and password below
			//http.setAuthorization("REPLACE_WITH_SERVER_USERNAME", "REPLACE_WITH_SERVER_PASSWORD");

			// Send HTTP GET request
			int httpResponseCode = http.GET();

			if (httpResponseCode>0) {
				Serial.print("HTTP Response code: ");
				Serial.println(httpResponseCode);
				String payload = http.getString();
				Serial.println(payload);
			}
			else {
				Serial.print("Error code: ");
				Serial.println(httpResponseCode);
			}
			// Free resources
			http.end();
		}
		else {
			Serial.println("WiFi Disconnected");
		}
		lastTime = millis();
	}
}
