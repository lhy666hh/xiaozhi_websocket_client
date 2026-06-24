#include <string.h>
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "opus.h"
#include "audio_player.h"
#include "rgb.h"
static const char *TAG = "XIAOZHI_CLIENT";

#define WS_URI "ws://192.168.1.3:8000/xiaozhi/v1/"
#define DEVICE_ID "esp32-client"
// 帧参数：16kHz * 60ms = 960 样本/帧
#define OPUS_FRAME_SAMPLES 960


// MCP 工具回调函数类型：接收参数 JSON，返回结果 JSON（调用者负责释放）
typedef esp_err_t (*mcp_tool_callback_t)(cJSON *params, cJSON **result);


// 工具注册项（增加 inputSchema）
typedef struct {
    const char *name;
    const char *description;
    cJSON *input_schema;          // 参数 JSON Schema（由注册时创建）
    mcp_tool_callback_t callback;
} mcp_tool_t;

// 静态工具表（最大支持 5 个工具，可调整）
#define MAX_MCP_TOOLS 5
static mcp_tool_t s_mcp_tools[MAX_MCP_TOOLS];
static int s_mcp_tool_count = 0;




//static esp_websocket_client_handle_t s_client = NULL;
static OpusDecoder *s_decoder = NULL;
static int s_frame_size = 0;      // 每帧样本数
static int s_sample_rate = 16000; // 从服务器 hello 响应中获取
static int s_channels = 1;


// 注册一个 MCP 工具
esp_err_t mcp_register_tool(const char *name, const char *description, 
                            cJSON *input_schema, mcp_tool_callback_t callback)
{
    if (s_mcp_tool_count >= MAX_MCP_TOOLS) {
        ESP_LOGE(TAG, "MCP tool table full");
        return ESP_ERR_NO_MEM;
    }
    s_mcp_tools[s_mcp_tool_count].name = name;
    s_mcp_tools[s_mcp_tool_count].description = description;
    s_mcp_tools[s_mcp_tool_count].input_schema = input_schema; // 由调用者创建，生命周期由工具表管理
    s_mcp_tools[s_mcp_tool_count].callback = callback;
    s_mcp_tool_count++;
    ESP_LOGI(TAG, "MCP tool registered: %s", name);
    return ESP_OK;
}

// 根据名称查找工具（内部使用）
static mcp_tool_t *mcp_find_tool(const char *name)
{
    for (int i = 0; i < s_mcp_tool_count; i++) {
        if (strcmp(s_mcp_tools[i].name, name) == 0)
            return &s_mcp_tools[i];
    }
    return NULL;
}

// 示例工具：设置 LED 状态
static esp_err_t mcp_tool_led_set_state(cJSON *params, cJSON **result)
{
    // 1. 解析参数
    cJSON *gpio_json = cJSON_GetObjectItem(params, "gpio_num");
    cJSON *state_json = cJSON_GetObjectItem(params, "state");
    if (!gpio_json || !cJSON_IsNumber(gpio_json) ||
        !state_json || !cJSON_IsNumber(state_json)) {
        return ESP_ERR_INVALID_ARG;
    }

    int gpio_num = gpio_json->valueint;
    int state = state_json->valueint;

    // 2. 执行硬件操作（初始化 GPIO 可放在启动时）
    //gpio_set_level(gpio_num, state);
	if(state==0)ws2812_set_color(0,0,0);
	else ws2812_set_color(100,100,100);
    ESP_LOGI(TAG, "MCP: LED on GPIO%d set to %d", gpio_num, state);

    // 3. 构造成功结果（符合 MCP 规范：包含 content 和 isError）
    *result = cJSON_CreateObject();
    cJSON_AddItemToObject(*result, "content", cJSON_CreateArray()); // 无附加内容
    cJSON_AddBoolToObject(*result, "isError", false);
    return ESP_OK;
}

// 发送 MCP 错误响应
static void send_mcp_error(esp_websocket_client_handle_t client, cJSON *id, int code, const char *message)
{
    cJSON *error_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(error_obj, "code", code);
    cJSON_AddStringToObject(error_obj, "message", message);

    // 内层 payload
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "jsonrpc", "2.0");
    if (id) {
        cJSON_AddItemToObject(payload, "id", cJSON_Duplicate(id, 1));
    } else {
        cJSON_AddNullToObject(payload, "id");
    }
    cJSON_AddItemToObject(payload, "error", error_obj);

    // 外层
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "mcp");
    cJSON_AddItemToObject(response, "payload", payload);

    char *resp_str = cJSON_PrintUnformatted(response);
    if (resp_str) {
        esp_websocket_client_send_text(client, resp_str, strlen(resp_str), portMAX_DELAY);
        free(resp_str);
    }
    cJSON_Delete(response);
}
// static void send_mcp_error(esp_websocket_client_handle_t client, cJSON *id, int code, const char *message)
// {
//     cJSON *error_obj = cJSON_CreateObject();
//     cJSON_AddNumberToObject(error_obj, "code", code);
//     cJSON_AddStringToObject(error_obj, "message", message);

//     cJSON *response = cJSON_CreateObject();
//     cJSON_AddStringToObject(response, "type", "mcp");
//     if (id) {
//         cJSON_AddItemToObject(response, "id", cJSON_Duplicate(id, 1));
//     }
//     cJSON_AddItemToObject(response, "error", error_obj);

//     char *resp_str = cJSON_PrintUnformatted(response);
//     if (resp_str) {
//         esp_websocket_client_send_text(client, resp_str, strlen(resp_str), portMAX_DELAY);
//         free(resp_str);
//     }
//     cJSON_Delete(response);
// }


void init_mcp_tools(void)
{
	// 创建 inputSchema
	cJSON *schema = cJSON_CreateObject();
	cJSON *properties = cJSON_CreateObject();
	cJSON *gpio_prop = cJSON_CreateObject();
	cJSON_AddStringToObject(gpio_prop, "type", "integer");
	cJSON_AddStringToObject(gpio_prop, "description", "GPIO number");
	cJSON_AddItemToObject(properties, "gpio_num", gpio_prop);

	cJSON *state_prop = cJSON_CreateObject();
	cJSON_AddStringToObject(state_prop, "type", "integer");
	cJSON_AddStringToObject(state_prop, "description", "0 or 1");
	cJSON_AddItemToObject(properties, "state", state_prop);

	cJSON_AddItemToObject(schema, "properties", properties);
	cJSON_AddArrayToObject(schema, "required"); // 可选，可添加 required 列表

	mcp_register_tool("led_set_state", 
					"Set LED state", 
					schema,
					mcp_tool_led_set_state);
}



// 发送 hello 消息
static void send_hello(esp_websocket_client_handle_t client)
{
	if((client==NULL)||(esp_websocket_client_is_connected(client)==false))return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddStringToObject(root, "device_id", DEVICE_ID);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON_AddNumberToObject(root, "version", 1);

    cJSON *audio = cJSON_CreateObject();
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio, "channels", 1);
    cJSON_AddNumberToObject(audio, "frame_duration", 60);
    cJSON_AddStringToObject(audio, "format", "opus");
    cJSON_AddItemToObject(root, "audio_params", audio);

    cJSON *features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        ESP_LOGI(TAG, "Sending hello: %s", json_str);
        esp_websocket_client_send_text(client, json_str, strlen(json_str), portMAX_DELAY);
        free(json_str);
    }
    cJSON_Delete(root);
}

// 发送 listen 文本消息
static bool send_listen(esp_websocket_client_handle_t client, const char *text)
{
	if((client==NULL)||(esp_websocket_client_is_connected(client)==false))return false;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
	cJSON_AddStringToObject(root, "mode", "manual");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddStringToObject(root, "text", text);
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        ESP_LOGI(TAG, "Sending listen: %s", text);
        esp_websocket_client_send_text(client, json_str, strlen(json_str), portMAX_DELAY);
        free(json_str);
    }
    cJSON_Delete(root);
	return true;
}

void stop_tts_play(esp_websocket_client_handle_t client)
{
	if((client==NULL)||(esp_websocket_client_is_connected(client)==false))return;
	cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "abort");
    cJSON_AddStringToObject(root, "reason", "wake_word_detected");
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        ESP_LOGI(TAG, "Stop TTS Play");
        esp_websocket_client_send_text(client, json_str, strlen(json_str), portMAX_DELAY);
        free(json_str);
    }
    cJSON_Delete(root);
}

void start_opus_transmit(esp_websocket_client_handle_t client)
{
	if((client==NULL)||(esp_websocket_client_is_connected(client)==false))return;
	cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
	cJSON_AddStringToObject(root, "mode", "manual");
    cJSON_AddStringToObject(root, "state", "start");
	
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        ESP_LOGI(TAG, "start_opus_transmit");
        esp_websocket_client_send_text(client, json_str, strlen(json_str), portMAX_DELAY);
        free(json_str);
    }
    cJSON_Delete(root);
}

void stop_opus_transmit(esp_websocket_client_handle_t client)
{
	if((client==NULL)||(esp_websocket_client_is_connected(client)==false))return;
	cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
	cJSON_AddStringToObject(root, "mode", "manual");
    cJSON_AddStringToObject(root, "state", "stop");
	
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        ESP_LOGI(TAG, "stop_opus_transmit");
        esp_websocket_client_send_text(client, json_str, strlen(json_str), portMAX_DELAY);
        free(json_str);
    }
    cJSON_Delete(root);
}




static esp_err_t send_opus_audio_frame(esp_websocket_client_handle_t client,
                                   const int16_t *pcm_frame, size_t samples)
{
	int error;
    if (!client || !pcm_frame || samples != OPUS_FRAME_SAMPLES) {
        ESP_LOGE(TAG, "Invalid parameters: samples=%d, expected=%d", samples, OPUS_FRAME_SAMPLES);
        return ESP_FAIL;
    }
	if(esp_websocket_client_is_connected(client)==false)return ESP_FAIL;

    // Create encoder (48kHz, stereo)
    OpusEncoder *encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK) {
        ESP_LOGE(TAG, "Failed to create encoder: %s", opus_strerror(error));
        return ESP_FAIL;
    }

	// Set bitrate (e.g., 128 kbps)
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(128000));

	// Encode frame
    uint8_t opus_data[4000]; // Maximum packet size
    int bytes = opus_encode(encoder, pcm_frame, samples, opus_data, sizeof(opus_data));

	if (bytes < 0) {
        ESP_LOGE(TAG, "Encode error: %s", opus_strerror(bytes));
    } else {
        //ESP_LOGI(TAG, "Encoded %d bytes", bytes);
        //Transmit or store opus_data...
		//非阻塞发送，超时 100ms，失败则丢弃
        int ret = esp_websocket_client_send_bin(client, (const char*)opus_data,
                                                bytes, portMAX_DELAY);
        if (ret < 0) {
            ESP_LOGW(TAG, "WebSocket send failed, dropping frame");
        }
		else{
			//ESP_LOGI(TAG, "WebSocket send success");
		}
    }
    opus_encoder_destroy(encoder);
	return ESP_OK;
}


static void delete_opus_decoder(void) {
    if (s_decoder) {
        opus_decoder_destroy(s_decoder);
        s_decoder = NULL;
    }
}
// 初始化 Opus 解码器
static void init_opus_decoder(int sample_rate, int channels)
{
    delete_opus_decoder();
    int err;
    s_decoder = opus_decoder_create(sample_rate, channels, &err);
    if (err != OPUS_OK) {
        ESP_LOGE(TAG, "Opus decoder init failed: %s", opus_strerror(err));
        s_decoder = NULL;
    } else {
        s_sample_rate = sample_rate;
        s_channels = channels;
        // frame_size 根据帧时长 60ms 计算（与服务器协商的 frame_duration 一致）
        s_frame_size = sample_rate * 60 / 1000;
        ESP_LOGI(TAG, "Opus decoder ready: %dHz %dch, frame_size=%d", sample_rate, channels, s_frame_size);
    }
	
}


// 定义足够大的 PCM 缓冲区（支持 60ms @ 48kHz = 2880 样本）
#define MAX_PCM_SAMPLES 2880

// 静态分配的 PCM 缓冲区（避免每次栈分配，也避免重复 malloc）
static int16_t s_pcm_buffer[MAX_PCM_SAMPLES];

static void opus_binary_decorder_player(esp_websocket_event_data_t *data)
{
    if (!s_decoder) {
        ESP_LOGW(TAG, "Received binary data but decoder not ready");
        return;
    }

    // 校验输入数据长度（Opus 最小帧头为 1 字节，通常至少 5 字节）
    if (data->data_len < 1) {
        ESP_LOGW(TAG, "Opus packet too short: %d", data->data_len);
        return;
    }

    // 解码：使用最大缓冲区容量，实际输出帧数由 opus_decode 返回
    int frame_samples = opus_decode(s_decoder,
                                    (const unsigned char*)data->data_ptr,
                                    data->data_len,
                                    s_pcm_buffer,
                                    MAX_PCM_SAMPLES,  // 缓冲区容量（样本数）
                                    0);               // 不进行 FEC

    if (frame_samples > 0) {
        // 直接推送，不延时（由播放器内部队列处理背压）
        audio_player_push_pcm(s_pcm_buffer, frame_samples);
    } else if (frame_samples < 0) {
        ESP_LOGW(TAG, "Opus decode error: %s", opus_strerror(frame_samples));
        // 尝试重置解码器状态，避免后续数据持续解码失败
        opus_decoder_ctl(s_decoder, OPUS_RESET_STATE);
    }
}

#include "audio_recorder.h"
#include "myfont_ui.h"
static esp_websocket_client_handle_t client = NULL;


static esp_err_t user_opusdata_cb(const int16_t *pcm_frame, size_t samples)
{
	if(client)return send_opus_audio_frame(client,pcm_frame,samples);
	return ESP_FAIL;
}

static void user_jsontext_cb(esp_websocket_event_data_t *data,esp_websocket_client_handle_t s_client)
{
	// 文本 JSON
	cJSON *root = cJSON_Parse((char *)data->data_ptr);
	if (root == NULL) {
		ESP_LOGW(TAG, "Failed to parse JSON: %.*s", data->data_len, (char*)data->data_ptr);
		return;
	}
	cJSON *type = cJSON_GetObjectItem(root, "type");
	if (type && cJSON_IsString(type)) {
		if (strcmp(type->valuestring, "hello") == 0) {
			// 获取服务器音频参数
			cJSON *audio = cJSON_GetObjectItem(root, "audio_params");
			if (audio) {
				cJSON *rate_item = cJSON_GetObjectItem(audio, "sample_rate");
				cJSON *ch_item = cJSON_GetObjectItem(audio, "channels");
				if (rate_item && ch_item) {
					int rate = rate_item->valueint;
					int ch = ch_item->valueint;
					init_opus_decoder(rate, ch);
					//init_opus_encoder(rate, ch);
					audio_player_start_pcm_stream(rate,16,ch);
				}
			}
			// 握手完成，发送 listen 消息
			//send_listen(s_client, "你好，请介绍一下你自己");
			
		} else if (strcmp(type->valuestring, "tts") == 0) {
			cJSON *state = cJSON_GetObjectItem(root, "state");
			cJSON *text = cJSON_GetObjectItem(root, "text");
			if (state && cJSON_IsString(state) && text && cJSON_IsString(text)) {
				if (strcmp(state->valuestring, "sentence_start") == 0) {
					ESP_LOGI(TAG, "AI says: %s", text->valuestring);
					// 可在此处将文本显示到屏幕
					write_text_to_label(text->valuestring);
				}
			}
		} else if (strcmp(type->valuestring, "goodbye") == 0) {
			ESP_LOGI(TAG, "Server closed connection");
			esp_websocket_client_close(s_client, portMAX_DELAY);
		}else if (strcmp(type->valuestring, "stt") == 0) {
			cJSON *text = cJSON_GetObjectItem(root, "text");
			if (text && cJSON_IsString(text)) {
				
				ESP_LOGI(TAG, "USer says: %s", text->valuestring);
					
			}
		}
		// ---------- 新增 MCP 处理 ----------
        else if (strcmp(type->valuestring, "mcp") == 0) {
			// 取出 payload 字段
			cJSON *payload = cJSON_GetObjectItem(root, "payload");
			if (!payload || !cJSON_IsObject(payload)) {
				ESP_LOGW(TAG, "MCP message missing payload");
				cJSON_Delete(root);
				return;
			}

			// 检查是否为 JSON-RPC 2.0
			if (cJSON_GetObjectItem(payload, "jsonrpc") == NULL) {
				ESP_LOGW(TAG, "MCP payload missing jsonrpc");
				cJSON_Delete(root);
				return;
			}

			// 提取 id, method, params
			cJSON *id = cJSON_GetObjectItem(payload, "id");
			cJSON *method = cJSON_GetObjectItem(payload, "method");
			cJSON *params = cJSON_GetObjectItem(payload, "params");

			if (!method || !cJSON_IsString(method)) {
				send_mcp_error(s_client, id, -32600, "Invalid Request");
				cJSON_Delete(root);
				return;
			}

			const char *method_str = method->valuestring;

			// ----- initialize -----
			if (strcmp(method_str, "initialize") == 0) {
				ESP_LOGI(TAG, "Process MCP initialize");
				// 构造响应 result 对象
				cJSON *result = cJSON_CreateObject();
				cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
				cJSON *cap = cJSON_CreateObject();
				cJSON_AddItemToObject(cap, "tools", cJSON_CreateObject());
				cJSON_AddItemToObject(result, "capabilities", cap);
				cJSON *info = cJSON_CreateObject();
				cJSON_AddStringToObject(info, "name", "ESP32");
				cJSON_AddStringToObject(info, "version", "1.0.0");
				cJSON_AddItemToObject(result, "serverInfo", info);

				// 构造完整响应（外层 type + payload）
				cJSON *response = cJSON_CreateObject();
				cJSON_AddStringToObject(response, "type", "mcp");
				cJSON *payload_resp = cJSON_CreateObject();
				cJSON_AddStringToObject(payload_resp, "jsonrpc", "2.0");
				if (id) cJSON_AddItemToObject(payload_resp, "id", cJSON_Duplicate(id, 1));
				cJSON_AddItemToObject(payload_resp, "result", result);
				cJSON_AddItemToObject(response, "payload", payload_resp);

				char *resp_str = cJSON_PrintUnformatted(response);
				if (resp_str) {
					esp_websocket_client_send_text(s_client, resp_str, strlen(resp_str), portMAX_DELAY);
					free(resp_str);
				}
				cJSON_Delete(response);
				ESP_LOGI(TAG, "Sent MCP initialize response");
			}
			// ----- tools/list -----
			else if (strcmp(method_str, "tools/list") == 0) {
				ESP_LOGI(TAG, "Process MCP tools/list");
				cJSON *result = cJSON_CreateObject();
				cJSON *tools_array = cJSON_CreateArray();
				for (int i = 0; i < s_mcp_tool_count; i++) {
					cJSON *tool_obj = cJSON_CreateObject();
					cJSON_AddStringToObject(tool_obj, "name", s_mcp_tools[i].name);
					cJSON_AddStringToObject(tool_obj, "description", s_mcp_tools[i].description);
					if (s_mcp_tools[i].input_schema) {
						cJSON_AddItemToObject(tool_obj, "inputSchema", cJSON_Duplicate(s_mcp_tools[i].input_schema, 1));
					} else {
						cJSON_AddItemToObject(tool_obj, "inputSchema", cJSON_CreateObject());
					}
					cJSON_AddItemToArray(tools_array, tool_obj);
				}
				cJSON_AddItemToObject(result, "tools", tools_array);

				cJSON *response = cJSON_CreateObject();
				cJSON_AddStringToObject(response, "type", "mcp");
				cJSON *payload_resp = cJSON_CreateObject();
				cJSON_AddStringToObject(payload_resp, "jsonrpc", "2.0");
				if (id) cJSON_AddItemToObject(payload_resp, "id", cJSON_Duplicate(id, 1));
				cJSON_AddItemToObject(payload_resp, "result", result);
				cJSON_AddItemToObject(response, "payload", payload_resp);

				char *resp_str = cJSON_PrintUnformatted(response);
				if (resp_str) {
					esp_websocket_client_send_text(s_client, resp_str, strlen(resp_str), portMAX_DELAY);
					free(resp_str);
				}
				cJSON_Delete(response);
				ESP_LOGI(TAG, "Sent tools/list response with %d tools", s_mcp_tool_count);
			}
			// ----- tools/call -----
			else if (strcmp(method_str, "tools/call") == 0) {
				ESP_LOGI(TAG, "Process MCP tools/call");
				if (!params || !cJSON_IsObject(params)) {
					send_mcp_error(s_client, id, -32602, "Invalid params");
					cJSON_Delete(root);
					return;
				}
				cJSON *name_json = cJSON_GetObjectItem(params, "name");
				cJSON *args = cJSON_GetObjectItem(params, "arguments");
				if (!name_json || !cJSON_IsString(name_json)) {
					send_mcp_error(s_client, id, -32602, "Invalid tool name");
					cJSON_Delete(root);
					return;
				}
				const char *tool_name = name_json->valuestring;
				mcp_tool_t *tool = mcp_find_tool(tool_name);
				if (!tool) {
					send_mcp_error(s_client, id, -32601, "Tool not found");
					cJSON_Delete(root);
					return;
				}

				cJSON *result_json = NULL;
				esp_err_t err = tool->callback(args, &result_json);
				if (err != ESP_OK || result_json == NULL) {
					send_mcp_error(s_client, id, -32000, "Tool execution failed");
					cJSON_Delete(root);
					return;
				}

				// 构造响应
				cJSON *response = cJSON_CreateObject();
				cJSON_AddStringToObject(response, "type", "mcp");
				cJSON *payload_resp = cJSON_CreateObject();
				cJSON_AddStringToObject(payload_resp, "jsonrpc", "2.0");
				if (id) cJSON_AddItemToObject(payload_resp, "id", cJSON_Duplicate(id, 1));
				cJSON_AddItemToObject(payload_resp, "result", result_json);
				cJSON_AddItemToObject(response, "payload", payload_resp);

				char *resp_str = cJSON_PrintUnformatted(response);
				if (resp_str) {
					esp_websocket_client_send_text(s_client, resp_str, strlen(resp_str), portMAX_DELAY);
					free(resp_str);
				}
				cJSON_Delete(response);
			}
			else {
				// 其他方法
				send_mcp_error(s_client, id, -32601, "Method not found");
			}
		}
        
	}
	cJSON_Delete(root);
}

// 事件处理回调函数
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
			ESP_LOGI(TAG, "WebSocket connected");
            if(client)send_hello(client);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            break;
        case WEBSOCKET_EVENT_DATA:
        //     ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
             //printf("Received: %.*s\n", data->data_len, (char *)data->data_ptr);

			// 区分数据是文本还是二进制
            if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
                // 文本数据：注意 data->data_ptr 并非以 '\0' 结尾，必须按长度打印或复制
                ESP_LOGI(TAG, "Received TEXT, len=%d", data->data_len);
				user_jsontext_cb(data,client);
                // 安全打印（限制长度避免刷屏）
                int print_len = (data->data_len > 256) ? 256 : data->data_len;
                printf("Text: %.*s\n", print_len, (char *)data->data_ptr);
            } else if (data->op_code == WS_TRANSPORT_OPCODES_BINARY) {
                //ESP_LOGI(TAG, "Received BINARY, len=%d", data->data_len);
				opus_binary_decorder_player(data);
                // 二进制数据处理示例：保存到缓冲区或解析
                // memcpy(user_buf, data->data_ptr, data->data_len);
            } else {
                // 其他 op_code（如延续帧），一般由库自动处理，可忽略
                //ESP_LOGW(TAG, "Received unexpected op_code: %d", data->op_code);
            }
            break;
		case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGI(TAG, "WebSocket closed");
			delete_opus_decoder();
			//delete_opus_encoder();
			
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
            break;
    }
}


// 启动客户端
esp_err_t xiaozhi_client_config_and_start(void)
{
	if(client == NULL)
	{
		// 1. 配置WebSocket客户端
		esp_websocket_client_config_t websocket_cfg = {
		.uri = "ws://192.168.1.3:8000/xiaozhi/v1/",   // 你的服务器地址
		.headers = "Device-Id: test-device-002\r\n",  // 关键：自定义 Header，必须以 \r\n 结尾
		.disable_auto_reconnect = true,   // 可选：允许自动重连
		.keep_alive_enable = true,
		// .keep_alive_interval = 10,
		.network_timeout_ms = 10000,
		.ping_interval_sec = 10,           // 可选：每10秒发ping保活
		//.ping_timeout_sec = 5,             // 可选：ping超时时间
		.buffer_size = 4096,
		};

		// 2. 初始化客户端
		client = esp_websocket_client_init(&websocket_cfg);
		if (!client) {
			ESP_LOGE(TAG, "Failed to init WebSocket client");
			return ESP_FAIL;
    	}
		esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
	}

    // 3. 启动连接
    return esp_websocket_client_start(client);
}

void xiaozhi_client_clear_and_stop(void)
{
	if(client)
	{
		esp_websocket_client_stop(client);
		esp_websocket_client_destroy(client);
		client = NULL;
	}
}


bool xiaozhi_client_is_connected(void)
{
	if(!client || esp_websocket_client_is_connected(client)==false)return false;
	return true;
}

// 定义事件组
static EventGroupHandle_t s_event_group;
#define RECORDER_DONE   (1 << 0)


static void recorder_xs_task(void *arg) {
	uint32_t para = (uint32_t)arg;
	ESP_LOGI(TAG, "recorder_xs_task run");
	start_opus_transmit(client);
    record_pcm_to_queue(para,16000,16,1,user_opusdata_cb);
	stop_opus_transmit(client);
	ESP_LOGI(TAG, "recorder_xs_task delete");
	// 通知完成
    xEventGroupSetBits(s_event_group, RECORDER_DONE);
	vTaskDelete(NULL);
}


void xiaozhi_client_send_opuspcm_start(uint32_t rec_time)
{
	//start_opus_transmit(client);
	//vTaskDelay(pdMS_TO_TICKS(100));
	//record_pcm_to_queue(4,16000,16,1,user_opusdata_cb);

	if(!s_event_group)
	{
		s_event_group = xEventGroupCreate();
		xEventGroupSetBits(s_event_group, RECORDER_DONE);
	}

	if(rec_time==0)rec_time = 1;
	if(recorder_running_status()==false)
	{
		// 等待上一次录音任务完成
    	xEventGroupWaitBits(s_event_group, RECORDER_DONE, pdTRUE, pdFALSE, portMAX_DELAY);
		xTaskCreate(recorder_xs_task, "recoeder_task", 4096, (void *)rec_time, 6, NULL);
	}
}

bool xiaozhi_client_recorder_running_status(void)
{
	return recorder_running_status();
}

void xiaozhi_client_send_opuspcm_stop(void)
{
	if(recorder_running_status()==true)
	{
		recorder_frame_stop();
		
	}

	// vTaskDelay(pdMS_TO_TICKS(100));
	// stop_opus_transmit(client);
}


bool xiaozhi_client_send_text(const char *text)
{
	return send_listen(client,text);
}

void xiaozhi_client_stop_tts(void)
{
	stop_tts_play(client);
}
