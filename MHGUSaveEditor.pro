QT += core gui widgets sql

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
    src/editor_dialogs.cpp \
    src/loadout.cpp \
    src/loadout_search.cpp \
    src/loadout_format.cpp \
    src/auto_loadout_dialog.cpp

HEADERS += \
    src/game_data.hpp \
    src/mhgu_save.hpp \
    src/transfer_forms.hpp \
    src/main_window.hpp \
    src/editor_dialogs.hpp \
    src/loadout.hpp \
    src/loadout_search.hpp \
    src/loadout_format.hpp \
    src/auto_loadout_dialog.hpp

RESOURCES += $$PWD/resources/resources.qrc
