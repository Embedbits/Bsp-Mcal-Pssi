# Template version: 0.0.1
# Sets the version of CMakeLists.txt file.
# Never, under any circumstances, change this line. It is used by
# the module generation script to identify the template version.

#======================== Library configuration ===============================#
# Publicly included directories to provide headers include
set( Pssi_PublicIncludeDirs

)


# Privately included directories to provide headers include
set( Pssi_PrivateIncludeDirs
    ${CMAKE_CURRENT_SOURCE_DIR}
)


# Source files list for current library generation
set( Pssi_SourceFiles
    Pssi.c
    Pssi_Reg.c
)


# List of public header files provided by the current library
set( Pssi_PublicHeaders
    Pssi_Types.h
    Pssi_Pins.h
    Pssi_Port.h
)


# List of dependent libraries that are publicly used by the current library
set( Pssi_PublicDependentLibs
    Ral_Lib
)


# List of dependent libraries that are privately used by the current library
set( Pssi_PrivateDependentLibs
    Rcc_Lib
    Nvic_Lib
    Gpio_Lib
    Gpdma_Lib
    Cache_Lib
)


#========================== Library generation ================================#
# Create library
add_library(Pssi_Lib STATIC ${Pssi_SourceFiles})


# Include necessary libraries to the target
target_link_libraries( Pssi_Lib
    PUBLIC
        ${Pssi_PublicDependentLibs}
    PRIVATE
        ${Pssi_PrivateDependentLibs}
)


# Set public headers directory path in build folder
set(PUBLIC_HEADERS_DIR ${CMAKE_CURRENT_BINARY_DIR}/PublicHeaders)

# Generates the public headers directory in build folder
file(MAKE_DIRECTORY ${PUBLIC_HEADERS_DIR})

# Process all public headers
foreach(PublicHeader IN LISTS Pssi_PublicHeaders)

    # Copy (or configure) only the public header there
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${PublicHeader}
        ${PUBLIC_HEADERS_DIR}/${PublicHeader}
        COPYONLY
    )

endforeach()


# Set include directories for the created target
target_include_directories( Pssi_Lib
    PUBLIC
        $<BUILD_INTERFACE:${PUBLIC_HEADERS_DIR}>
        $<INSTALL_INTERFACE:include>
        ${Pssi_PublicIncludeDirs}
    PRIVATE
        ${Pssi_PrivateIncludeDirs}
)


# Print status message
message(STATUS "Pssi_Lib library created")

#========================= Documentation configuration ========================#
if(DOXYGEN_AVAILABLE STREQUAL "ON")
    Doxygen_AddPath("${CMAKE_CURRENT_SOURCE_DIR}/")
endif()

#========================= Unit tests configuration ===========================#
if(UNIT_TESTING_AVAILABLE STREQUAL "ON")
    UnitTesting_AddPath("${CMAKE_CURRENT_SOURCE_DIR}/Tests/UnitTests")
endif()

#===================== Integration tests configuration ========================#
if(INTEGRATION_TESTING_AVAILABLE STREQUAL "ON")
    IntegrationTesting_AddPath("${CMAKE_CURRENT_SOURCE_DIR}/Tests/IntegrationTests")
endif()
