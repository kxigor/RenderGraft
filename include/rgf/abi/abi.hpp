#pragma once

// clang-format off
#ifdef __i386__
    #define RGF_ENGINE_CALL         __attribute__((fastcall))
    #define RGF_CDECL               __attribute__((cdecl))
    #define RGF_STDCALL             __attribute__((stdcall))
    #define RGF_THISCALL            __attribute__((thiscall))
    #define RGF_VECTORCALL          __attribute__((vectorcall))
#else
    #define RGF_ENGINE_CALL
    #define RGF_CDECL
    #define RGF_STDCALL
    #define RGF_THISCALL
    #define RGF_VECTORCALL
#endif
// clang-format on
