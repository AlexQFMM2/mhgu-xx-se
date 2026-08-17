QT += core gui widgets

TEMPLATE = app
TARGET = MHGUSaveEditor
CONFIG += c++17

DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc

INCLUDEPATH += $$PWD/src

SOURCES += \
    src/main.cpp \
    src/game_data.cpp \
    src/mhgu_save.cpp \
    src/transfer_forms.cpp \
    src/main_window.cpp \
    src/editor_dialogs.cpp

HEADERS += \
    src/game_data.hpp \
    src/mhgu_save.hpp \
    src/transfer_forms.hpp \
    src/main_window.hpp \
    src/editor_dialogs.hpp
