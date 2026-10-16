# Crash fix for GTA 5 Legacy (build 3411) on Windows 11 24H2+
> [!WARNING]  
> This will only work on Build 3411. 

If you're running an old build of GTA 5 Legacy you may have had the issue of the game crashing after a few minutes without a pop-up. This mod aims to fix the issue.

The code has been taken from [here](https://creativegeek.net/blog/fixing-gta-v-legacy-silent-crash-windows-11-24h2) and adjusted for build 3411 with the help of Claude. I do not have experience with debugging and such, so there are no guarantees to the solution I am providing here.

## Logging
The log 'GtaCrashFix3411.log' will be created when you start the game. An example of the log is shown belown:
```
[2026-10-16 19:03:47.421] === GtaCrashFix3411 loaded ===
[2026-10-16 19:03:47.422] pdata: rva=0x3067000 size=0x10B204 entries=91179; original last entry: begin=0x1A16A98 end=0x1A16BA7 unwind=0x1D65790
[2026-10-16 19:03:47.422] pdata: previous entry: begin=0x1A169EC end=0x1A16A95
[2026-10-16 19:03:47.422] Installed .pdata handler for GTA5+0x38895F6
[2026-10-16 19:07:01.439] Handled fault at GTA5+0x38895F6 (r8 was 0x620026, rdx=0x0, r9=0x4) count=1
```

## Build
A MinGW-w64 or MSVC compiler is required.

```g++ -shared -O2 -static -s GtaCrashFix3411.cpp -o GtaCrashFix3411.asi```

## Credits
This wouldn't be possible without the [research over on the Creative Geek blog](https://creativegeek.net/blog/fixing-gta-v-legacy-silent-crash-windows-11-24h2). 
