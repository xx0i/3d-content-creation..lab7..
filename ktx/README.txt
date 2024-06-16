If you are adding this to a CMake Vulkan project:

In C++:

// must be defined if ktx libraries are built statically (the ones in this folder are)
#define KHRONOS_STATIC 
#include <ktxvulkan.h>

In CMake: (make note of the "YOUR_PROJECT_NAME_HERE")

# Add support for ktx texture loading
include_directories(${CMAKE_SOURCE_DIR}/ktx/include)

if (WIN32)
	# Find the libraries
	find_library(KTX_LIB_D NAMES ktx_x64_d PATHS ${CMAKE_SOURCE_DIR}/ktx/lib)
	find_library(KTX_LIB_R NAMES ktx_x64_r PATHS ${CMAKE_SOURCE_DIR}/ktx/lib)
	
	# Link the libraries
	target_link_libraries(YOUR_PROJECT_NAME_HERE debug ${KTX_LIB_D} optimized ${KTX_LIB_R})
endif(WIN32)

# TODO: other platforms here (only windows libraries were pre-compiled)





 