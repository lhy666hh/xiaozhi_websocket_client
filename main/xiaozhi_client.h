#ifndef __XIAOZHI_CLENT_H_
#define __XIAOZHI_CLENT_H_
#include "esp_websocket_client.h"
//void xiaozhi_client_start(void);
//void send_hello(esp_websocket_client_handle_t client);

//esp_err_t send_opus_audio(esp_websocket_client_handle_t client,const int16_t *pcm_data, size_t samples);
//esp_err_t send_opus_audio_frame(esp_websocket_client_handle_t client,const int16_t *pcm_frame, size_t samples);
//void delete_opus_encoder(void);
//esp_err_t init_opus_encoder(int sample_rate, int channels);

// void delete_opus_decoder(void);
// void init_opus_decoder(int sample_rate, int channels);
// void opus_binary_decorder_player(esp_websocket_event_data_t *data);
//void send_listen(esp_websocket_client_handle_t client, const char *text);

esp_err_t xiaozhi_client_config_and_start(void);
void xiaozhi_client_clear_and_stop(void);
bool xiaozhi_client_is_connected(void);
void xiaozhi_client_send_opuspcm_start(uint32_t rec_time);
void xiaozhi_client_send_opuspcm_stop(void);
bool xiaozhi_client_send_text(const char *text);
bool xiaozhi_client_recorder_running_status(void);
#endif