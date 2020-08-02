#define ASYNC_TCP_SSL_ENABLED 0

#include <stdint.h>
#include <Arduino.h>

#define CAPTIVE_PORTAL
#include <EEPROM.h>
#include <WiFi.h>
#include <ESP32Ping.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"

#include "EPD.h"
#include "GUI_Paint.h"

#include "log2fix/log2fix.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))


/**
 * EPD
**/

#define GPIO_HANDSHAKE 2
#define GPIO_MOSI 12
#define GPIO_MISO 13
#define GPIO_SCLK 15
#define GPIO_CS 14

UBYTE BlackImage[100*480];

volatile uint8_t display_refresh_delay_ = 0;


/**
 * precision for fixed-point numbers (16.16 unsigned)
**/

#define PRECISION 16


/**
 * global state of the pings and motor angles
**/

volatile uint32_t ping_ms;	   // fixed point number
volatile uint32_t avg_ping_ms;   // fixed point number
volatile uint32_t m1_step;
volatile uint32_t m2_step;

volatile unsigned int target_phase_m1;
volatile unsigned int target_phase_m2;
const unsigned int m1_direction = 2;
const unsigned int m2_direction = 2;
const unsigned int phase_m1_offset = 7500ul;
const unsigned int phase_m2_offset = 164400ul;
const unsigned int stepsize_fast = 48;
const unsigned int stepsize_slow = 1;

const uint32_t small_section_angle = 7500ul;
const uint32_t large_section_angle = 113500ul;


/**
 * SPI bus
**/

#define SPI_CS_PIN  5
#define SPI_CLK_PIN 18
#define SPI_MOSI_PIN 23

spi_device_handle_t spi;


/**
 * wifi
**/

volatile int prev_wifi_status;

#define WIFI_AP_SSID "ping-clock"

String ssid = "";
String password = "";
String host_to_ping = "";

#define HTTP_PORT 80
#define MESSAGE_MAX_LEN 256

char messageBuf[MESSAGE_MAX_LEN];

const char HTTP_HEAD_HTML[] PROGMEM = "<!DOCTYPE HTML><html><head><meta name = \"viewport\" http-equiv=\"content-type\" content = \"width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0\"><title>ESP32 Demo</title>";
const char HTTP_STYLE[] PROGMEM= "<style>body { background-color: #0067B3  ; font-family: Arial, Helvetica, Sans-Serif; Color: #FFFFFF; }</style></head>";
const char HTTP_HEAD_STYLE[] PROGMEM= "<body><center><h1 style=\"color:#FFFFFF; font-family:verdana;font-family: verdana;padding-top: 10px;padding-bottom: 10px;font-size: 36px\">ESP32 Captive Portal</h1><h2 style=\"color:#FFFFFF;font-family: Verdana;font: caption;font-size: 27px;padding-top: 10px;padding-bottom: 10px;\">Give Your WiFi Credentials</h2>";
const char HTTP_FORM_START[] PROGMEM= "<FORM action=\"/\" method= \"post\">";
const char HTTP_CONTENT1_START[] PROGMEM= "<div style=\"padding-left:100px;text-align:left;display:inline-block;min-width:150px;\"><a href=\"#pass\" onclick=\"c(this)\" style=\"text-align:left\">{v}</a></div>&nbsp&nbsp&nbsp <div style=\"display:inline-block;min-width:260px;\"><span class=\"q\" style=\"text-align:right\">{r}%</span></div><br>";
const char HTTP_CONTENT2_START[] PROGMEM= "<P ><label style=\"font-family:Times New Roman\">SSID</label><br><input maxlength=\"30px\" id=\"ssid\" type=\"text\" name=\"ssid\" placeholder='Enter WiFi SSID' style=\"width: 400px; padding: 5px 10px ; margin: 8px 0; border : 2px solid #3498db; border-radius: 4px; box-sizing:border-box\" ><br></P>";
const char HTTP_CONTENT3_START[] PROGMEM= "<P><label style=\"font-family:Times New Roman\">PASSKEY</label><br><input maxlength=\"30px\" type = \"text\" id=\"pass\" name=\"passkey\"  placeholder = \"Enter WiFi PASSKEY\" style=\"width: 400px; padding: 5px 10px ; margin: 8px 0; border : 2px solid #3498db; border-radius: 4px; box-sizing:border-box\" ><br><P>";
const char HTTP_CONTENT4_START[] PROGMEM= "<input type=\"checkbox\" name=\"configure\" value=\"change\"> Change IP Settings </P>";
const char HTTP_CONTENT5_START[] PROGMEM= "<INPUT type=\"submit\">&nbsp&nbsp&nbsp&nbsp<INPUT type=\"reset\"><style>input[type=\"reset\"]{background-color: #3498DB; border: none; color: white; padding:  15px 48px; text-align: center; text-decoration: none;display: inline-block; font-size: 16px;}input[type=\"submit\"]{background-color: #3498DB; border: none; color: white; padding:  15px 48px;text-align: center; text-decoration: none;display: inline-block;font-size: 16px;}</style>";
const char HTTP_FORM_END[] PROGMEM= "</FORM>";
const char HTTP_SCRIPT[] PROGMEM= "<script>function c(l){document.getElementById('ssid').value=l.innerText||l.textContent;document.getElementById('pass').focus();}</script>";
const char HTTP_END[] PROGMEM= "</body></html>";

const char* messageStatic PROGMEM= "{\"staticSet\":\"staticValue\", \"staticIP\":\"%s\", \"staticGate\":\"%s\", \"staticSub\":\"%s\",\"ssidStatic\":\"%s\",\"staticPass\":\"%s\"}";
const char* messageDhcp PROGMEM= "{\"dhcpSet\":\"dhcpValue\",\"ssidDHCP\":\"%s\", \"passDHCP\":\"%s\"}";

const char HTTP_PAGE_STATIC[] PROGMEM = "<p>{s}<br>{g}<br>{n}<br></p>";
const char HTTP_PAGE_DHCP[] PROGMEM = "<p>{s}</p>";
const char HTTP_PAGE_WiFi[] PROGMEM = "<p>{s}<br>{p}</p>";
const char HTTP_PAGE_GOHOME[] PROGMEM = "<H2><a href=\"/\">go home</a></H2><br>";
const char HTTP_PAGE_HOME[] PROGMEM = "<H2>HELLO, WORLD</H2><br>";

DNSServer dnsServer;
AsyncWebServer server(HTTP_PORT);


/**
 * global state
**/

hw_timer_t * timer = NULL;


// ---------------------------------------------------------------------------


/**
 * EEPROM functions
**/

String read_string(int l, int p){
	String temp;
	for (int n = p; n < l+p; ++n) {
		if (char(EEPROM.read(n)) != '\0') {
			temp += String(char(EEPROM.read(n)));
		}
		else {
			n = l + p;
		}
	}
	return temp;
}

String read_prom_host_to_ping() {
	return read_string(30, 60);
}

void write_EEPROM(String s, int pos){
	for (int n = pos; n < s.length()+pos; ++n){
		EEPROM.write(n,s[n-pos]);
	}
}

void ROMwrite(String s, String p,String host_to_ping){
	write_EEPROM(s + '\0', 0);
	write_EEPROM(p + '\0', 30);
	write_EEPROM(host_to_ping + '\0', 60);
	EEPROM.commit();
}


/**
 * web server
**/

void handleNotFound(AsyncWebServerRequest *);
void handleStaticForm(AsyncWebServerRequest *);

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
	//request->addInterestingHeader("ANY");
	return true;
  }

  void handleRequest(AsyncWebServerRequest *request) {
	Serial.println("in handleRequest(), redirecting");
	request->redirect("http://" + WiFi.softAPIP().toString() + "/");
  }
};

void handleNotFound(AsyncWebServerRequest *request)
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += request->url();
  request->send_P(404, "text/plain", message.c_str());
}


void handleSubmitForm(AsyncWebServerRequest *request) {
	Serial.println("handleSubmitForm()");
	String response = FPSTR(HTTP_PAGE_WiFi);
	response.replace("{s}", request->arg("ssid"));
	response.replace("{p}", String(request->arg("passkey")));
	response+=FPSTR(HTTP_PAGE_GOHOME);
	request->send_P(200, "text/html", response.c_str());
	ROMwrite(String(request->arg("ssid")),
	         String(request->arg("host_to_ping")),
	         String(request->arg("passkey")));
}


void handleStaticForm(AsyncWebServerRequest *request) {
	Serial.println("Scanning for networks");

	if (request != NULL && request->hasArg("ssid") && request->hasArg("passkey")) {
		handleSubmitForm(request);
	}
	else {
		String json;
		int n = WiFi.scanComplete();
		if (n == -1) {
			json += "Scan in progress, please wait 30 seconds then reload this page!";
		}
		else if (n == -2) {
			// error during scan, try again!
			WiFi.scanDelete();
			WiFi.scanNetworks(true);
			json += "Scan in progress, please wait 30 seconds then reload this page!";
		}
		else if (n >= 0) {
			if (n > 0) {
				json += "<table>";
				json += "<tr>";
				json += "<td>rssi</td>";
				json += "<td>ssid</td>";
				json += "<td>bssid</td>";
				json += "<td>channel</td>";
				json += "<td>secure</td>";
				json += "</tr>";
				for (int i = 0; i < n; ++i) {
					json += "<tr>";
					json += "<td>" + String(WiFi.RSSI(i)) + "</td>";
					json += "<td><a href=\"/get?ssid=" + WiFi.SSID(i)+"\">\"" + WiFi.SSID(i) + "\"</a></td>";
					json += "<td>" + WiFi.BSSIDstr(i) + "</td>";
					json += "<td>" + String(WiFi.channel(i)) + "</td>";
					json += "<td>" + String(WiFi.encryptionType(i)) + "</td>";
					//json += ",\"hidden\":"+String(WiFi.isHidden(i)?"true":"false");
					json += "</tr>";
				}
				json += "</table>";
			}
			else {
				json += "No networks found!";
			}

			WiFi.scanDelete();
			WiFi.scanNetworks(true);
		}
		request->send_P(200, "text/html", json.c_str());
	}
}


void setupServer() {
	server.onNotFound(handleNotFound);
	server.on("/", HTTP_GET, handleStaticForm);

	server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
		if (request->hasParam("ssid") && request->hasParam("password")) {
			Serial.println("Connecting to ssid:");
			Serial.println(request->getParam("ssid")->value());
			String s("<h1>Connecting to network</h1>\n");
			s += "<center><h3>WiFi SSID</h3></center><br><input type=\"text\" name=\"ssid\" value=\"" + request->getParam("ssid")->value() + "\"><br>\n";
			request->send_P(200, "text/html", s.c_str());
			ssid = request->getParam("ssid")->value();
			password = request->getParam("password")->value();
			host_to_ping = request->getParam("host_to_ping")->value();
			WiFi.begin(ssid.c_str(), password.c_str());
			ROMwrite(ssid,
					password,
					host_to_ping);
		}
		else if (request->hasParam("ssid")) {
			Serial.println("Request password to ssid:");
			Serial.println(request->getParam("ssid")->value());
			String s("<h1>Connect to network</h1>\n");
			s += "<form method=\"get\" action=\"/get\">";
			s += "<center><h3>WiFi SSID</h3></center><br><input type=\"text\" name=\"ssid\" value=\"" + request->getParam("ssid")->value() + "\"><br>\n";
			s += "<center><h3>WiFi password</h3></center><br><input type=\"text\" name=\"password\" value=\"\"><br>";
			s += "<center><h3>Host to ping</h3></center><br><input type=\"text\" name=\"host_to_ping\" value=\"" + read_prom_host_to_ping() + "\"><br>";
			s += "<center><input type=\"submit\"></center>";
			s += "</form>";
			request->send_P(200, "text/html", s.c_str());
		}
	});
}


/**
 * SPI functions
**/

void IRAM_ATTR spi_send_cmds(spi_device_handle_t spi, const uint8_t n_bytes, const uint8_t cmd[])
{
	esp_err_t ret;
	static spi_transaction_t t;

	memset(&t, 0, sizeof(t));
	t.length = 8 * n_bytes;					 // word to send is 8 bits * n_bytes
	t.tx_buffer = cmd;
	t.user = (void*)0;			// sending data, not command

	ret = spi_device_polling_transmit(spi, &t);
	
	assert(ret == ESP_OK);
}


void spi_send_cmd(spi_device_handle_t spi, const uint8_t cmd)
{
	esp_err_t ret;
	static spi_transaction_t t;
	
	memset(&t, 0, sizeof(t));
	t.length = 8;					 // word to send is 8 bits
	t.tx_buffer = &cmd;
	t.user = (void*)0;				// sending data, not command

	ret = spi_device_polling_transmit(spi, &t);
	
	assert(ret == ESP_OK);
}


void init_spi() {
	spi_bus_config_t buscfg;

	//memset(&buscfg, 0, sizeof(spi_bus_config_t));

	buscfg.miso_io_num = -1;
	buscfg.mosi_io_num = SPI_MOSI_PIN;
	buscfg.sclk_io_num = SPI_CLK_PIN;
	buscfg.quadwp_io_num = -1;
	buscfg.quadhd_io_num = -1;
	buscfg.max_transfer_sz = 16;
	buscfg.flags = 0;
	buscfg.intr_flags = 0;

	spi_device_interface_config_t devcfg;
	memset(&devcfg, 0, sizeof(spi_device_interface_config_t));
	devcfg.clock_speed_hz = 1E6;		   // 1 MHz
	devcfg.mode = 0;
	devcfg.spics_io_num = SPI_CS_PIN;
	devcfg.queue_size = 7;
	devcfg.pre_cb = NULL;
	//devcfg.flags = SPI_DEVICE_TXBIT_LSBFIRST;

	esp_err_t ret;
	ret = spi_bus_initialize(VSPI_HOST, &buscfg, 0); // DMA disable
	ESP_ERROR_CHECK(ret);
	ret=spi_bus_add_device(VSPI_HOST, &devcfg, &spi);
	ESP_ERROR_CHECK(ret);
}


/**
 * ping-clock specific functions
**/

/*
float ping_to_angle(float ping_ms) {
	  const float small_section_angle = .082;
	  if (ping_ms < 1) {
			// compress the range 0.1..1
			ping_ms = fmax(.1, ping_ms);
			return small_section_angle * log10(ping_ms);
	  }
	  if (ping_ms > 1000) {
		  // compress the range 1000..10000
		  ping_ms = fmin(ping_ms, 10000);
		  return ping_to_angle(1000) + small_section_angle * (log10(ping_ms) - log10(1000));
	  }
	  return log10(ping_ms) / 3;
}*/


uint32_t IRAM_ATTR ping_to_angle(uint32_t ping_ms) {
	if (ping_ms < 1 << 16) {
		// compress the range 0.1..1
		ping_ms = MAX(6554ul, ping_ms);
		return (uint32_t)(((uint64_t)small_section_angle * (uint64_t)log10fix(ping_ms * 10, PRECISION)) >> PRECISION);
	}
	if (ping_ms > 1000 << 16) {
		// compress the range 1000..10000
		ping_ms = MIN(ping_ms, 10000 << PRECISION);
		return ping_to_angle(1000 << 16) + (small_section_angle * (log10fix(ping_ms, PRECISION) - log10fix(1000 << PRECISION, PRECISION)) >> 16);
	}
	return (((uint64_t)small_section_angle << 16)
	        + (((uint64_t)log10fix(ping_ms, PRECISION) * large_section_angle) >> 2)) >> 16;
}


void move_motors(uint32_t target_phase_m1, uint8_t stepsize_m1, uint8_t direction_m1,
				 uint32_t target_phase_m2, uint8_t stepsize_m2, uint8_t direction_m2,
				 spi_device_handle_t spi) {
	uint8_t word[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	word[0] = target_phase_m1 & 0x000000FF;
	word[1] = (target_phase_m1 & 0x0000FF00) >> 8;
	word[2] = (target_phase_m1 & 0x00FF0000) >> 16;
	word[3] = (target_phase_m1 & 0xFF000000) >> 24;
	word[4] = stepsize_m1;
	word[5] = direction_m1;

	word[6] = target_phase_m2 & 0x000000FF;
	word[7] = (target_phase_m2 & 0x0000FF00) >> 8;
	word[8] = (target_phase_m2 & 0x00FF0000) >> 16;
	word[9] = (target_phase_m2 & 0xFF000000) >> 24;
	word[10] = stepsize_m2;
	word[11] = direction_m2;

	for (uint8_t i = 0; i < 12; ++i) {
		spi_send_cmd(spi, word[i]);
	}
}


void IRAM_ATTR set_word(const uint32_t target_phase, const uint8_t stepsize, const uint8_t direction, uint8_t word[]) {
	(word)[0] = target_phase & 0x000000FF;
	(word)[1] = (target_phase & 0x0000FF00) >> 8;
	(word)[2] = (target_phase & 0x00FF0000) >> 16;
	(word)[3] = (target_phase & 0xFF000000) >> 24;
	(word)[4] = stepsize;
	(word)[5] = direction;
}


void IRAM_ATTR set_motors_according_to_ping() {
	// Serial.println("set_motors_according_to_ping");
	// Serial.print("\tping = ");
	// Serial.println(ping_ms >> 16);
	// Serial.print("\tping_avg = ");
	// Serial.println(avg_ping_ms >> 16);

	unsigned int target_phase_m1 = (phase_m1_offset + ping_to_angle(ping_ms)) % 171990;
	unsigned int target_phase_m2 = (phase_m2_offset + ping_to_angle(avg_ping_ms)) % 171990;

	uint8_t word[12];
	set_word(target_phase_m1, stepsize_fast, m1_direction, word + 6);
	set_word(target_phase_m2, m2_step, m2_direction, word);
	// Serial.print("\ttarget_phase_m1 = ");
	// Serial.println(target_phase_m1);
	// Serial.print("\ttarget_phase_m2 = ");
	// Serial.println(target_phase_m2);

	spi_send_cmds(spi, 12, word);
}


void IRAM_ATTR onTimer() {
	// update average
	// low-pass filtering
	const uint64_t tmp = 64880ul * (uint64_t)(avg_ping_ms) + 655ul * (uint64_t)(ping_ms);
	avg_ping_ms = (uint32_t)(tmp >> 16);

	// always track increasing ping fast
	const uint64_t tmp2 = 58982ul * (uint64_t)(MAX(ping_ms, avg_ping_ms)) + 6554ul * (uint64_t)(avg_ping_ms);
	avg_ping_ms = (uint32_t)(tmp2 >> 16);
	m2_step = stepsize_fast;

	set_motors_according_to_ping();
}


void draw_epd_nowait() {
	  UWORD Imagesize = 100*480;
	  Serial.println("Paint_NewImage");
	  for (size_t i = 0; i < Imagesize; ++i) {
		BlackImage[i] = 0xFF;
	  }
	  Paint_NewImage(BlackImage, EPD_7IN5_V2_WIDTH, EPD_7IN5_V2_HEIGHT, 0, WHITE);

	  {
		String s("wifi network");
		const size_t textwidth = Font24.Width * s.length();
		Paint_DrawString_EN(800/2 - textwidth/2, 295, s.c_str(), &Font24, WHITE, BLACK);
		Paint_DrawLine(20, 305, 800/2 - textwidth/2 - 10, 305, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
		Paint_DrawLine(800/2 + textwidth/2 + 10, 305, 800 - 20, 305, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
	  }

	  {
		String s("host to ping");
		const size_t textwidth = Font24.Width * s.length();
		Paint_DrawString_EN(800/2 - textwidth/2, 400, s.c_str(), &Font24, WHITE, BLACK);
		Paint_DrawLine(20, 410, 800/2 - textwidth/2 - 10, 410, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
		Paint_DrawLine(800/2 + textwidth/2 + 10, 410, 800 - 20, 410, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
	  }

	  if (WiFi.status() != WL_CONNECTED) {
		String s("Not connected");
		const size_t textwidth = Font48.Width * s.length();
		Paint_DrawString_EN(800/2 - textwidth/2, 230, s.c_str(), &Font48, BLACK, WHITE);
	  }
	  else {
		const size_t textwidth = Font48.Width * WiFi.SSID().length();
		Paint_DrawString_EN(800/2 - textwidth/2, 230, WiFi.SSID().c_str(), &Font48, WHITE, BLACK);
	  }

	  if (host_to_ping[0] == '\0') {
		String s("No host set");
		const size_t textwidth2 = Font48.Width * s.length();
		Paint_DrawString_EN(800/2 - textwidth2/2, 335, s.c_str(), &Font48, BLACK, WHITE);
	  }
	  else {
		const size_t textwidth2 = Font48.Width * host_to_ping.length();
		Paint_DrawString_EN(800/2 - textwidth2/2, 335, host_to_ping.c_str(), &Font48, WHITE, BLACK);
	  }

	  EPD_7IN5_V2_Display(BlackImage);
	  // EPD_7IN5_V2_TurnOnDisplay();   // redraw, wait for completion
	  EPD_SendCommand(0x12);			// redraw, do not wait for completion

}


void process_new_ping_sample(float ping_ms_raw) {
	Serial.print("Processing new ping sample: ");
	Serial.print(ping_ms_raw);
	Serial.println(" ms");
	ping_ms_raw = MAX(MIN(ping_ms_raw, 10000.), 1.);
	ping_ms = ping_ms_raw * (1 << 16);
	m2_step = stepsize_fast;
}


void loop() {
	Serial.println("loop");
	dnsServer.processNextRequest();

	delay(1000);

	if (WiFi.status() != WL_CONNECTED) {
		Serial.println("Wifi not connected");
		ping_ms = 10000 << 16;
		avg_ping_ms = 10000 << 16;
		return;
	}

	Serial.print("pinging host: ");
	Serial.println(host_to_ping.c_str());

	bool ret = Ping.ping(host_to_ping.c_str(), 1);	// 1 second timeout
	if (ret) {
		delay(0);
		const float ping_ms = Ping.averageTime();
		Serial.print("Result of ping: ");
		Serial.println(ping_ms);
		process_new_ping_sample(ping_ms);
	}
	else {
		// failure (timeout)
		Serial.println("ping timed out");
		if (avg_ping_ms > 0 && WiFi.status()) {
			process_new_ping_sample(avg_ping_ms / (float)(1 << 16) + 1000.);
		}
	}

	Serial.print("AP IP address: ");
	Serial.println(WiFi.softAPIP());

	if (WiFi.status() == WL_CONNECTED) {
		Serial.print("Connected to:\n");
		Serial.println(WiFi.localIP());
	}

	if (display_refresh_delay_ == 0 && prev_wifi_status != WiFi.status()) {
		draw_epd_nowait();
		prev_wifi_status = WiFi.status();
	}
	display_refresh_delay_ = (display_refresh_delay_ + 1) % 10;
}


void setup() {
	Serial.begin(115200);

	/**
	* init and draw "not connected"
	**/

	DEV_Module_Init();
	EPD_7IN5_V2_Init();
	EPD_7IN5_V2_Clear();
	DEV_Delay_ms(500);

	draw_epd_nowait();
	prev_wifi_status = WiFi.status();


	/**
	* startup delay waiting for the motors to finish calibrating
	**/

	delay(15000);

	display_refresh_delay_ = 0;


	/**
	* init EEPROM
	**/

	Serial.println("Setting up EEPROM");
	EEPROM.begin(512);


	/**
	* init wifi
	**/

	Serial.println("Initializing wifi AP mode");
	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(WIFI_AP_SSID);
	Serial.print("AP IP address: ");
	Serial.println(WiFi.softAPIP());
	Serial.println("Initializing web server");
	setupServer();
	Serial.println("Initializing DNS server");
	dnsServer.start(53, "*", WiFi.softAPIP());
	server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER); // only when requested from AP
	server.begin();


	/**
	* init timer and motor control
	**/

	init_spi();

	ping_ms = 10000 << 16;
	avg_ping_ms = 10000 << 16;

	timer = timerBegin(0, 240000, true); // 240 MHz / 240000 = 1/ms
	timerAttachInterrupt(timer, &onTimer, true);
	timerAlarmWrite(timer, 100, true); // timer trigger value; set auto-reload
	timerAlarmEnable(timer);


	/**
	* init wifi
	**/

	WiFi.scanNetworks(true); // async scan networks, this could take a while
}
