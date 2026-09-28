# This file configures the "build" of this SKSE plugin
# - how to compile the C++ plugin (in addition to settings found in CMakePresets.json)
# - where to output the resulting .dll file

# It's recommended to set a minimum CMake version.
# If you use CMake features from higher versions, update this to match.
cmake_minimum_required(VERSION 3.21)

# [BEGIN set_project_name]
# Set your project name. This will be the name of your SKSE .dll file.
project(AutoSEQ VERSION 1.0.0 LANGUAGES CXX)

# [END set_project_name]

# #
# YOU DO NOT NEED TO EDIT ANYTHING BELOW HERE
# #

# If you're not using a mod manager, you probably want the SKSE plugin to go
# inside of your Skyrim "Data" folder.
#
# To do this automatically, set the `SKYRIM_FOLDER` environment variable
# to the path of your Skyrim Special Edition folder
if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()

# If you're using Mod Organizer 2 or Vortex, you might want this to go inside
# of your "mods" folder, inside of a subfolder named "<your mod>".
#
# To do this automatically, set the `SKYRIM_MODS_FOLDER` environment variable
# to the path of your "mods" folder
if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

# Otherwise, you can set OUTPUT_FOLDER to any place you'd like :)
# set(OUTPUT_FOLDER "C:/path/to/any/folder")

# Setup your SKSE plugin as an SKSE plugin!
find_package(CommonLibSSE CONFIG REQUIRED)
add_commonlibsse_plugin(${PROJECT_NAME} SOURCES plugin.cpp StalledQuests.cpp SEQAudit.cpp SEQFiles.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23) # <--- use C++23 standard
target_precompile_headers(${PROJECT_NAME} PRIVATE PCH.h) # <--- PCH.h is required!
target_compile_definitions(${PROJECT_NAME} PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING) # fmt 9.1.0 noise

# When your SKSE .dll is compiled, this will automatically copy the .dll into your mods folder.
# Only works if you configure DEPLOY_ROOT above (or set the SKYRIM_MODS_FOLDER environment variable)
if(DEFINED OUTPUT_FOLDER)
    # If you specify an <OUTPUT_FOLDER> (including via environment variables)
    # then we'll copy your mod files into Skyrim or a mod manager for you!

    # Copy the SKSE plugin .dll files into the SKSE/Plugins/ folder
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        # Empty ESL-flagged plugin that makes the game load AutoSEQ.bsa
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/data/AutoSEQ.esp" "${OUTPUT_FOLDER}/AutoSEQ.esp"
        # Placeholder archive so the DLL updates it in place inside the mod folder. Only copied when
        # missing: the DLL rewrites it with the actual fixes, and a build must not wipe those.
        COMMAND "${CMAKE_COMMAND}" -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/data/AutoSEQ.bsa -DDST=${OUTPUT_FOLDER}/AutoSEQ.bsa
                -P "${CMAKE_CURRENT_BINARY_DIR}/copy_if_missing.cmake"
        VERBATIM
    )
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/copy_if_missing.cmake"
        "if(NOT EXISTS \"\${DST}\")\n    file(COPY_FILE \"\${SRC}\" \"\${DST}\")\nendif()\n")

    # If you perform a "Debug" build, also copy .pdb file (for debug symbols)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endif()

# Release builds also lay the mod out in dist/<name>/ and zip it as dist/<name>-<version>.zip,
# ready to upload. (Debug builds need Visual Studio's debug runtime, so they aren't packaged.)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(DIST_STAGE "${CMAKE_CURRENT_SOURCE_DIR}/dist/${PROJECT_NAME}")
    set(DIST_ZIP "${CMAKE_CURRENT_SOURCE_DIR}/dist/${PROJECT_NAME}-${PROJECT_VERSION}.zip")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_STAGE}" "${DIST_ZIP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_STAGE}/SKSE/Plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${DIST_STAGE}/SKSE/Plugins/"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/data/AutoSEQ.esp"
                "${CMAKE_CURRENT_SOURCE_DIR}/data/AutoSEQ.bsa" "${DIST_STAGE}/"
        VERBATIM
    )
    # Separate command so the zip is made from inside the freshly created staging folder.
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${DIST_ZIP}" --format=zip AutoSEQ.esp AutoSEQ.bsa SKSE
        WORKING_DIRECTORY "${DIST_STAGE}"
        COMMENT "Packaging ${DIST_ZIP}"
        VERBATIM
    )
endif()
