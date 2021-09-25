# CMakeList.txt: netease-music-ncm-converter 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.8)

# Set vcpkg.cmake path here.
set(CMAKE_TOOLCHAIN_FILE "H:/cpp-pkg-man/vcpkg/scripts/buildsystems/vcpkg.cmake")
set(VCPKG_TARGET_TRIPLET x64-windows-static)

project ("netease-music-ncm-converter")

set(CMAKE_CXX_STANDARD 17)
set(target_name ncmdump-gui)

set(user_source  "src/ncmdump-gui.cpp" 
				  
				"src/ui.cxx" "src/ui.h" "src/logic.h" "src/logic.cpp")

file(GLOB vendor_source vendor/ncmdump-master/*.cpp)

# 将源代码添加到此项目的可执行文件。
add_executable (${target_name}  ${user_source} ${vendor_source})


if(MSVC)
	# static link
	string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
	string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
	string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
	# window subsystem
	set_target_properties(${target_name} PROPERTIES WIN32_EXECUTABLE TRUE)
endif(MSVC)

target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(${target_name} PRIVATE _CRT_SECURE_NO_WARNINGS)

# 配置fltk
find_package(FLTK CONFIG REQUIRED)
target_link_libraries(${target_name} PRIVATE fltk fltk_gl fltk_forms fltk_images)
target_include_directories(${target_name} PRIVATE ${FLTK_INCLUDE_DIRS})

# 配置taglib
find_path(taglib_inc_path taglib/mpegfile.h)
find_library(taglib_libs tag tag_c)
target_include_directories(${target_name} PRIVATE ${taglib_inc_path})
target_link_libraries(${target_name} PRIVATE ${taglib_libs})

# TODO: 如有需要，请添加测试并安装目标。
