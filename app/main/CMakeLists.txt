# app/main

file(GLOB SOURCE "src/*.c")
target_sources(app PRIVATE
    ${SOURCE}
    )

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)