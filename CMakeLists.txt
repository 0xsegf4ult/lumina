cmake_minimum_required(VERSION 3.30.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

project(lumina VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_MODULE_STD 1)

# import std with clang requires linking against libc++
set(LUMINA_COMPILE_OPTIONS -Wno-deprecated-declarations -stdlib=libc++)

# ninja is required for cpp modules
# colorize ninja output
if(${CMAKE_GENERATOR} MATCHES "Ninja")
	set(LUMINA_COMPILE_OPTIONS ${LUMINA_COMPILE_OPTIONS} -fdiagnostics-color=always)
else()
	message(FATAL_ERROR "Generator ${CMAKE_GENERATOR} is not supported with C++ modules")
endif()

if(NOT LUMINA_PLATFORM)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		set(LUMINA_PLATFORM "LINUX")
		set(LUMINA_COMPILE_OPTIONS ${LUMINA_COMPILE_OPTIONS} -DLUMINA_PLATFORM_LINUX -DLUMINA_PLATFORM_POSIX)
		message(status "lumina::platform: linux")
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		set(LUMINA_PLATFORM "WIN32")
		set(LUMINA_COMPILE_OPTIONS ${LUMINA_COMPILE_OPTIONS} -DLUMINA_PLATFORM_WIN32)
		message(STATUS "lumina::platform: win32")
	else()
		message(FATAL_ERROR "Platform ${CMAKE_SYSTEM_NAME} not supported")
	endif()
endif()

add_subdirectory("external")

add_subdirectory("modules")
