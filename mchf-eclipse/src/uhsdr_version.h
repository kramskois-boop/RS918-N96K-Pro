/*  -*-  mode: c; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4; coding: utf-8  -*-  */
/************************************************************************************
 **                                                                                 **
 **                                        UHSDR                                    **
 **               a powerful firmware for STM32 based SDR transceivers              **
 **                                                                                 **
 **---------------------------------------------------------------------------------**
 **                                                                                 **
 **  File name:                                                                     **
 **  Description:                                                                   **
 **  Last Modified:                                                                 **
 **  Licence:		GNU GPLv3                                                      **
 ************************************************************************************/

#define 	UHSDR_VER_MAJOR			"2"
#define 	UHSDR_VER_MINOR			"9"
#define 	UHSDR_VER_RELEASE		"48"

#define		UHSDR_BOOT_VERS			"4.1.2"


#ifdef SBLA									// Bootloader enables additional rf bands PCB
  #define	UHSDR_BOOT_VER			UHSDR_BOOT_VERS"a"
#elif SBLS									// Bootloader enables special debug capabilities
  #define	UHSDR_BOOT_VER			UHSDR_BOOT_VERS"s"
#else										// Standard Bootloader
  #define	UHSDR_BOOT_VER			UHSDR_BOOT_VERS
#endif

// N96K P0-6: product identification (audit §6.3). The numeric version macros above must stay 2.9.48:
// mchf-eclipse/Makefile parses UHSDR_VER_* from this file and the firmware version logic / config
// compatibility rely on them (ui_driver.c: fw_version_number_major/minor/release).
// UHSDR_PRODUCT_VERSION is the human visible product version, keep it in sync with UHSDR_VER_* above.
// UHSDR_VERSION below stays derived from it, so the fork is distinguishable from official 2.9.48.
#define     UHSDR_PRODUCT_NAME      "RS918 N96K-Pro"
#define     UHSDR_PRODUCT_VERSION   "2.9.48-n96kp1"
#define     UHSDR_PRODUCT_REPO      ""                             // no public product repository yet (local tree)

// trailing characters are needed for identifying version and building date+time in binary
#define	    UHSDR_VERSION           "fwv-"UHSDR_PRODUCT_VERSION
#define	    UHSDR_BUILD_DAT         "fwt-"__DATE__ " - " __TIME__
#define	    UHSDR_LICENCE           "GNU GPLv3"
#define     UHSDR_REPO              "github.com/df8oe/UHSDR"  // N96K P0-6: origin of this code base (upstream UHSDR, audit §6.3)
