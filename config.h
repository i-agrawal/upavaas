#if defined(linux) || defined(__linux) || defined(__linux__)
#define __linuxos__
#elif defined(macintosh) || (defined(__APPLE__) || defined(__MACH__)) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__bsdi__) || defined(__DragonFly__)
#define __bsdos__
#elif defined(unix) || defined(__unix) || defined(__unix__)
#define __unixos__
#else
#error "unsupported operating system"
#endif