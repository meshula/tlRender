include(ExternalProject)

set(OTIO_ARGS
    ${TLR_EXTERNAL_ARGS}
    -DOTIO_PYTHON_INSTALL=${TLR_ENABLE_PYTHON}
    -DOTIO_PYTHON_INSTALL_DIR_INITIALIZED_TO_DEFAULT=TRUE)

ExternalProject_Add(
    OTIO
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/OTIO
    DEPENDS ${OTIO_DEPENDS}
    GIT_REPOSITORY https://github.com/PixarAnimationStudios/OpenTimelineIO.git
    CMAKE_ARGS ${OTIO_ARGS})

