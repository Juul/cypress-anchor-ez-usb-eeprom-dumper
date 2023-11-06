@echo off
REM #--------------------------------------------------------------------------
REM #	File:		BUILD.BAT
REM #	Contents:	Batch file to build Vend_Ax.
REM #
REM #	Copyright (c) 1997 AnchorChips, Inc. All rights reserved
REM #--------------------------------------------------------------------------

REM ### Compile code ###
c51 fw.c debug objectextend code small moddp2 DF(NO_RENUM)

REM ### Compile user peripheral code ###
REM ### Note: This code does not generate interrupt vectors ###
REM ### Note: periph.c  replaced by (modified into) Vend_Ax.c ###
c51 Vend_Ax.c db oe code small moddp2 noiv

REM ### Assemble descriptor table ###
a51 dscr.a51 errorprint debug

REM ### Link object code (includes debug info) ###
REM ### Note: XDATA and CODE must not overlap ###
REM ### Note: using a response file here for line longer than 128 chars
REM echo fw.obj, dscr.obj, Vend_Ax.obj, > tmp.rsp
echo fw.obj, dscr.obj, Vend_Ax.obj, > tmp.rsp
echo %CYUSB%\Target\Lib\Ezusb\USBJmpTb.obj,%CYUSB%\Target\Lib\Ezusb\EZUSB.lib  >> tmp.rsp
echo TO Vend_Ax RAMSIZE(256) CODE(80h) XDATA(0E00h)  >> tmp.rsp
bl51 @tmp.rsp

REM ### Generate intel hex image of binary (no debug info) ###
oh51 Vend_Ax HEXFILE(Vend_Ax.hex)

REM ### Generate BIX image of binary (no debug info) ###
%CYUSB%\Bin\hex2bix Vend_Ax.hex

REM ### usage: build -clean to remove intermediate files after build
if "%1" == "-clean" del tmp.rsp

if "%1" == "-clean" del *.lst
if "%1" == "-clean" del *.obj
if "%1" == "-clean" del *.m51

