# ParallelN64 (Parallel Launcher Edition)

A fork of [ParallelN64](https://git.libretro.com/libretro/parallel-n64) that adds the GLideN64 graphics plugin and some additional settings and features. Designed to be used with Parallel Launcher.

# Credits

The original ParallelN64 can be found here: https://git.libretro.com/libretro/parallel-n64  

Contributors for the modifications specific to this fork:
 * [Matt Pharoah](https://gitlab.com/mpharoah)
   * Black border fix for ParaLLEl
   * Config to enable/disable unaligned DMA support
   * 16:10 (SteamDeck) widescreen support for GLideN64
   * Support for IS Viewer logging
   * Support for changing the time on the RTC
   * Support for storing the RTC state in savestates
 * [Wiseguy](https://gitlab.com/Mr-Wiseguy)
    * Raw gamecube controller support
 * [Aglab2](https://gitlab.com/aglab2)
    * MoltenVK (MacOS) support for ParaLLEl
    * Black border fix for NTSC roms in GLideN64
    * GLideN64 modifications for supporting old SM64 romhacks
    * Support for detecting save types based on EverDrive headers
    * Support for ROMs larger than 64 MiB
    * Native ARM support for MacOS
 * [devwizard](https://gitlab.com/devwizard64)
    * Added support for SummerCart64 SD card emulation

### Platform Builds

Build scripts are found [here](https://gitlab.com/parallel-launcher/parallel-n64/-/tree/master/scripts).
