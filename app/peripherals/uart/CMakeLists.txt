# app/peripherals/uart

file(GLOB COMMON "common/*.c")
file(GLOB DATA "data/*.c")
file(GLOB SOURCE "src/*.c")



target_sources(app PRIVATE
    ${COMMON}
    ${DATA}
    ${SOURCE}
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/data

)