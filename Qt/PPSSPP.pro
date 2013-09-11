TARGET = PPSSPPQt

QT += core gui opengl
CONFIG += mobility
MOBILITY += multimedia
win32: QT += multimedia
greaterThan(QT_MAJOR_VERSION,4): QT += widgets
VERSION = 0.9.1

include(Settings.pri)
mobile_platform: MOBILITY += sensors
symbian: MOBILITY += systeminfo

# Libs
symbian {
	LIBS += -lCore.lib -lCommon.lib -lNative.lib
	# For now you have to copy these to the Symbian lib dir using ffmpeg/symbian-install.sh
	LIBS += -lavformat.lib -lavcodec.lib -lavutil.lib -lswresample.lib -lswscale.lib
}
qnx: LIBS += -L. -lCore -lCommon -lNative -lscreen -lz
win32 {
	CONFIG(release, debug|release) {
		LIBS += -L$$OUT_PWD/release
	} else {
		LIBS += -L$$OUT_PWD/debug
	}
	LIBS += -lCore -lCommon -lNative -lwinmm -lws2_32
}
linux {
	LIBS += -L. -lCore -lCommon -lNative -ldl
	PRE_TARGETDEPS += ./libCommon.a ./libCore.a ./libNative.a
	!mobile_platform {
		CONFIG += link_pkgconfig
		packagesExist(sdl) {
			DEFINES += QT_HAS_SDL
			PKGCONFIG += sdl
		}
		FFMPEG_DIR=../ffmpeg/linux/x86_64/lib/
		LIBS += $${FFMPEG_DIR}libavformat.a $${FFMPEG_DIR}libavcodec.a $${FFMPEG_DIR}libavutil.a $${FFMPEG_DIR}libswresample.a $${FFMPEG_DIR}libswscale.a
	}
	# put this at the end avoids problems with some compilers
	LIBS += -lz
}

# Main
SOURCES += ../native/base/QtMain.cpp
HEADERS += ../native/base/QtMain.h

# UI
SOURCES += ../UI/*Screen.cpp \
	../UI/*Screens.cpp \
	../UI/GamepadEmu.cpp \
	../UI/GameInfoCache.cpp \
	../UI/OnScreenDisplay.cpp \
	../UI/UIShader.cpp \
	../android/jni/TestRunner.cpp

HEADERS += ../UI/*.h
INCLUDEPATH += .. ../Common ../native

# Temporarily only use new UI for Linux desktop
linux:!mobile_platform {
	MOC_DIR = moc
	UI_DIR = ui
	RCC_DIR = rcc
	SOURCES += *.cpp
	HEADERS += *.h
	FORMS += *.ui
	RESOURCES += resources.qrc
	INCLUDEPATH += ../Qt
} else {
	# Desktop handles the Init separately
	SOURCES += ../UI/NativeApp.cpp
}
symbian {
	RESOURCES += assets_lowmem.qrc
	SOURCES += ../UI/ui_atlas_lowmem.cpp
} else {
	RESOURCES += assets.qrc
	SOURCES += ../UI/ui_atlas.cpp
}

# Translations
TRANSLATIONS = $$files(languages/ppsspp_*.ts)

lang.name = lrelease ${QMAKE_FILE_IN}
lang.input = TRANSLATIONS
lang.output = ${QMAKE_FILE_PATH}/${QMAKE_FILE_BASE}.qm
lang.commands = $$[QT_INSTALL_BINS]/lrelease ${QMAKE_FILE_IN}
lang.CONFIG = no_link
QMAKE_EXTRA_COMPILERS += lang
PRE_TARGETDEPS += compiler_lang_make_all

# Packaging
symbian {
	# App UID:
	TARGET.UID3 = 0xE0095B1D

	# App Name:
	DEPLOYMENT.display_name = PPSSPP

	# App Vendor:
	vendor_deploy.pkg_prerules = "%{\"Qtness\"}" ":\"Qtness\""

	# App Icon:
	ICON = ../assets/icon.svg

	# Folders:
	assets.sources = ../flash0
	assets.path = E:/PPSSPP

	lang.sources = $$files(../lang/*.ini)
# Unsupported languages on Symbian.
!contains(MEEGO_EDITION,harmattan):contains(QMAKE_HOST.os, "Windows") {
	lang.sources -= ..\\lang/ja_JP.ini ..\\lang/ko_KR.ini ..\\lang/zh_CN.ini ..\\lang/zh_TW.ini
} else {
	lang.sources -= ../lang/ja_JP.ini ../lang/ko_KR.ini ../lang/zh_CN.ini ../lang/zh_TW.ini
}
	lang.path = E:/PPSSPP/lang

	DEPLOYMENT += vendor_deploy assets lang

	# 268 MB maximum
	TARGET.EPOCHEAPSIZE = 0x40000 0x10000000
	TARGET.EPOCSTACKSIZE = 0x10000
}

contains(MEEGO_EDITION,harmattan) {
	target.path = /opt/PPSSPP/bin
	assets.files = ../flash0
	assets.path = /opt/PPSSPP
	lang.files = $$files(../lang/*.ini)
	lang.path = /opt/PPSSPP/lang
	desktopfile.files = PPSSPP.desktop
	desktopfile.path = /usr/share/applications
	icon.files = ../assets/icon-114.png
	icon.path = /usr/share/icons/hicolor/114x114/apps
	INSTALLS += target assets lang desktopfile icon
	# Booster
	QMAKE_CXXFLAGS += -fPIC -fvisibility=hidden -fvisibility-inlines-hidden
	QMAKE_LFLAGS += -pie -rdynamic
	CONFIG += qt-boostable
}

