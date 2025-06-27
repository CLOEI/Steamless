cmake_minimum_required(VERSION 3.20)
project(Steamless)

set(IMGUI_DIR thirdparty/imgui)
include_directories(${IMGUI_DIR})
include_directories(${IMGUI_DIR}/backends)

set(IMGUI_SOURCES 
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

find_package(OpenGL REQUIRED)

add_subdirectory(thirdparty/glfw)
include_directories(include)

add_executable(${PROJECT_NAME} main.cpp ${IMGUI_SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE glfw OpenGL::GL)