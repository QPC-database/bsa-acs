/** @file
 * Copyright (c) 2016-2021, Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier : Apache-2.0

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "Include/IndustryStandard/Acpi61.h"
#include <Protocol/AcpiTable.h>
#include <Protocol/HardwareInterrupt.h>
#include <Protocol/HardwareInterrupt2.h>

#include "include/pal_uefi.h"
#include "include/bsa_pcie_enum.h"
#include "src_gic_its/bsa_gic_its.h"

static EFI_ACPI_6_1_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *gMadtHdr;

EFI_HARDWARE_INTERRUPT_PROTOCOL *gInterrupt = NULL;
EFI_HARDWARE_INTERRUPT2_PROTOCOL *gInterrupt2 = NULL;

UINT64
pal_get_madt_ptr();

GIC_INFO_ENTRY  *g_gic_entry = NULL;
GIC_ITS_INFO    *g_gic_its_info;

/**
  @brief  Populate information about the GIC sub-system at the input address.
          In a UEFI-ACPI framework, this information is part of the MADT table.

  @param  GicTable  Address of the memory region where this information is to be filled in

  @return None
**/
VOID
pal_gic_create_info_table(GIC_INFO_TABLE *GicTable)
{
  EFI_ACPI_6_1_GIC_STRUCTURE    *Entry = NULL;
  GIC_INFO_ENTRY                *GicEntry = NULL;
  UINT32                         Length= 0;
  UINT32                         TableLength;

  if (GicTable == NULL) {
    bsa_print(ACS_PRINT_ERR, L"Input GIC Table Pointer is NULL. Cannot create GIC INFO \n");
    return;
  }

  GicEntry = GicTable->gic_info;
  g_gic_entry = GicTable->gic_info;

  GicTable->header.gic_version = 0;
  GicTable->header.num_gicrd = 0;
  GicTable->header.num_gicd = 0;
  GicTable->header.num_its = 0;
  GicTable->header.num_msi_frame = 0;

  gMadtHdr = (EFI_ACPI_6_1_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *) pal_get_madt_ptr();

  if (gMadtHdr != NULL) {
    TableLength =  gMadtHdr->Header.Length;
    bsa_print(ACS_PRINT_INFO, L" MADT is at %x and length is %x \n", gMadtHdr, TableLength);
  } else {
    bsa_print(ACS_PRINT_ERR, L" MADT not found \n");
    return;
  }

  Entry = (EFI_ACPI_6_1_GIC_STRUCTURE *) (gMadtHdr + 1);
  Length = sizeof (EFI_ACPI_6_1_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER);


  do {

    if (Entry->Type == EFI_ACPI_6_1_GIC) {
      if (Entry->PhysicalBaseAddress != 0) {
        GicEntry->type = ENTRY_TYPE_CPUIF;
        GicEntry->base = Entry->PhysicalBaseAddress;
        bsa_print(ACS_PRINT_INFO, L"GIC CPUIF base %x \n", GicEntry->base);
        GicEntry++;
      }

      if (Entry->GICRBaseAddress != 0) {
        GicEntry->type = ENTRY_TYPE_GICC_GICRD;
        GicEntry->base = Entry->GICRBaseAddress;
        GicEntry->length = 0;
        bsa_print(ACS_PRINT_INFO, L"GIC RD base %x \n", GicEntry->base);
        GicTable->header.num_gicrd++;
        GicEntry++;
      }

      if (Entry->GICH != 0) {
        GicEntry->type = ENTRY_TYPE_GICH;
        GicEntry->base = Entry->GICH;
        GicEntry->length = 0;
        bsa_print(ACS_PRINT_INFO, L"GICH base %x \n", GicEntry->base);
        GicEntry++;
      }
    }

    if (Entry->Type == EFI_ACPI_6_1_GICD) {
        GicEntry->type = ENTRY_TYPE_GICD;
        GicEntry->base = ((EFI_ACPI_6_1_GIC_DISTRIBUTOR_STRUCTURE *)Entry)->PhysicalBaseAddress;
        GicTable->header.gic_version = ((EFI_ACPI_6_1_GIC_DISTRIBUTOR_STRUCTURE *)Entry)->GicVersion;
        bsa_print(ACS_PRINT_INFO, L"GIC DIS base %x \n", GicEntry->base);
        GicTable->header.num_gicd++;
        GicEntry++;
    }

    if (Entry->Type == EFI_ACPI_6_1_GICR) {
        GicEntry->type = ENTRY_TYPE_GICR_GICRD;
        GicEntry->base = ((EFI_ACPI_6_1_GICR_STRUCTURE *)Entry)->DiscoveryRangeBaseAddress;
        GicEntry->length = ((EFI_ACPI_6_1_GICR_STRUCTURE *)Entry)->DiscoveryRangeLength;
        bsa_print(ACS_PRINT_INFO, L"GIC RD base Structure %x \n", GicEntry->base);
        GicTable->header.num_gicrd++;
        GicEntry++;
    }

    if (Entry->Type == EFI_ACPI_6_1_GIC_ITS) {
        GicEntry->type = ENTRY_TYPE_GICITS;
        GicEntry->base = ((EFI_ACPI_6_1_GIC_ITS_STRUCTURE *)Entry)->PhysicalBaseAddress;
        GicEntry->entry_id = ((EFI_ACPI_6_1_GIC_ITS_STRUCTURE *)Entry)->GicItsId;
        bsa_print(ACS_PRINT_INFO, L"GIC ITS base %x \n", GicEntry->base);
        bsa_print(ACS_PRINT_INFO, L"GIC ITS ID%x \n", GicEntry->entry_id);
        GicTable->header.num_its++;
        GicEntry++;
    }

    if (Entry->Type == EFI_ACPI_6_1_GIC_MSI_FRAME) {
        GicEntry->type = ENTRY_TYPE_GIC_MSI_FRAME;
        GicEntry->base = ((EFI_ACPI_6_1_GIC_MSI_FRAME_STRUCTURE *)Entry)->PhysicalBaseAddress;
        GicEntry->entry_id = ((EFI_ACPI_6_1_GIC_MSI_FRAME_STRUCTURE *)Entry)->GicMsiFrameId;
        GicEntry->flags = ((EFI_ACPI_6_1_GIC_MSI_FRAME_STRUCTURE *)Entry)->Flags;
        GicEntry->spi_count = ((EFI_ACPI_6_1_GIC_MSI_FRAME_STRUCTURE *)Entry)->SPICount;
        GicEntry->spi_base = ((EFI_ACPI_6_1_GIC_MSI_FRAME_STRUCTURE *)Entry)->SPIBase;
        bsa_print(ACS_PRINT_INFO, L"GIC MSI Frame base %x \n", GicEntry->base);
        bsa_print(ACS_PRINT_INFO, L"GIC MSI SPI base %x \n", GicEntry->spi_base);
        bsa_print(ACS_PRINT_INFO, L"GIC MSI SPI Count %x \n", GicEntry->spi_count);
        GicTable->header.num_msi_frame++;
        GicEntry++;
    }
    Length += Entry->Length;
    Entry = (EFI_ACPI_6_1_GIC_STRUCTURE *) ((UINT8 *)Entry + (Entry->Length));


  } while(Length < TableLength);

  GicEntry->type = 0xFF;  //Indicate end of data

}

/**
  @brief  Enable the interrupt in the GIC Distributor and GIC CPU Interface and hook
          the interrupt service routine for the IRQ to the UEFI Framework

  @param  int_id  Interrupt ID which needs to be enabled and service routine installed for
  @param  isr     Function pointer of the Interrupt service routine

  @return Status of the operation
**/
UINT32
pal_gic_install_isr(UINT32 int_id,  VOID (*isr)())
{

  EFI_STATUS  Status;

 // Find the interrupt controller protocol.
  Status = gBS->LocateProtocol (&gHardwareInterruptProtocolGuid, NULL, (VOID **)&gInterrupt);
  if (EFI_ERROR(Status)) {
    return 0xFFFFFFFF;
  }

  //First disable the interrupt to enable a clean handoff to our Interrupt handler.
  gInterrupt->DisableInterruptSource(gInterrupt, int_id);

  //Register our handler
  Status = gInterrupt->RegisterInterruptSource (gInterrupt, int_id, isr);
  if (EFI_ERROR(Status)) {
    Status =  gInterrupt->RegisterInterruptSource (gInterrupt, int_id, NULL);  //Deregister existing handler
    Status = gInterrupt->RegisterInterruptSource (gInterrupt, int_id, isr);  //register our Handler.
    //Even if this fails. there is nothing we can do in UEFI mode
  }

  return 0;
}

/**
  @brief  Indicate that processing of interrupt is complete by writing to
          End of interrupt register in the GIC CPU Interface

  @param  int_id  Interrupt ID which needs to be acknowledged that it is complete

  @return Status of the operation
**/
UINT32
pal_gic_end_of_interrupt(UINT32 int_id)
{

  EFI_STATUS  Status;

 // Find the interrupt controller protocol.
  Status = gBS->LocateProtocol (&gHardwareInterruptProtocolGuid, NULL, (VOID **)&gInterrupt);
  if (EFI_ERROR(Status)) {
    return 0xFFFFFFFF;
  }

  //EndOfInterrupt.
  gInterrupt->EndOfInterrupt(gInterrupt, int_id);

  return 0;
}

/**
  @brief  Set Trigger type Edge/Level

  @param  int_id  Interrupt ID which needs to be enabled and service routine installed for
  @param  trigger_type  Interrupt Trigger Type Edge/Trigger

  @return Status of the operation
**/
UINT32
pal_gic_set_intr_trigger(UINT32 int_id, INTR_TRIGGER_INFO_TYPE_e trigger_type)
{

  EFI_STATUS  Status;

  /* Find the interrupt protocol. */
  Status = gBS->LocateProtocol (&gHardwareInterrupt2ProtocolGuid, NULL, (VOID **)&gInterrupt2);
  if (EFI_ERROR(Status)) {
    return 0xFFFFFFFF;
  }

  Status = gInterrupt2->SetTriggerType (
                   gInterrupt2,
                   int_id,
                   (EFI_HARDWARE_INTERRUPT2_TRIGGER_TYPE)trigger_type
                   );

  if (EFI_ERROR(Status))
    return 0xFFFFFFFF;

  return 0;
}

/* Place holder function. Need to be
 * implemented if needed in later releases
 */
UINT32
pal_gic_request_irq(
  UINT32 IrqNum,
  UINT32 MappedIrqNum,
  VOID *Isr
  )
{
    return 0;
}

/* Place holder function. Need to be
 * implemented if needed in later releases
 */
VOID
pal_gic_free_irq(
  UINT32 IrqNum,
  UINT32 MappedIrqNum
  )
{

}

UINT32
pal_gic_its_configure(
  )
{
  /*
   * This function configure the gic to have support for LPIs,
   * If supported in the system.
  */
  EFI_STATUS Status;
  GIC_INFO_ENTRY *gic_entry = g_gic_entry;

  /* Allocate memory to store ITS info */
  g_gic_its_info = (GIC_ITS_INFO *) pal_mem_alloc(1024);
  if (!g_gic_its_info) {
      bsa_print(ACS_PRINT_DEBUG, L"GIC : ITS table memory allocation failed\n", 0);
      goto its_fail;
  }

  g_gic_its_info->GicNumIts = 0;
  g_gic_its_info->GicRdBase = 0;
  g_gic_its_info->GicDBase  = 0;

  while (gic_entry->type != 0xFF)
  {
    if (gic_entry->type == ENTRY_TYPE_GICD)
    {
        g_gic_its_info->GicDBase = gic_entry->base;
    }
    else if ((gic_entry->type == ENTRY_TYPE_GICR_GICRD)
        || (gic_entry->type == ENTRY_TYPE_GICC_GICRD))
    {
        /* Calculate Current PE Redistributor Base Address */
        if (g_gic_its_info->GicRdBase == 0)
        {
            if (gic_entry->type == ENTRY_TYPE_GICR_GICRD)
                g_gic_its_info->GicRdBase = GetCurrentCpuRDBase(gic_entry->base, gic_entry->length);
            else
                g_gic_its_info->GicRdBase = GetCurrentCpuRDBase(gic_entry->base, 0);
        }
    }
    else if (gic_entry->type == ENTRY_TYPE_GICITS)
    {
        g_gic_its_info->GicIts[g_gic_its_info->GicNumIts].Base = gic_entry->base;
        g_gic_its_info->GicIts[g_gic_its_info->GicNumIts++].ID = gic_entry->entry_id;
    }
    gic_entry++;
  }

  /* Return if no ITS */
  if (g_gic_its_info->GicNumIts == 0)
  {
    bsa_print(ACS_PRINT_DEBUG, L"No ITS Found in the MADT.\n", 0);
    goto its_fail;
  }

  /* Base Address Check. */
  if ((g_gic_its_info->GicRdBase == 0) || (g_gic_its_info->GicDBase == 0))
  {
    bsa_print(ACS_PRINT_DEBUG, L"Could not get GICD/GICRD Base.\n", 0);
    goto its_fail;
  }

  if (ArmGICDSupportsLPIs(g_gic_its_info->GicDBase) && ArmGICRSupportsLPIs(g_gic_its_info->GicRdBase))
  {
    Status = ArmGicItsConfiguration();
    if (EFI_ERROR(Status))
    {
      bsa_print(ACS_PRINT_DEBUG, L"Could Not Configure ITS.\n", 0);
      goto its_fail;
    }
  }
  else
  {
    bsa_print(ACS_PRINT_DEBUG, L"LPIs not supported in the system.\n", 0);
    goto its_fail;
  }

  return 0;

its_fail:
  bsa_print(ACS_PRINT_DEBUG, L"GIC ITS Initialization Failed.\n", 0);
  bsa_print(ACS_PRINT_DEBUG, L"LPI Interrupt related test may not pass.\n", 0);
  return 0xFFFFFFFF;
}

UINT32
pal_gic_get_max_lpi_id (
  )
{
  return ArmGicItsGetMaxLpiID();
}

UINT32
getItsIndex (
  IN UINT32   ItsID
  )
{
  UINT32  index;

  for (index=0; index<g_gic_its_info->GicNumIts; index++)
  {
    if (ItsID == g_gic_its_info->GicIts[index].ID)
      return index;
  }
  return 0xFFFFFFFF;
}

UINT32
pal_gic_request_msi (
  UINT32    ItsID,
  UINT32    DevID,
  UINT32    IntID,
  UINT32    msi_index,
  UINT32    *msi_addr,
  UINT32    *msi_data
  )
{
  UINT32  ItsIndex;

  if ((g_gic_its_info == NULL) || (g_gic_its_info->GicNumIts == 0))
    return 0xFFFFFFFF;

  ItsIndex = getItsIndex(ItsID);
  if (ItsIndex > g_gic_its_info->GicNumIts) {
    bsa_print(ACS_PRINT_ERR, L"\n       Could not find ITS block in MADT", 0);
    return 0xFFFFFFFF;
  }

  if ((g_gic_its_info->GicRdBase == 0) || (g_gic_its_info->GicDBase == 0))
  {
    bsa_print(ACS_PRINT_DEBUG, L"GICD/GICRD Base Invalid value.\n", 0);
    return 0xFFFFFFFF;
  }

  ArmGicItsCreateLpiMap(ItsIndex, DevID, IntID, LPI_PRIORITY1);

  *msi_addr = ArmGicItsGetGITSTranslatorAddress(ItsIndex);
  *msi_data = IntID;

  return 0;
}

VOID
pal_gic_free_msi (
  UINT32    ItsID,
  UINT32    DevID,
  UINT32    IntID,
  UINT32    msi_index
  )
{
  UINT32  ItsIndex;

  if ((g_gic_its_info == NULL) || (g_gic_its_info->GicNumIts == 0))
    return;

  ItsIndex = getItsIndex(ItsID);
  if (ItsIndex > g_gic_its_info->GicNumIts)
  {
    bsa_print(ACS_PRINT_ERR, L"\n       Could not find ITS block in MADT", 0);
    return;
  }
  if ((g_gic_its_info->GicRdBase == 0) || (g_gic_its_info->GicDBase == 0))
  {
    bsa_print(ACS_PRINT_DEBUG, L"GICD/GICRD Base Invalid value.\n", 0);
    return;
  }

  ArmGicItsClearLpiMappings(ItsIndex, DevID, IntID);
}
