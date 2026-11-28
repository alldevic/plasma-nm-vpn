# pnv_editor_metadata(<output.json> SERVICE <service> NAME <name> DESCRIPTION <text> NAME_RU <name>
#                     DESCRIPTION_RU <text>)
# The JSON metadata of an editor plugin. ki18n does not translate it, so the Russian strings come from here (7.1
# supplies the final texts). plasma-nm finds the editor by X-NetworkManager-Services.
set(PNV_EDITOR_TEMPLATE ${CMAKE_CURRENT_LIST_DIR}/pnv-editor.json.in)

function(pnv_json_string out value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(pnv_editor_metadata output)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "SERVICE;NAME;DESCRIPTION;NAME_RU;DESCRIPTION_RU" "")
    foreach(field SERVICE NAME DESCRIPTION NAME_RU DESCRIPTION_RU)
        if(NOT arg_${field})
            message(FATAL_ERROR "pnv_editor_metadata: ${field} is required")
        endif()
        pnv_json_string(PNV_${field} "${arg_${field}}")
    endforeach()
    if(NOT arg_SERVICE MATCHES "^org\\.freedesktop\\.NetworkManager\\.pnv\\.[a-z0-9]+$")
        message(FATAL_ERROR "pnv_editor_metadata: ${arg_SERVICE} is not org.freedesktop.NetworkManager.pnv.<protocol>")
    endif()
    set(PNV_VERSION ${PROJECT_VERSION})
    configure_file(${PNV_EDITOR_TEMPLATE} ${output} @ONLY)
endfunction()

# pnv_editor_install(<module>): into plasma-nm's VPN plugin directory, finding the widget library of
# plasma-nm-vpn-common by RUNPATH.
function(pnv_editor_install target)
    set_target_properties(${target} PROPERTIES INSTALL_RPATH ${KDE_INSTALL_FULL_LIBDIR}/plasma-nm-vpn)
    install(TARGETS ${target} DESTINATION ${KDE_INSTALL_PLUGINDIR}/plasma/network/vpn)
endfunction()
