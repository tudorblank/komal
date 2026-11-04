# The .komal file type for this user, without installing the app (Linux): its MIME type
# (application/x-komal) and the document icon (named komal-document: see CMakeLists.txt), under
# ~/.local/share, then the desktop's caches refreshed so file managers show .komal files with it.
# Run as a script by the targets register-file-type and unregister-file-type (CMakeLists.txt):
#   cmake -DACTION=register|unregister -DMIME_XML=<komal-mime.xml> -DICONS=<assets/icons> -P register-file-type.cmake
if(NOT DEFINED ENV{HOME})
    message(FATAL_ERROR "no home folder")
endif()
if(DEFINED ENV{XDG_DATA_HOME})
    set(data "$ENV{XDG_DATA_HOME}")
else()
    set(data "$ENV{HOME}/.local/share")
endif()
set(sizes 16 32 48 64 128 256 512)

if(ACTION STREQUAL "register")
    file(MAKE_DIRECTORY "${data}/mime/packages")
    file(COPY_FILE "${MIME_XML}" "${data}/mime/packages/komal.xml")
    foreach(size IN LISTS sizes)
        file(MAKE_DIRECTORY "${data}/icons/hicolor/${size}x${size}/mimetypes")
        file(COPY_FILE "${ICONS}/doc-icon-${size}.png" "${data}/icons/hicolor/${size}x${size}/mimetypes/komal-document.png")
        file(REMOVE "${data}/icons/hicolor/${size}x${size}/mimetypes/application-x-komal.png")   # the name it had first
    endforeach()
elseif(ACTION STREQUAL "unregister")
    file(REMOVE "${data}/mime/packages/komal.xml")
    foreach(size IN LISTS sizes)
        file(REMOVE "${data}/icons/hicolor/${size}x${size}/mimetypes/komal-document.png"
                    "${data}/icons/hicolor/${size}x${size}/mimetypes/application-x-komal.png")
    endforeach()
else()
    message(FATAL_ERROR "ACTION is register or unregister")
endif()

# The caches the desktop reads (each one only if it's there: GNOME, KDE, others).
find_program(UPDATE_MIME update-mime-database)
find_program(UPDATE_ICONS gtk-update-icon-cache)
find_program(KDE_CACHE NAMES kbuildsycoca6 kbuildsycoca5)
if(UPDATE_MIME)
    execute_process(COMMAND "${UPDATE_MIME}" "${data}/mime")
endif()
# A user icon folder's cache (another app may have made one) lists what it holds: a stale one hides
# new icons, so it's made again whenever there is one.
if(UPDATE_ICONS AND (EXISTS "${data}/icons/hicolor/index.theme" OR EXISTS "${data}/icons/hicolor/icon-theme.cache"))
    execute_process(COMMAND "${UPDATE_ICONS}" -f -t "${data}/icons/hicolor" ERROR_QUIET)
endif()
if(KDE_CACHE)
    execute_process(COMMAND "${KDE_CACHE}" OUTPUT_QUIET ERROR_QUIET)
endif()
message(STATUS ".komal file type ${ACTION}ed for this user (${data})")
