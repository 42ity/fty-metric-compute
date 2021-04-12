cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

project(fty-metric-compute
    VERSION 1.0.0
    DESCRIPTION "42ity computation services on METRICS"
)

##############################################################################################################
find_package(fty-cmake PATHS ${CMAKE_BINARY_DIR}/fty-cmake)
##############################################################################################################

etn_target(exe ${PROJECT_NAME}
    SOURCES
        src/cmstats.cc
        src/cmsteps.cc
        src/fty_metric_compute.cc
        src/fty_mc_server.cc
    INCLUDE_DIRS
        include
    USES_PRIVATE
        czmq
        fty_common_logging
        fty_proto
        fty_shm
        libmlm
)

#install resources files
set(MAL_SETTINGS_DIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/fty/${PROJECT_NAME}")
set(MAL_USER "bios")

configure_file("${PROJECT_SOURCE_DIR}/resources/fty-metric-compute.cfg.in" "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.cfg" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.cfg" DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/${PROJECT_NAME})

configure_file("${PROJECT_SOURCE_DIR}/resources/fty-metric-compute.service.in" "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.service" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/resources/fty-metric-compute.service" DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)

if (BUILD_TESTING)

    etn_test(${PROJECT_NAME}-test
        SOURCES
            tests/main.cc
            src/cmstats.cc
            src/cmsteps.cc
            src/fty_mc_server.cc
            src/fty_metric_compute_private_selftest.cc
        INCLUDE_DIRS
            include
        PREPROCESSOR -DCATCH_CONFIG_FAST_COMPILE
        USES
            czmq
            fty_common_logging
            fty_proto
            fty_shm
            libmlm
            Catch2::Catch2
            stdc++fs
            log4cplus
    )

    #copy selftest-ro for test
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/selftest-ro DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    file(MAKE_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/selftest-rw)

    #enable coverage
    etn_coverage(${PROJECT_NAME}-test)

    #copy selftest-ro for test coverage
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/selftest-ro DESTINATION ${CMAKE_BINARY_DIR})
    file(MAKE_DIRECTORY  ${CMAKE_BINARY_DIR}/selftest-rw)

endif()