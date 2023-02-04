#pragma once

//#define NOGDICAPMASKS     // CC_*, LC_*, PC_*, CP_*, TC_*, RC_
//#define NOVIRTUALKEYCODES // VK_*
//#define NOWINMESSAGES     // WM_*, EM_*, LB_*, CB_*
//#define NOWINSTYLES       // WS_*, CS_*, ES_*, LBS_*, SBS_*, CBS_*
//#define NOSYSMETRICS      // SM_*
#define NOMENUS           // MF_*
//#define NOICONS           // IDI_*
#define NOKEYSTATES       // MK_*
//#define NOSYSCOMMANDS     // SC_*
#define NORASTEROPS       // Binary and Tertiary raster ops
//#define NOSHOWWINDOW      // SW_*
#define OEMRESOURCE       // OEM Resource values
#define NOATOM            // Atom Manager routines
//#define NOCLIPBOARD       // Clipboard routines
#define NOCOLOR           // Screen colors
//#define NOCTLMGR          // Control and Dialog routines
#define NODRAWTEXT        // DrawText() and DT_*
//#define NOGDI             // All GDI defines and routines
#define NOKERNEL          // All KERNEL defines and routines
//#define NOUSER            // All USER defines and routines
//#define NONLS             // All NLS defines and routines
//#define NOMB              // MB_* and MessageBox()
#define NOMEMMGR          // GMEM_*, LMEM_*, GHND, LHND, associated routines
#define NOMETAFILE        // typedef METAFILEPICT
#define NOMINMAX          // Macros min(a,b) and max(a,b)
//#define NOMSG             // typedef MSG and associated routines
#define NOOPENFILE        // OpenFile(), OemToAnsi, AnsiToOem, and OF_*
#define NOSCROLL          // SB_* and scrolling routines
#define NOSERVICE         // All Service Controller routines, SERVICE_ equates, etc.
#define NOSOUND           // Sound driver routines
#define NOTEXTMETRIC      // typedef TEXTMETRIC and associated routines
#define NOWH              // SetWindowsHook and WH_*
#define NOWINOFFSETS      // GWL_*, GCL_*, associated routines
#define NOCOMM            // COMM driver routines
#define NOKANJI           // Kanji support stuff.
#define NOHELP            // Help engine interface.
#define NOPROFILER        // Profiler interface.
#define NODEFERWINDOWPOS  // DeferWindowPos routines
#define NOMCX             // Modem Configuration Extensions

#define WIN32_MEAN_AND_LEAN
#define VC_EXTRALEAN

#include "../Core/Base.h"

#include <winsock2.h>
#include <windows.h>

#undef INFO
#undef WARNING
#undef DEBUG
#undef ERROR
#undef FATAL
#undef TRACE
#undef VERBOSE
// Undef existing
#undef CINFO
#undef CWARNING
#undef CDEBUG
#undef CFATAL
#undef CERROR
#undef CTRACE
#undef CVERBOSE
#undef CINFO_IF
#undef CWARNING_IF
#undef CDEBUG_IF
#undef CERROR_IF
#undef CFATAL_IF
#undef CTRACE_IF
#undef CVERBOSE_IF
#undef CINFO_EVERY_N
#undef CWARNING_EVERY_N
#undef CDEBUG_EVERY_N
#undef CERROR_EVERY_N
#undef CFATAL_EVERY_N
#undef CTRACE_EVERY_N
#undef CVERBOSE_EVERY_N
#undef CINFO_AFTER_N
#undef CWARNING_AFTER_N
#undef CDEBUG_AFTER_N
#undef CERROR_AFTER_N
#undef CFATAL_AFTER_N
#undef CTRACE_AFTER_N
#undef CVERBOSE_AFTER_N
#undef CINFO_N_TIMES
#undef CWARNING_N_TIMES
#undef CDEBUG_N_TIMES
#undef CERROR_N_TIMES
#undef CFATAL_N_TIMES
#undef CTRACE_N_TIMES
#undef CVERBOSE_N_TIMES