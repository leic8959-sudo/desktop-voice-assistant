idf_component_register(SRCS "opus_capture_encoder.cpp" "opus_playback_decoder.cpp" INCLUDE_DIRS "." REQUIRES esp_audio_codec)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
