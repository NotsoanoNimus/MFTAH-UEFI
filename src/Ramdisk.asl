/**
  The definition block in ACPI table for NVDIMM root device.

  Copyright (c) 2016, Intel Corporation. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

DefinitionBlock (
  "Ramdisk.aml",
  "SSDT",
  2,
  "MFTAH ",
  "RamDisk ",
  0x1000
  )
{
  Scope (\_SB)
  {
    Device (NVDR)
    {
      // Define _HID, "ACPI0012" NVDIMM Root Device
      Name (_HID, "ACPI0012")

      // Readable name of this device
      Name (_STR, Unicode("NVDIMM Root Device"))

      Method (_STA, 0)
      {
        Return (0x0f)
      }

      // NOTE: We could define a simple 'NVD' Device in this tree when an NVDR doesn't
      //       already exist. When one does exist, we just need to update NFIT information
      //       and add the new handle to the NVDR. But I think the NVDR implies an existing NFIT.
      // SEE: https://patchwork.kernel.org/project/kvm/patch/1445216059-88521-23-git-send-email-guangrong.xiao@linux.intel.com/
      //
      // Device(NVD)
      // {
      //   Name(_ADR, 0x00)   // <-- 'h' is the Handle to the NFIT table.
      //   Method(_DSM) { ... }
      // }
    }
  }
}
