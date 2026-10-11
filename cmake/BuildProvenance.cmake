function(lumina_resolve_build_provenance source_dir)
  set(revision "unknown")
  set(dirty "null")
  if(DEFINED ENV{LUMINA_SVG_BUILD_REVISION})
    set(revision "$ENV{LUMINA_SVG_BUILD_REVISION}")
    string(LENGTH "${revision}" revision_length)
    if(NOT revision_length EQUAL 40 OR NOT revision MATCHES "^[0-9a-fA-F]+$")
      message(FATAL_ERROR "LUMINA_SVG_BUILD_REVISION must be a full 40-character Git SHA")
    endif()
    if("$ENV{LUMINA_SVG_BUILD_DIRTY}" STREQUAL "0")
      set(dirty "false")
    elseif("$ENV{LUMINA_SVG_BUILD_DIRTY}" STREQUAL "1")
      set(dirty "true")
    else()
      message(FATAL_ERROR "LUMINA_SVG_BUILD_DIRTY must be 0 or 1 when a revision is declared")
    endif()
    string(TOLOWER "${revision}" revision)
  else()
    execute_process(
      COMMAND git -c "safe.directory=${source_dir}" rev-parse HEAD
      WORKING_DIRECTORY "${source_dir}"
      RESULT_VARIABLE revision_status
      OUTPUT_VARIABLE git_revision
      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(revision_status EQUAL 0)
      execute_process(
        COMMAND git -c "safe.directory=${source_dir}" status --porcelain
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE status_result
        OUTPUT_VARIABLE git_status
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if(status_result EQUAL 0)
        set(revision "${git_revision}")
        if(git_status STREQUAL "")
          set(dirty "false")
        else()
          set(dirty "true")
        endif()
      endif()
    endif()
  endif()
  set(LUMINA_SVG_SOURCE_REVISION "${revision}" PARENT_SCOPE)
  set(LUMINA_SVG_SOURCE_DIRTY "${dirty}" PARENT_SCOPE)
endfunction()
