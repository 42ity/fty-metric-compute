cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

project(fty-metric-compute
    VERSION 1.0.0
    DESCRIPTION "42ity computation services on METRICS"
)

##############################################################################################################
find_package(fty-cmake PATHS ${CMAKE_BINARY_DIR}/fty-cmake)
##############################################################################################################

etn_target(static ${PROJECT_NAME}-lib
    SOURCES
        src/cmstats.cc
        src/cmstats.h
        src/cmsteps.cc
        src/cmsteps.h
        src/fty_mc_server.cc
        src/fty_mc_server.h
    USES_PRIVATE
        czmq
        fty_common_logging
        fty_proto
        fty_shm
        mlm
    PRIVATE
)

##############################################################################################################

etn_target(exe ${PROJECT_NAME}
    SOURCES
        src/fty_metric_compute.cc
    USES_PRIVATE
        ${PROJECT_NAME}-lib
)

##############################################################################################################

etn_test_target(${PROJECT_NAME}-lib
    SOURCES
        tests/main.cpp
        tests/cmsteps.cpp
        tests/cmstats.cpp
        tests/mc_server.cpp
    PREPROCESSOR
        -DCATCH_CONFIG_FAST_COMPILE
    SUBDIR
        tests
)

##############################################################################################################

#install resources files
set(MC_SETTINGS_DIR "${CMAKE_INSTALL_LOCALSTATEDIR}/lib/fty/${PROJECT_NAME}")
set(MC_USER "bios")

# Agent config
configure_file("${PROJECT_SOURCE_DIR}/resources/fty-metric-compute.cfg.in" "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.cfg" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.cfg" DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/${PROJECT_NAME})

# systemd unit
configure_file("${PROJECT_SOURCE_DIR}/resources/fty-metric-compute.service.in" "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.service" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.service" DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)

# systemd tmpfiles
configure_file("${PROJECT_SOURCE_DIR}/resources/fty-metric-compute.conf.in" "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.conf" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.conf" DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/tmpfiles.d/)
