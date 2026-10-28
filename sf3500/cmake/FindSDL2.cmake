# Hand-written FindSDL2 module for the SF3500 cross build.
# The device's staged SDL2 2.0.14 .so predates SDL2's own CMake config
# export, so find_package(SDL2) can't resolve via Config mode - this
# module provides the SDL2::SDL2 imported target by hand instead.

find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
find_library(SDL2_LIBRARY NAMES SDL2)

if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
	set(SDL2_FOUND TRUE)
	set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
	set(SDL2_LIBRARIES ${SDL2_LIBRARY})
	if(NOT TARGET SDL2::SDL2)
		add_library(SDL2::SDL2 UNKNOWN IMPORTED)
		set_target_properties(SDL2::SDL2 PROPERTIES
			IMPORTED_LOCATION "${SDL2_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
	endif()
	# SDL2main only does real work on Windows (WinMain -> main shim) - on
	# Linux a plain main() works fine with no library at all. Projects that
	# unconditionally link SDL2::SDL2main (chocolate-doom, etc.) still need
	# the target to exist; a no-op INTERFACE library satisfies the link
	# line without requiring us to build or stage anything real.
	if(NOT TARGET SDL2::SDL2main)
		add_library(SDL2::SDL2main INTERFACE IMPORTED)
	endif()
else()
	set(SDL2_FOUND FALSE)
	if(SDL2_FIND_REQUIRED)
		message(FATAL_ERROR "SDL2 not found (include=${SDL2_INCLUDE_DIR} lib=${SDL2_LIBRARY})")
	endif()
endif()
