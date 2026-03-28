add_library(kissfft STATIC kiss_fft.c kiss_fftr.c)
target_include_directories(kissfft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(kissfft PUBLIC kiss_fft_scalar=float)
