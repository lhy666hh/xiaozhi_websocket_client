set(src_dirs
			UART
			EXIT
			KEY
			SD
			AUDIO_CODE
			LCD
			RGB)

set(include_dirs
			UART
			EXIT
			KEY
			SD
			AUDIO_CODE
			LCD
			RGB)

set(requires
            driver
			esp_timer
			fatfs
			sdmmc
			esp_codec_dev
			es8311
			lvgl
			esp_websocket_client)

#EMBED_FILES "canon.pcm"  
idf_component_register(SRC_DIRS ${src_dirs}
					  INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
