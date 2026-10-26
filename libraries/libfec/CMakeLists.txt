# Reed-Solomon subset used by NGene. Upstream source and license are retained.
add_library(ngene_fec STATIC init_rs_char.c encode_rs_char.c decode_rs_char.c)
target_include_directories(ngene_fec PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
