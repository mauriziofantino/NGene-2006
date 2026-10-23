# libusb 1.0.30 (Windows backend)

Source: https://github.com/libusb/libusb/tree/v1.0.30

The files in `libusb-1.0/` are an unchanged subset of upstream tag `v1.0.30`:
the core, Windows event/thread abstractions and Windows USB backends. The source
directory was renamed from `libusb/` so receiver includes retain the established
`libusb-1.0/libusb.h` spelling. `COPYING` contains the upstream LGPL license.

The local CMake adapter builds this subset with Cygwin GCC and mirrors the
upstream Windows configuration without optional hotplug support. It optimizes
the dependency independently of the classic receiver's DSP preservation flags.
Linux continues using its system libusb through pkg-config.

The Windows build needs 1.0.30's public WinUSB RAW_IO APIs to keep reads queued
for the SiGe sampler. The bundled implementation avoids depending on Cygwin's
older libusb 1.0.21 DLL. Updating requires reviewing the upstream source and
configuration and repeating streaming and cancellation checks.
