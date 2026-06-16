set(src_dirs
			jpeg
			lvgl_ui)

set(include_dirs
			jpeg
			lvgl_ui)

set(requires
            driver
			esp_timer
			fatfs
			sdmmc
			esp_codec_dev
			es8311
			lvgl
			BSP)


idf_component_register(SRC_DIRS ${src_dirs}
					  INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
