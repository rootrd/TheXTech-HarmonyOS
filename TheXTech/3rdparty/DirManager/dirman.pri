INCLUDEPATH += $$PWD/include

win32:{
    SOURCES += $$PWD/src/dirman_winapi.cpp
} else {
    SOURCES += $$PWD/src/dirman_posix.cpp
}

SOURCES += \
    $$PWD/src/dirman.cpp

HEADERS += \
    $$PWD/include/DirManager/dirman.h \
    $$PWD/src/dirman_private.h
