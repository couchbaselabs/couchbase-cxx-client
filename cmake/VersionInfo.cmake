find_program(GIT git)
if(GIT)
  execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE BACKEND_GIT_REVISION)
endif()

string(TIMESTAMP BACKEND_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)
configure_file(${PROJECT_SOURCE_DIR}/build_version.hxx.in ${PROJECT_BINARY_DIR}/generated/build_version.hxx @ONLY)
configure_file(${PROJECT_SOURCE_DIR}/build_config.hxx.in ${PROJECT_BINARY_DIR}/generated/build_config.hxx @ONLY)

file(
  GENERATE
  OUTPUT ${PROJECT_BINARY_DIR}/generated/build_info.hxx
  CONTENT
    "
#pragma once

#define OPENSSL_CRYPTO_LIBRARIES \"${OPENSSL_CRYPTO_LIBRARIES}\"
#define OPENSSL_SSL_LIBRARIES \"${OPENSSL_SSL_LIBRARIES}\"
#define OPENSSL_INCLUDE_DIR \"${OPENSSL_INCLUDE_DIR}\"

#define CMAKE_BUILD_TYPE \"${CMAKE_BUILD_TYPE}\"
")
