
if(POLICY CMP0026)
  cmake_policy(SET CMP0026 OLD)
endif()

include(CMakeParseArguments)

#############################################################
function(get_resources_source_list target var)
  get_property(RESOURCE_LIST GLOBAL PROPERTY _${target}_RESOURCE_LIST)
  foreach(RF ${RESOURCE_LIST})
    string(REPLACE "|" ";" PARTS "${RF}")
    list(GET PARTS 0 SOURCE_FILE)
    list(APPEND _SF ${SOURCE_FILE})
  endforeach()
  set(${var} ${_SF} PARENT_SCOPE)
endfunction()

#############################################################
function(copy_resources target)
  if(XCODE)
    return()
  endif()

  get_property(RESOURCE_LIST GLOBAL PROPERTY _${target}_RESOURCE_LIST)

  # we read the LOCATION from the target instead of using a generator
  # here since add_custom_command doesn't support generator expresessions
  # in the output field, and this is still cleaner than hardcoding the path
  # of the output binary.
  #
  get_property(_LOC TARGET ${target} PROPERTY LOCATION)
  get_filename_component(TARGET_DIR ${_LOC} DIRECTORY)
  if(APPLE AND STANDARD_PMS)
    set(TARGET_LOC ${TARGET_DIR}/..)
  else()
    set(TARGET_LOC ${TARGET_DIR})
  endif()

  if(RESOURCE_LIST)
    foreach(RF ${RESOURCE_LIST})
      string(REPLACE "|" ";" PARTS "${RF}")
      list(GET PARTS 0 SOURCE_FILE)
      list(GET PARTS 1 _TARGET_FILE)
      set(TARGET_FILE ${TARGET_LOC}/${_TARGET_FILE})

      add_custom_command(OUTPUT ${TARGET_FILE}
                         COMMAND ${CMAKE_COMMAND} -E copy "${SOURCE_FILE}" "${TARGET_FILE}"
                         DEPENDS "${SOURCE_FILE}"
                         COMMENT "CopyResource (${target}): ${TARGET_FILE}")
      list(APPEND RESOURCES ${TARGET_FILE})
    endforeach()
    add_custom_target(${target}_CopyResources DEPENDS ${RESOURCES})
    add_dependencies(${target} ${target}_CopyResources)
  endif(RESOURCE_LIST)
endfunction()

#############################################################
function(add_resources)
  set(args1 TARGET)
  set(args2 SOURCES DEST EXCLUDE NAME)
  cmake_parse_arguments(BD "" "${args1}" "${args2}" ${ARGN})

  foreach(_BDFILE ${BD_SOURCES})
    if(IS_DIRECTORY ${_BDFILE})
      file(GLOB _DIRCONTENTS ${_BDFILE}/*)
      foreach(_BDDFILE ${_DIRCONTENTS})
        get_filename_component(_BDFILE_NAME ${_BDDFILE} NAME)

        set(PROCESS_FILE 1)
        foreach(EX_FILE ${BD_EXCLUDE})
          string(REGEX MATCH ${EX_FILE} DID_MATCH ${_BDDFILE})
          if(NOT "${DID_MATCH}" STREQUAL "")
            set(PROCESS_FILE 0)
          endif(NOT "${DID_MATCH}" STREQUAL "")
        endforeach(EX_FILE ${BD_EXCLUDE})

        if(PROCESS_FILE STREQUAL "1")
          if(IS_DIRECTORY ${_BDDFILE})
            set(DEST ${BD_DEST}/${_BDFILE_NAME})
          else()
            set(DEST ${BD_DEST})
          endif()

          add_resources(SOURCES ${_BDDFILE} DEST ${DEST} EXCLUDE ${BD_EXCLUDE} TARGET ${BD_TARGET})
        endif()
      endforeach()
    else()
      if(NOT BD_NAME)
        get_filename_component(BD_NAME ${_BDFILE} NAME)
      endif()
      set_property(GLOBAL APPEND PROPERTY _${BD_TARGET}_RESOURCE_LIST "${_BDFILE}|${BD_DEST}/${BD_NAME}")
      if(XCODE)
        set_source_files_properties(${_BDFILE} PROPERTIES MACOSX_PACKAGE_LOCATION ${BD_DEST})
      endif()
    endif()
  endforeach()
endfunction()

