QT += core gui quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = morph_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/cli.h \
    ../src/converter.h \
    ../src/filepicker.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    morph_tests.cpp \
    ../src/backend.cpp \
    ../src/cli.cpp \
    ../src/converter.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc
