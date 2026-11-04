# HDAUniversalBootChimeDxe
# Copyright © 2026 DNMTechLabs
# Contact: dnmtechlabs@gmail.com
# Boot chime DXE driver for Clover/OpenCore.

[Defines]
  PLATFORM_NAME                  = HDAUniversalBootChimeDxePkg
  PLATFORM_GUID                  = F0F8DAD2-42C9-4B79-8D61-278B67491C17
  PLATFORM_VERSION               = 1.0.87
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/HDAUniversalBootChimeDxePkg
  SUPPORTED_ARCHITECTURES        = X64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT

[LibraryClasses]
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  StackCheckLib|MdePkg/Library/BaseStackCheckLib/BaseStackCheckLib.inf
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf

[Components]
  HDAUniversalBootChimeDxePkg/HDAUniversalBootChimeDxe/HDAUniversalBootChimeDxe.inf
