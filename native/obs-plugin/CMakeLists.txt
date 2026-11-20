cmake_minimum_required(VERSION 3.21)
project(camlayer-obs VERSION 0.1.0 LANGUAGES C)

find_package(libobs CONFIG REQUIRED)

add_library(camlayer-obs MODULE src/plugin-main.c)
target_compile_features(camlayer-obs PRIVATE c_std_11)
target_link_libraries(camlayer-obs PRIVATE OBS::libobs)

set_target_properties(camlayer-obs PROPERTIES PREFIX "" OUTPUT_NAME "camlayer-obs")

install(TARGETS camlayer-obs
  RUNTIME DESTINATION obs-plugins/64bit
  LIBRARY DESTINATION obs-plugins/64bit)
install(DIRECTORY data/ DESTINATION data/obs-plugins/camlayer-obs)
