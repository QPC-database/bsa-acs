/** @file
 * Copyright (c) 2016-2020, 2021 Arm Limited or its affiliates. All rights reserved.
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
#include <Include/libfdt.h>
#include <Protocol/HardwareInterrupt.h>
#include <Protocol/HardwareInterrupt2.h>

#include "include/pal_uefi.h"
#include "include/bsa_pcie_enum.h"
#include "src_gic_its/bsa_gic_its.h"
#include "include/pal_dt.h"
#include "include/pal_dt_spec.h"

static char gicv3_dt_arr[][GIC_COMPATIBLE_STR_LEN] = {
    "arm,gic-v3",
};

static char gicv2_dt_arr[][GIC_COMPATIBLE_STR_LEN] = {
    "arm,cortex-a15-gic",
    "arm,cortex-a9-gic",
    "arm,cortex-a7-gic",
    "arm,cortex-a5-gic",
    "arm,eb11mp-gic",
    "arm,arm11mp-gic",
    "arm,gic-400",
    "arm,pl390",
    "qcom,msm-8660-qgic",
    "qcom,msm-qgic2",
};

static char gicv2m_frame_dt_arr[][GIC_COMPATIBLE_STR_LEN] = {
    "arm,gic-v2m-frame"
};

static char its_dt_arr[][GIC_COMPATIBLE_STR_LEN] = {
    "arm,gic-v3-its"
};
static EFI_ACPI_6_1_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *gMadtHdr;

EFI_HARDWARE_INTERRUPT_PROTOCOL *gInterrupt = NULL;
EFI_HARDWARE_INTERRUPT2_PROTOCOL *gInterrupt2 = NULL;

UINT64
pal_get_madt_ptr();

GIC_INFO_ENTRY  *g_gic_entry = NULL;
GIC_ITS_INFO    *g_gic_its_info;

/**
  @brief  This API fills in the PE_INFO_TABLE  with information about GIC maintenance
          interrupt in the system. This is achieved by parsing the DT.

  @param  PeTable  - Address where the information needs to be filled.

  @return  None
**/
VOID
pal_pe_info_table_gmaint_gsiv_dt(PE_INFO_TABLE *PeTable)
{
  int i, offset, prop_len;
  UINT64 dt_ptr = 0;
  UINT32 *Pintr;
  PE_INFO_ENTRY *Ptr = NULL;
  int interrupt_cell;

  if (PeTable == NULL)
    return;

  dt_ptr = pal_get_dt_ptr();
  if (dt_ptr == 0) {
      bsa_print(ACS_PRINT_ERR, L" dt_ptr is NULL \n");
      return;
  }

  Ptr = PeTable->pe_info;
  for (i = 0; i < (sizeof(gicv3_dt_arr)/GIC_COMPATIBLE_STR_LEN); i++) {
      /* Search for GICv3 nodes*/
      offset = fdt_node_offset_by_compatible((const void *)dt_ptr, -1, gicv3_dt_arr[i]);
      if (offset < 0) {
        bsa_print(ACS_PRINT_DEBUG, L"GICv3 compatible value not found for index : %d\n", i);
        continue; /* Search for next compatible item*/
      }
      else {
        bsa_print(ACS_PRINT_DEBUG, L" NODE Int Ctrl offset  %x  \n", offset);
        break;
      }
  }

  if (offset < 0) {
      for (i = 0; i < (sizeof(gicv2_dt_arr)/GIC_COMPATIBLE_STR_LEN); i++) {
          /* Search for GICv2 nodes*/
          offset = fdt_node_offset_by_compatible((const void *)dt_ptr, -1, gicv2_dt_arr[i]);
          if (offset < 0) {
              bsa_print(ACS_PRINT_DEBUG, L"GICv2 compatible value not found for index : %d\n", i);
              continue; /* Search for next compatible item*/
          }
          else {
            bsa_print(ACS_PRINT_DEBUG, L" NODE Int Ctrl offset  %x  \n", offset);
            break;
          }
      }
  }

  if (offset < 0) {
      bsa_print(ACS_PRINT_DEBUG, L"GIC compatible node not found\n");
      return;
  }

  /* read the interrupt property value */
  Pintr = (UINT32 *)fdt_getprop_namelen((void *)dt_ptr, offset, "interrupts", 10, &prop_len);
  if ((prop_len < 0) || (Pintr == NULL)) {
      bsa_print(ACS_PRINT_ERR, L" PROPERTY interrupts offset %x, Error %d\n", offset, prop_len);
      return;
  }

  interrupt_cell = fdt_interrupt_cells((const void *)dt_ptr, offset);
  bsa_print(ACS_PRINT_DEBUG, L" interrupt_cell  %d\n", interrupt_cell);
  if (interrupt_cell < 1 || interrupt_cell > 3) {
      bsa_print(ACS_PRINT_ERR, L" Invalid interrupt cell : %d \n", interrupt_cell);
      return;
  }

  for (i = 0; i < PeTable->header.num_of_pe; i++) {
      if (interrupt_cell == 3) {
          if (Pintr[0])
              Ptr->gmain_gsiv = fdt32_to_cpu(Pintr[1]) + PPI_OFFSET;
          else
              bsa_print(ACS_PRINT_WARN, L" Int is not PPI \n", 0);
      }
      else
          Ptr->gmain_gsiv = fdt32_to_cpu(Pintr[0]) + PPI_OFFSET;
      Ptr++;
  }
}

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
    bsa_print(ACS_PRINT_ERR, L" Input GIC Table Pointer is NULL. Cannot create GIC INFO \n");
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
    bsa_print(ACS_PRINT_DEBUG, L" MADT not found. Checking DT table \n");
    pal_gic_create_info_table_dt(GicTable);
    dt_dump_gic_table(GicTable);
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
        GicEntry->its_id = ((EFI_ACPI_6_1_GIC_ITS_STRUCTURE *)Entry)->GicItsId;
        bsa_print(ACS_PRINT_INFO, L"GIC ITS base %x \n", GicEntry->base);
        bsa_print(ACS_PRINT_INFO, L"GIC ITS ID%x \n", GicEntry->its_id);
        GicTable->header.num_its++;
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
pal_gic_request_irq (
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
pal_gic_free_irq (
  UINT32 IrqNum,
  UINT32 MappedIrqNum
  )
{

}

UINT32
pal_gic_its_configure (
  )
{
  /*
   * This function configure the gic to have support for LPIs,
   * If supported in the system.
  */
  EFI_STATUS Status;

  /* Allocate memory to store ITS info */
  g_gic_its_info = (GIC_ITS_INFO *) pal_mem_alloc(1024);
  if (!g_gic_its_info) {
      bsa_print(ACS_PRINT_DEBUG, L"GIC : ITS table memory allocation failed\n", 0);
      goto its_fail;
  }

  g_gic_its_info->GicNumIts = 0;
  g_gic_its_info->GicRdBase = 0;
  g_gic_its_info->GicDBase  = 0;

  while (g_gic_entry->type != 0xFF)
  {
    if (g_gic_entry->type == ENTRY_TYPE_GICD)
    {
        g_gic_its_info->GicDBase = g_gic_entry->base;
    }
    else if ((g_gic_entry->type == ENTRY_TYPE_GICR_GICRD) || (g_gic_entry->type == ENTRY_TYPE_GICC_GICRD))
    {
        /* Calculate Current PE Redistributor Base Address */
        if (g_gic_its_info->GicRdBase == 0)
        {
            if (g_gic_entry->type == ENTRY_TYPE_GICR_GICRD)
                g_gic_its_info->GicRdBase = GetCurrentCpuRDBase(g_gic_entry->base, g_gic_entry->length);
            else
                g_gic_its_info->GicRdBase = GetCurrentCpuRDBase(g_gic_entry->base, 0);
        }
    }
    else if (g_gic_entry->type == ENTRY_TYPE_GICITS)
    {
        g_gic_its_info->GicIts[g_gic_its_info->GicNumIts].Base = g_gic_entry->base;
        g_gic_its_info->GicIts[g_gic_its_info->GicNumIts++].ID = g_gic_entry->its_id;
    }
    g_gic_entry++;
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

/**
  @brief  This API fills in the GIC_INFO Table with information about the GIC in the
          system. This is achieved by parsing the DT blob.
  @param  PeTable  - Address where the GIC information needs to be filled.
  @return  None
**/
VOID
pal_gic_create_info_table_dt(GIC_INFO_TABLE *GicTable)
{
  GIC_INFO_ENTRY           *GicEntry = NULL;
  UINT64 dt_ptr, cpuif_base, cpuif_length;
  UINT32 *Preg_val, *Prdregions_val;
  UINT32 num_of_pe, num_of_rd, Index = 0;
  int prop_len, i;
  int addr_cell, size_cell;
  int offset, parent_offset, num_gic_interfaces;

  dt_ptr = pal_get_dt_ptr();
  if (dt_ptr == 0) {
      bsa_print(ACS_PRINT_ERR, L" dt_ptr is NULL \n");
      return;
  }

  GicEntry = GicTable->gic_info;
  GicEntry->type = 0xFF;

  for (i = 0; i < (sizeof(gicv3_dt_arr)/GIC_COMPATIBLE_STR_LEN); i++) {
      /* Search for GICv3 nodes*/
      offset = fdt_node_offset_by_compatible((const void *)dt_ptr, -1, gicv3_dt_arr[i]);
      if (offset < 0) {
        bsa_print(ACS_PRINT_DEBUG, L"GICv3 compatible value not found for index : %d\n", i);
        continue; /* Search for next compatible item*/
      }
      else {
        bsa_print(ACS_PRINT_DEBUG, L" NODE Int Ctrl offset  %x  \n", offset);
        GicTable->header.gic_version = 3;
        break;
      }
  }

  if (offset < 0) {
      bsa_print(ACS_PRINT_DEBUG, L"GIC v3 compatible node not found\n");
      for (i = 0; i < (sizeof(gicv2_dt_arr)/GIC_COMPATIBLE_STR_LEN); i++) {
          /* Search for GICv2 nodes*/
          offset = fdt_node_offset_by_compatible((const void *)dt_ptr, -1, gicv2_dt_arr[i]);
          if (offset < 0) {
            bsa_print(ACS_PRINT_DEBUG, L"GICv2 compatible value not found for index : %d\n", i);
            continue; /* Search for next compatible item*/
          }
          else {
            bsa_print(ACS_PRINT_DEBUG, L" NODE Int Ctrl offset  %x  \n", offset);
            GicTable->header.gic_version = 2;
            break;
          }
      }
  }

  if (offset < 0) {
      bsa_print(ACS_PRINT_DEBUG, L"GIC v2 compatible node not found\n");
      return;
  }

  /* Read the address and size cell for decoding reg property */
  parent_offset = fdt_parent_offset((const void *) dt_ptr, offset);

  size_cell = fdt_size_cells((const void *) dt_ptr, parent_offset);
  bsa_print(ACS_PRINT_DEBUG, L" NODE gic size cell %d\n", size_cell);
  if (size_cell < 0) {
      bsa_print(ACS_PRINT_ERR, L" Invalid size cell for node gic\n");
      return;
  }

  addr_cell = fdt_address_cells((const void *) dt_ptr, parent_offset);
  bsa_print(ACS_PRINT_DEBUG, L" NODE gic addr cell %d\n", addr_cell);
  if (addr_cell < 0) {
      bsa_print(ACS_PRINT_ERR, L" Invalid address cell for node gic\n");
      return;
  }

  /* read the reg property value */
  Preg_val = (UINT32 *)fdt_getprop_namelen((void *)dt_ptr, offset, "reg", 3, &prop_len);
  if ((prop_len < 0) || (Preg_val == NULL)) {
      bsa_print(ACS_PRINT_ERR, L" PROPERTY reg offset %x, Error %d\n", offset, prop_len);
      return;
  }

  num_gic_interfaces = (prop_len/sizeof(int))/(addr_cell + size_cell);
  bsa_print(ACS_PRINT_DEBUG, L" Gic frame count : %d \n", num_gic_interfaces);

  /* Fill details for Distributor */
  GicEntry->type = ENTRY_TYPE_GICD;
  if (addr_cell == 2) {
      GicEntry->base = fdt32_to_cpu(Preg_val[Index++]);
      GicEntry->base = (GicEntry->base << 32) | fdt32_to_cpu(Preg_val[Index++]);
  } else
    GicEntry->base = fdt32_to_cpu(Preg_val[Index++]);

  if (size_cell == 2) {
      GicEntry->length = fdt32_to_cpu(Preg_val[Index++]);
      GicEntry->length = (GicEntry->length << 32) | fdt32_to_cpu(Preg_val[Index++]);
  } else
    GicEntry->length = fdt32_to_cpu(Preg_val[Index++]);

  bsa_print(ACS_PRINT_DEBUG, L"GIC DIS base %x \n", GicEntry->base);
  GicTable->header.num_gicd++;
  GicEntry++;

  if (GicTable->header.gic_version == 3) { /* RD present only in gicv3 node */
      /* get number of redistributor implemented */
      Prdregions_val = (UINT32 *)fdt_getprop_namelen((void *)dt_ptr, offset,
                                    "redistributor-regions", 21, &prop_len);
      if (prop_len < 0) {
          bsa_print(ACS_PRINT_DEBUG, L" Single redistributor regions present \n");
          num_of_rd = 1;
      } else
          num_of_rd = fdt32_to_cpu(Prdregions_val[0]);

      bsa_print(ACS_PRINT_DEBUG, L" NUM GIC RD %d \n", num_of_rd);
      i = num_of_rd;
      /* Fill details for Redistributor */
      while (i--) {
          GicEntry->type = ENTRY_TYPE_GICR_GICRD;
          if (addr_cell == 2) {
              GicEntry->base = fdt32_to_cpu(Preg_val[Index++]);
              GicEntry->base = (GicEntry->base << 32) | fdt32_to_cpu(Preg_val[Index++]);
          } else
              GicEntry->base = fdt32_to_cpu(Preg_val[Index++]);

          if (size_cell == 2) {
              GicEntry->length = fdt32_to_cpu(Preg_val[Index++]);
              GicEntry->length = (GicEntry->length << 32) | fdt32_to_cpu(Preg_val[Index++]);
          } else
              GicEntry->length = fdt32_to_cpu(Preg_val[Index++]);

          bsa_print(ACS_PRINT_DEBUG, L"GIC RD base %x \n", GicEntry->base);
          GicTable->header.num_gicrd++;
          GicEntry++;
    }
  }

  if ((num_gic_interfaces - num_of_rd) > 1) {
      /* Fill details for CPU-IF , if available*/
      if (addr_cell == 2) {
        cpuif_base = fdt32_to_cpu(Preg_val[Index++]);
        cpuif_base = (cpuif_base << 32) | fdt32_to_cpu(Preg_val[Index++]);
      } else
        cpuif_base = fdt32_to_cpu(Preg_val[Index++]);

      if (size_cell == 2) {
        cpuif_length = fdt32_to_cpu(Preg_val[Index++]);
        cpuif_length = (cpuif_length << 32) | fdt32_to_cpu(Preg_val[Index++]);
      } else
        cpuif_length = fdt32_to_cpu(Preg_val[Index++]);

      num_of_pe = pal_pe_get_num();
      while (num_of_pe--) {
        GicEntry->type = ENTRY_TYPE_CPUIF;
        GicEntry->base = cpuif_base;
        GicEntry->length = cpuif_length;
        bsa_print(ACS_PRINT_DEBUG, L"GIC CPUIF base %x \n", GicEntry->base);
        GicEntry++;
      }
  } else
    bsa_print(ACS_PRINT_WARN, L" GIC CPUIF not present\n");

  num_gic_interfaces -= (num_of_rd + 1);
  bsa_print(ACS_PRINT_INFO, L" Number of gic interface %d\n", num_gic_interfaces);

  if (GicTable->header.gic_version == 2) { /* parse v2m frame if present */
      /* fill details of GICH needed for gic v2 */
      if (num_gic_interfaces > 1) {
          GicEntry->type = ENTRY_TYPE_GICH;
          if (addr_cell == 2) {
            GicEntry->base = fdt32_to_cpu(Preg_val[Index++]);
            GicEntry->base = (GicEntry->base << 32) | fdt32_to_cpu(Preg_val[Index++]);
          } else
            GicEntry->base = fdt32_to_cpu(Preg_val[Index++]);

          if (size_cell == 2) {
            GicEntry->length = fdt32_to_cpu(Preg_val[Index++]);
            GicEntry->length = (GicEntry->length << 32) | fdt32_to_cpu(Preg_val[Index++]);
          } else
            GicEntry->length = fdt32_to_cpu(Preg_val[Index++]);
          bsa_print(ACS_PRINT_DEBUG, L"GICH base %x \n", GicEntry->base);
          GicEntry++;
      }

      /* Search for GICv2m-frame nodes*/
      offset = fdt_node_offset_by_compatible((const void *)dt_ptr, -1, gicv2m_frame_dt_arr[0]);
      if (offset < 0) {
          bsa_print(ACS_PRINT_DEBUG, L" No v2m-frame present\n", 0);
          GicEntry->type = 0xFF;
          return;
      }

      /* Read the address and size cell for decoding reg property */
      parent_offset = fdt_parent_offset((const void *) dt_ptr, offset);

      size_cell = fdt_size_cells((const void *) dt_ptr, parent_offset);
      bsa_print(ACS_PRINT_DEBUG, L" NODE gic size cell %d\n", size_cell);
      if (size_cell < 0) {
          bsa_print(ACS_PRINT_ERR, L" Invalid size cell for node gic\n");
          return;
      }

      addr_cell = fdt_address_cells((const void *) dt_ptr, parent_offset);
      bsa_print(ACS_PRINT_DEBUG, L" NODE gic addr cell %d\n", addr_cell);
      if (addr_cell < 0) {
          bsa_print(ACS_PRINT_ERR, L" Invalid address cell for node gic\n");
          return;
      }

      while (offset != -FDT_ERR_NOTFOUND) {
          bsa_print(ACS_PRINT_DEBUG, L" NODE v2m frame offset %x  \n", offset);
          Index = 0;
          /* read the reg property value */
          Preg_val = (UINT32 *)fdt_getprop_namelen((void *)dt_ptr, offset, "reg", 3, &prop_len);
          if ((prop_len < 0) || (Preg_val == NULL)) {
              bsa_print(ACS_PRINT_ERR, L" PROPERTY reg offset %x, Error %d\n", offset, prop_len);
              return;
          }

          /* Fill details for msi frame */
          GicEntry->type = ENTRY_TYPE_GIC_MSI_FRAME;
          GicTable->header.num_msi_frame++;

          if (addr_cell == 2) {
             GicEntry->base = fdt32_to_cpu(Preg_val[Index++]);
             GicEntry->base = (GicEntry->base << 32) | fdt32_to_cpu(Preg_val[Index++]);
          } else
             GicEntry->base = fdt32_to_cpu(Preg_val[Index++]);

          if (size_cell == 2) {
             GicEntry->length = fdt32_to_cpu(Preg_val[Index++]);
             GicEntry->length = (GicEntry->length << 32) | fdt32_to_cpu(Preg_val[Index++]);
          } else
             GicEntry->length = fdt32_to_cpu(Preg_val[Index++]);

          bsa_print(ACS_PRINT_DEBUG, L"GIC v2m frame base %x \n", GicEntry->base);

          /* read the arm,msi-base-spi property value */
          Preg_val = (UINT32 *)fdt_getprop_namelen((void *)dt_ptr, offset, "arm,msi-base-spi", 16,
                                                    &prop_len);
          if ((prop_len < 0) || (Preg_val == NULL)) {
              bsa_print(ACS_PRINT_WARN, L" PROPERTY arm,msi-base-spi Error %d\n", prop_len);
              GicEntry->spi_base = 0;
          } else
              GicEntry->spi_base = fdt32_to_cpu(Preg_val[0]);

           /* read the arm,msi-num-spis property value */
          Preg_val = (UINT32 *)fdt_getprop_namelen((void *)dt_ptr, offset, "arm,msi-num-spis", 16,
                                                    &prop_len);
          if ((prop_len < 0) || (Preg_val == NULL)) {
              bsa_print(ACS_PRINT_WARN, L" PROPERTY arm,msi-num-spis Error %d\n", prop_len);
              GicEntry->spi_count = 0;
          } else
              GicEntry->spi_count = fdt32_to_cpu(Preg_val[0]);

          GicEntry++;
          offset = fdt_node_offset_by_compatible((const void *)dt_ptr, offset,
                                                 gicv2m_frame_dt_arr[0]);
      }
      bsa_print(ACS_PRINT_DEBUG, L" Num of v2m frame %x \n", GicTable->header.num_msi_frame);
  }

  if (GicTable->header.gic_version == 3) { /* Check if ITS sub-node present */
      /* Search for its nodes*/
      offset = fdt_node_offset_by_compatible((const void *)dt_ptr, -1, its_dt_arr[0]);
      if (offset < 0) {
          bsa_print(ACS_PRINT_DEBUG, L" No its present\n", 0);
          GicEntry->type = 0xFF;
          return;
      }
      while (offset != -FDT_ERR_NOTFOUND) {
          GicTable->header.num_its++;
          offset = fdt_node_offset_by_compatible((const void *)dt_ptr, offset, its_dt_arr[0]);
      }
      bsa_print(ACS_PRINT_DEBUG, L" Num of its frame %x \n", GicTable->header.num_its);
  }

  /* Mark end of table */
  GicEntry->type = 0xFF;
}
