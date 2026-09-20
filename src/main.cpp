#include <Arduino.h>
#include <TFT_eSPI.h>
#include "driver/i2s.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WebSocketsClient.h>


#define I2S_LRC  14
#define I2S_DOUT 26
#define I2S_BCLK 27
#define I2S_DIN  33
#define AUDIO_BUFFER_SIZE 4000
#define AUDIO_BUFFER_POOL 25

const char* ssid = "Home";
const char* pass = "353Arm52@89";

char buf[50];
char playerBuf[50];

float volume = 0.2f;

typedef struct AudioFrame{
  uint8_t buffer[AUDIO_BUFFER_SIZE];
  size_t lenght;
}AudioFrame;

AudioFrame *frame[AUDIO_BUFFER_POOL];

WebSocketsClient webSocket;
TFT_eSPI tft = TFT_eSPI();

TaskHandle_t SocketTaskHandle;
TaskHandle_t AudioTaskHandle;

QueueHandle_t AudioQueue;
QueueHandle_t AudioBufferQueue;
QueueHandle_t FreeAudioBufferQueue;


portMUX_TYPE bufMux = portMUX_INITIALIZER_UNLOCKED;

void printNowPlaying(char* buffer){
  tft.setCursor(10,60);
  snprintf(playerBuf,sizeof(playerBuf),"Now playing: %s\n",buffer);
  tft.printf(playerBuf);

}

bool i2s_setup(uint32_t sampleRate){

  static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = (uint32_t)sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = (i2s_channel_fmt_t)I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 12,
    .dma_buf_len = 512,
    .use_apll = true,
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_DIN,
  };

  Serial.printf("Before I2S install: Free=%u Largest=%u\n", ESP.getFreeHeap(),ESP.getMaxAllocHeap());

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);

  if(err != ESP_OK){
    Serial.printf("ESP i2S Installation failed: %s\n", esp_err_to_name(err));
    return false;
  }

  err = i2s_set_pin(I2S_NUM_0,&pin_config);

  if(err != ESP_OK){
    Serial.printf("ESP i2S Pin Installation failed: %s\n", esp_err_to_name(err));
    return false;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  
  err = i2s_set_clk(I2S_NUM_0, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);

  if(err != ESP_OK){

    Serial.printf("I2S Config failed: %s\n", esp_err_to_name(err));
    return false;
  }


  Serial.println("I2S is configured");
  return true;

}

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length){

  switch(type){
    case WStype_DISCONNECTED:
      Serial.println("DISCONNECTED");
      break;
    case WStype_CONNECTED:
      Serial.println("CONNECTED");
      break;
    case WStype_TEXT:
      Serial.printf("Received: %s",payload);
      printNowPlaying((char*)payload);
      break;
    case WStype_BIN:
      //Serial.printf("Received binary data, length %u, free heap: %u\n",length,ESP.getFreeHeap());
      AudioFrame *newFrame; // Creating a pointer to AudioFrameStruct
      if((xQueueReceive(FreeAudioBufferQueue,&newFrame,0) == pdTRUE) && (length < AUDIO_BUFFER_SIZE)){ // Getting the address from queue with newFrame pointer
        newFrame->lenght = length;
        memcpy(newFrame->buffer,payload,length); // copying the data to newFrame pointer which contains the free buffer address which means address of corresponding frame array

        if(xQueueSend(AudioBufferQueue,&newFrame,0) != pdPASS){
          Serial.println("Audio dropping because queue is full");
        }
      }else{
        Serial.println("Packet too large");
       }     

      break;

  }
}

void AudioTask(void* pvParamter){
  AudioFrame *CurrentFrame;    
  size_t bytesWritten = 0;

  while(true){
   
    if(xQueueReceive(AudioBufferQueue,&CurrentFrame,portMAX_DELAY) == pdTRUE ){
      int16_t *samples = (int16_t*)CurrentFrame->buffer;
      int32_t size = CurrentFrame->lenght / sizeof(int16_t);


      for(int i = 0; i < size; i++){
        samples[i] = (int16_t)(samples[i] * volume);

        if(samples[i] > 32767) samples[i] = 32767;
        if(samples[i] < -32768) samples[i] = -32768;

      }
      i2s_write(I2S_NUM_0,(const char*)CurrentFrame->buffer,CurrentFrame->lenght,&bytesWritten, portMAX_DELAY);
      xQueueSend(FreeAudioBufferQueue,&CurrentFrame,portMAX_DELAY);
    }
    
  }
}

void SocketTask(void *pvParameter){

  while(true){
    webSocket.loop();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_GREEN);
  tft.setFreeFont(&FreeSerif9pt7b);
  tft.setCursor(10,21);
  snprintf(buf,sizeof(buf),"Connecting...\n");
  tft.printf(buf);
  tft.setCursor(10,40);

  WiFi.begin(ssid,pass);
  int i = 0;
  while(WiFi.status() != WL_CONNECTED){
   Serial.print(".");
   delay(500);
  }

  Serial.println("Connected");
  Serial.println(WiFi.localIP());

  snprintf(buf,sizeof(buf),"Connected %s\n",WiFi.localIP().toString().c_str());
  tft.printf(buf);

  bool setI2S = i2s_setup(44100);

  webSocket.begin("192.168.0.109",82,"/");
  webSocket.onEvent(webSocketEvent);
  //AudioQueue = xQueueCreate(10, sizeof(AudioFrame));
    Serial.printf("Before I2S install: Free=%u Largest=%u\n", ESP.getFreeHeap(),ESP.getMaxAllocHeap());

 

  AudioBufferQueue = xQueueCreate(AUDIO_BUFFER_POOL, sizeof(AudioFrame*)); // Create queue of address AudioFrame Struct
  FreeAudioBufferQueue = xQueueCreate(AUDIO_BUFFER_POOL, sizeof(AudioFrame*));

  for(int i = 0; i < AUDIO_BUFFER_POOL; i++){
    frame[i] = (AudioFrame*) heap_caps_malloc(sizeof(AudioFrame),MALLOC_CAP_8BIT);

    if(frame[i] == NULL){
      Serial.printf("Audio Frame Allocation failed: %d\n",i);
      return;
    }
    AudioFrame *p = frame[i]; // Getting address of that frame buffer and sending each of them to freeAudioBufferQueue
    xQueueSend(FreeAudioBufferQueue,&p,0);
  }
  Serial.printf("Free buffers: %u\n",  uxQueueMessagesWaiting(FreeAudioBufferQueue));
  // if(AudioQueue == NULL){
  //   Serial.println("Audio Queue allocation failed");
  //   return;
  // }
  BaseType_t ok1 = xTaskCreatePinnedToCore(SocketTask,"Socket Loop",16384,NULL,1,&SocketTaskHandle,0);
  BaseType_t ok2 = xTaskCreatePinnedToCore(AudioTask,"Audio Loop",16384,NULL,2,&AudioTaskHandle,1);

}

void loop() {
  // put your main code here, to run repeatedly:

 vTaskDelay(pdMS_TO_TICKS(2));
}


