#======================== Unit tests configuration ============================#
# Unit tests of Pssi module. Registered in module CMakeLists.txt by
# UnitTesting_AddPath() and created by UnitTesting_Generate() in "UnitTest"
# build (see EmBi_Platform/CMake/UnitTesting/README.md).

get_filename_component(PSSI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(MCAL_DIR "${PSSI_DIR}/.." ABSOLUTE)

UnitTesting_Add_Test(
    NAME            Pssi
    TEST_SOURCE     Test_Pssi.c
    UUT_SOURCES     ${PSSI_DIR}/Pssi.c
                    ${PSSI_DIR}/Pssi_Reg.c
    MOCK_HEADERS    ${MCAL_DIR}/Rcc/Rcc_Port.h
                    ${MCAL_DIR}/Nvic/Nvic_Port.h
                    ${MCAL_DIR}/Gpio/Gpio_Port.h
                    ${MCAL_DIR}/Gpdma/Gpdma_Port.h
                    ${MCAL_DIR}/Cache/Cache_Port.h
    INCLUDE_DIRS    ${PSSI_DIR}
                    ${MCAL_DIR}/Rcc
                    ${MCAL_DIR}/Nvic
                    ${MCAL_DIR}/Gpio
                    ${MCAL_DIR}/Gpdma
                    ${MCAL_DIR}/Cache
    LINK_LIBS       ut_RegMem                           # Registers in host memory
                    ut_CmsisHost                        # CMSIS core intrinsics on host
    FORCE_INCLUDES  ${UNIT_TESTING_CMSIS_HOST_HEADER}   # Replaces cmsis_gcc.h
    HOST_LIBS       Ral_Lib                             # ST LL compiled for host
    COVERAGE_EXCLUDE ".*/Ral/.*"
)
