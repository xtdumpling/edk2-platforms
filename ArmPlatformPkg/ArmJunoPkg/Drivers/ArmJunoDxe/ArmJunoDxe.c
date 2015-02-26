/** @file
*
*  Copyright (c) 2013-2015, ARM Limited. All rights reserved.
*
*  This program and the accompanying materials
*  are licensed and made available under the terms and conditions of the BSD License
*  which accompanies this distribution.  The full text of the license may be found at
*  http://opensource.org/licenses/bsd-license.php
*
*  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
*  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*
**/

#include "ArmJunoDxeInternal.h"
#include <Library/ArmShellCmdLib.h>
#include <Library/AcpiLib.h>

// This GUID must match the FILE_GUID in ArmPlatformPkg/ArmJunoPkg/AcpiTables/AcpiTables.inf
STATIC CONST EFI_GUID mJunoAcpiTableFile = { 0xa1dd808e, 0x1e95, 0x4399, { 0xab, 0xc0, 0x65, 0x3c, 0x82, 0xe8, 0x53, 0x0c } };

EFI_STATUS
EFIAPI
ArmJunoEntryPoint (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  HypBase;
  CHAR16                *TextDevicePath;
  UINTN                 TextDevicePathSize;
  VOID                  *Buffer;
  UINT32                Midr;
  UINT32                CpuType;
  UINT32                CpuRev;

  Status = PciEmulationEntryPoint ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // If a hypervisor has been declared then we need to make sure its region is protected at runtime
  //
  // Note: This code is only a workaround for our dummy hypervisor (ArmPkg/Extra/AArch64ToAArch32Shim/)
  //       that does not set up (yet) the stage 2 translation table to hide its own memory to EL1.
  //
  if (FixedPcdGet32 (PcdHypFvSize) != 0) {
    // Ensure the hypervisor region is strictly contained into a EFI_PAGE_SIZE-aligned region.
    // The memory must be a multiple of EFI_PAGE_SIZE to ensure we do not reserve more memory than the hypervisor itself.
    // A UEFI Runtime region size granularity cannot be smaller than EFI_PAGE_SIZE. If the hypervisor size is not rounded
    // to this size then there is a risk some non-runtime memory could be visible to the OS view.
    if (((FixedPcdGet32 (PcdHypFvSize) & EFI_PAGE_MASK) == 0) && ((FixedPcdGet32 (PcdHypFvBaseAddress) & EFI_PAGE_MASK) == 0)) {
      // The memory needs to be declared because the DXE core marked it as reserved and removed it from the memory space
      // as it contains the Firmware.
      Status = gDS->AddMemorySpace (
          EfiGcdMemoryTypeSystemMemory,
          FixedPcdGet32 (PcdHypFvBaseAddress), FixedPcdGet32 (PcdHypFvSize),
          EFI_MEMORY_WB | EFI_MEMORY_RUNTIME
          );
      if (!EFI_ERROR (Status)) {
        // We allocate the memory to ensure it is marked as runtime memory
        HypBase = FixedPcdGet32 (PcdHypFvBaseAddress);
        Status = gBS->AllocatePages (AllocateAddress, EfiRuntimeServicesCode,
                                     EFI_SIZE_TO_PAGES (FixedPcdGet32 (PcdHypFvSize)), &HypBase);
      }
    } else {
      // The hypervisor must be contained into a EFI_PAGE_SIZE-aligned region and its size must also be aligned
      // on a EFI_PAGE_SIZE boundary (ie: 4KB).
      Status = EFI_UNSUPPORTED;
      ASSERT_EFI_ERROR (Status);
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  // Install dynamic Shell command to run baremetal binaries.
  Status = ShellDynCmdRunAxfInstall (ImageHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "ArmJunoDxe: Failed to install ShellDynCmdRunAxf\n"));
  }

  //
  // Set up the device path to the FDT.
  // We detect whether we are running on a Juno r0 or Juno r1 board at
  // runtime by checking the value of the MIDR register.
  //

  Midr    = ArmReadMidr ();
  CpuType = (Midr >> ARM_CPU_TYPE_SHIFT) & ARM_CPU_TYPE_MASK;
  CpuRev  = Midr & ARM_CPU_REV_MASK;
  TextDevicePath = NULL;

  switch (CpuType) {
  case ARM_CPU_TYPE_A53:
    if (CpuRev == ARM_CPU_REV (0, 0)) {
      TextDevicePath = (CHAR16*)FixedPcdGetPtr (PcdJunoR0FdtDevicePath);
    } else if (CpuRev == ARM_CPU_REV (0, 3)) {
      TextDevicePath = (CHAR16*)FixedPcdGetPtr (PcdJunoR1A57x2FdtDevicePath);
    }
    break;

  case ARM_CPU_TYPE_A57:
    if (CpuRev == ARM_CPU_REV (0, 0)) {
      TextDevicePath = (CHAR16*)FixedPcdGetPtr (PcdJunoR0FdtDevicePath);
    } else if (CpuRev == ARM_CPU_REV (1, 1)) {
      TextDevicePath = (CHAR16*)FixedPcdGetPtr (PcdJunoR1A57x2FdtDevicePath);
    }
  }

  if (TextDevicePath != NULL) {
    TextDevicePathSize = StrSize (TextDevicePath);
    Buffer = PcdSetPtr (PcdFdtDevicePaths, &TextDevicePathSize, TextDevicePath);
    Status = (Buffer != NULL) ? EFI_SUCCESS : EFI_BUFFER_TOO_SMALL;
  } else {
    Status = EFI_NOT_FOUND;
  }

  if (EFI_ERROR (Status)) {
    DEBUG (
      (EFI_D_ERROR,
      "ArmJunoDxe: Setting of FDT device path in PcdFdtDevicePaths failed - %r\n", Status)
      );
    return Status;
  }

  // Try to install the ACPI Tables
  Status = LocateAndInstallAcpiFromFv (&mJunoAcpiTableFile);

  return Status;
}
