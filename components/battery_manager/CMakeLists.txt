set(REQ
	freertos
	esp32_s3_touch_amoled_2_16
	esp_driver_i2c
	xpowerslib
)

idf_component_register(
	SRCS
	"battery_manager.cpp"
	INCLUDE_DIRS
	"include"
	REQUIRES
	${REQ}
)
