#ifndef _CSURF_H_
#define _CSURF_H_

#define REAPERAPI_IMPLEMENT
#include "../reaper_plugin_functions.h"

#include "../../WDL/db2val.h"
#include "../../WDL/wdlstring.h"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include "resource.h"

using namespace std;


#define CSURF_EXT_RESET 0x0001FFFF // clear all surface state and reset (harder reset than SetTrackListChange)
#define CSURF_EXT_SETINPUTMONITOR 0x00010001 // parm1=(MediaTrack*)track, parm2=(int*)recmonitor
#define CSURF_EXT_SETMETRONOME 0x00010002 // parm1=0 to disable metronome, !0 to enable
#define CSURF_EXT_SETAUTORECARM 0x00010003 // parm1=0 to disable autorecarm, !0 to enable
#define CSURF_EXT_SETRECMODE 0x00010004 // parm1=(int*)record mode: 0=autosplit and create takes, 1=replace (tape) mode
#define CSURF_EXT_SETSENDVOLUME 0x00010005 // parm1=(MediaTrack*)track, parm2=(int*)sendidx, parm3=(double*)volume
#define CSURF_EXT_SETSENDPAN 0x00010006 // parm1=(MediaTrack*)track, parm2=(int*)sendidx, parm3=(double*)pan
#define CSURF_EXT_SETFXENABLED 0x00010007 // parm1=(MediaTrack*)track, parm2=(int*)fxidx, parm3=0 if bypassed, !0 if enabled
#define CSURF_EXT_SETFXPARAM 0x00010008 // parm1=(MediaTrack*)track, parm2=(int*)(fxidx<<16|paramidx), parm3=(double*)normalized value
#define CSURF_EXT_SETLASTTOUCHEDFX 0x0001000A // parm1=(MediaTrack*)track, parm2=(int*)mediaitemidx (may be NULL), parm3=(int*)fxidx. all parms NULL=clear last touched FX
#define CSURF_EXT_SETFOCUSEDFX 0x0001000B // parm1=(MediaTrack*)track, parm2=(int*)mediaitemidx (may be NULL), parm3=(int*)fxidx. all parms NULL=clear focused FX
#define CSURF_EXT_SETLASTTOUCHEDTRACK 0x0001000C // parm1=(MediaTrack*)track
#define CSURF_EXT_SETMIXERSCROLL 0x0001000D // parm1=(MediaTrack*)track, leftmost track visible in the mixer
#define CSURF_EXT_SETBPMANDPLAYRATE 0x00010009 // parm1=*(double*)bpm (may be NULL), parm2=*(double*)playrate (may be NULL)
#define CSURF_EXT_SETPAN_EX 0x0001000E // parm1=(MediaTrack*)track, parm2=(double*)pan, parm3=(int*)mode 0=v1-3 balance, 3=v4+ balance, 5=stereo pan, 6=dual pan. for modes 5 and 6, (double*)pan points to an array of two doubles.  if a csurf supports CSURF_EXT_SETPAN_EX, it should ignore CSurf_SetSurfacePan.
#define CSURF_EXT_SETRECVVOLUME 0x00010010 // parm1=(MediaTrack*)track, parm2=(int*)recvidx, parm3=(double*)volume
#define CSURF_EXT_SETRECVPAN 0x00010011 // parm1=(MediaTrack*)track, parm2=(int*)recvidx, parm3=(double*)pan
#define CSURF_EXT_SETFXOPEN 0x00010012 // parm1=(MediaTrack*)track, parm2=(int*)fxidx, parm3=0 if UI closed, !0 if open

#endif