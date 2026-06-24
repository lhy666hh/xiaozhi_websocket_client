file(GLOB_RECURSE srcs
						"*.c"
						"guider_customer_fonts/*.c"
						"guider_fonts/*.c"
						"images/*.c"
)

set(include_dirs
				guider_customer_fonts
				guider_fonts
				images)

idf_component_register(
				SRCS ${srcs}
				INCLUDE_DIRS ${include_dirs}
				PRIV_REQUIRES lvgl
)
