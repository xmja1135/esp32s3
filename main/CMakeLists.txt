idf_component_register(
	SRCS
	"main.c"
	# "damping.c"
	# "button.c"
	INCLUDE_DIRS
	"."
	PRIV_REQUIRES
	esp32_s3_touch_amoled_2_16
	nvs_flash
	esp_new_jpeg
	mjpeg-player
	damping-manager
	button-manager
)
