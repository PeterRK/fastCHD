#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(SHD_BUILDING_SHARED)
#    define SHD_API __declspec(dllexport)
#  elif defined(SHD_USING_SHARED)
#    define SHD_API __declspec(dllimport)
#  else
#    define SHD_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define SHD_API __attribute__((visibility("default")))
#else
#  define SHD_API
#endif
