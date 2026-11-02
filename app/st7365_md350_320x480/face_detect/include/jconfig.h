/* Minimal jconfig.h for cross-compiling against the board's
 * libjpeg-turbo (libjpeg.so.8, ABI version 80). Only what jpeglib.h
 * and this app need; the real library on the board provides the rest. */
#ifndef JCONFIG_H
#define JCONFIG_H

#define JPEG_LIB_VERSION 80
#define BITS_IN_JSAMPLE  8

#endif /* JCONFIG_H */
