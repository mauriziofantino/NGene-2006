# libusb 1.0.30 (native Windows backend)

Source: https://github.com/libusb/libusb/tree/v1.0.30

The files in `libusb-1.0/` are an unchanged subset of upstream tag `v1.0.30`:
the core, Windows event/thread abstractions and Windows USB backends. The source
directory was renamed from `libusb/` so receiver includes retain the established
`libusb-1.0/libusb.h` spelling. `COPYING` contains the upstream LGPL license.

The local CMake adapter builds this subset statically using Visual Studio
clang-cl, Microsoft ABI and Windows SDK headers. It mirrors the Windows
configuration without optional hotplug support. The dependency is optimized
independently of the classic receiver's DSP preservation flags.

The sampler needs the public WinUSB RAW_IO APIs from version 1.0.30 to keep reads
queued. There is no separate libusb runtime DLL to install. Updating requires
reviewing upstream changes and repeating streaming and cancellation checks.
