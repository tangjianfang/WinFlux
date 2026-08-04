# WinFlux compiler options — mirrors the nfui discipline:
# /W4 /permissive- /EHsc /utf-8 /FS /MP on every target.
function(flux_apply_compiler_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /EHsc /utf-8 /FS /MP /Zc:__cplusplus)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()
