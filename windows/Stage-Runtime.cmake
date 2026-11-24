cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
cmake_path(CONVERT "${UCRT}" TO_CMAKE_PATH_LIST UCRT NORMALIZE)

# The MSYS2 Qt DLLs use shared libraries from the UCRT64 toolchain.
# windeployqt selects Qt plugins, then CMake follows each PE import.
file(GLOB_RECURSE plugins "${STAGE}/*.dll")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${STAGE}/finily.exe"
    LIBRARIES ${plugins}
    DIRECTORIES "${UCRT}/bin"
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\]"
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    RESOLVED_DEPENDENCIES_VAR dependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolved
)
if(unresolved)
    message(FATAL_ERROR "Missing runtime DLLs: ${unresolved}")
endif()

# Plugins may resolve a Qt DLL from UCRT64 that windeployqt already staged.
foreach(name IN LISTS conflicts_FILENAMES)
    if(NOT EXISTS "${STAGE}/${name}")
        message(FATAL_ERROR "Conflicting runtime DLLs for ${name}: ${conflicts_${name}}")
    endif()
endforeach()

foreach(dependency IN LISTS dependencies)
    cmake_path(IS_PREFIX UCRT "${dependency}" NORMALIZE in_toolchain)
    if(in_toolchain)
        file(COPY "${dependency}" DESTINATION "${STAGE}")
    endif()
endforeach()
file(COPY "${UCRT}/share/licenses/" DESTINATION "${STAGE}/Licencias")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/Componentes.txt" DESTINATION "${STAGE}/Licencias")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/../LICENSE" DESTINATION "${STAGE}/Licencias")
