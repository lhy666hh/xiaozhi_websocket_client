set(src_dirs
			jpeg)

set(include_dirs
			jpeg)

set(requires
            driver
			esp_timer
			fatfs
			sdmmc
			esp_codec_dev
			es8311
			lvgl)


idf_component_register(SRC_DIRS ${src_dirs}
					  INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
