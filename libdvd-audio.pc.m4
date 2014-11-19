libdir=LIB_DIR
includedir=INCLUDE_DIR

Name: libdvd-audio
Description: DVD-Audio extraction library
Version: MAJOR_VERSION.MINOR_VERSION.RELEASE_VERSION
Libs: -L${libdir} -ldvd-audio -lm
CFlags: -I${includedir}
