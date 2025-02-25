/** @file
 * Copyright (c) 2016-2018, 2021 Arm Limited or its affiliates. All rights reserved.
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
#include <Include/libfdt.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/Cpu.h>

#include "include/pal_uefi.h"
#include "include/pal_dt.h"
#include "include/pal_dt_spec.h"

static   EFI_ACPI_6_1_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *gMadtHdr;
UINT8   *gSecondaryPeStack;
UINT64  gMpidrMax;
static UINT32 g_num_pe;

static char pmu_dt_arr[][PMU_COMPATIBLE_STR_LEN] = {
    "arm,armv8-pmuv3",
    "arm,cortex-a17-pmu",
    "arm,cortex-a15-pmu",
    "arm,cortex-a12-pmu",
    "arm,cortex-a9-pmu",
    "arm,cortex-a8-pmu",
    "arm,cortex-a7-pmu",
    "arm,cortex-a5-pmu",
    "arm,arm11mpcore-pmu",
    "arm,arm1176-pmu",
    "arm,arm1136-pmu"
};

#define SIZE_STACK_SECONDARY_PE  0x100                //256 bytes per core
#define UPDATE_AFF_MAX(src,dest,mask)  ((dest & mask) > (src & mask) ? (dest & mask) : (src & mask))

UINT64
pal_get_madt_ptr();
VOID
ArmCallSmc (
  IN OUT ARM_SMC_ARGS *Args
  );


/**
  @brief   Return the base address of the region allocated for Stack use for the Secondary
           PEs.
  @param   None
  @return  base address of the Stack
**/
UINT64
PalGetSecondaryStackBase()
{

  return (UINT64)gSecondaryPeStack;
}

/**
  @brief   Return the number of PEs in the System.
  @param   None
  @return  num_of_pe
**/
UINT32
pal_pe_get_num()
{

  return (UINT32)g_num_pe;
}

/**
  @brief   Returns the Max of each 8-bit Affinity fields in MPIDR.
  @param   None
  @return  Max MPIDR
**/
UINT64
PalGetMaxMpidr()
{

  return gMpidrMax;
}

/**
  @brief  Allocate memory region for secondary PE stack use. SIZE of stack for each PE
          is a #define

  @param  Number of PEs

  @return  None
**/
VOID
PalAllocateSecondaryStack(UINT64 mpidr)
{
  EFI_STATUS Status;
  UINT32 NumPe, Aff0, Aff1, Aff2, Aff3;

  Aff0 = ((mpidr & 0x00000000ff) >>  0);
  Aff1 = ((mpidr & 0x000000ff00) >>  8);
  Aff2 = ((mpidr & 0x0000ff0000) >> 16);
  Aff3 = ((mpidr & 0xff00000000) >> 32);

  NumPe = ((Aff3+1) * (Aff2+1) * (Aff1+1) * (Aff0+1));

  if (gSecondaryPeStack == NULL) {
      Status = gBS->AllocatePool ( EfiBootServicesData,
                    (NumPe * SIZE_STACK_SECONDARY_PE),
                    (VOID **) &gSecondaryPeStack);
      if (EFI_ERROR(Status)) {
          bsa_print(ACS_PRINT_ERR, L"\n FATAL - Allocation for Seconday stack failed %x \n", Status);
      }
      pal_pe_data_cache_ops_by_va((UINT64)&gSecondaryPeStack, CLEAN_AND_INVALIDATE);
  }

}

/**
  @brief  This API fills in the PE_INFO Table with information about the PEs in the
          system. This is achieved by parsing the ACPI - MADT table.

  @param  PeTable  - Address where the PE information needs to be filled.

  @return  None
**/
VOID
pal_pe_create_info_table(PE_INFO_TABLE *PeTable)
{
  EFI_ACPI_6_1_GIC_STRUCTURE    *Entry = NULL;
  PE_INFO_ENTRY                 *Ptr = NULL;
  UINT32                        TableLength = 0;
  UINT32                        Length = 0;
  UINT64                        MpidrAff0Max = 0, MpidrAff1Max = 0, MpidrAff2Max = 0, MpidrAff3Max = 0;


  if (PeTable == NULL) {
    bsa_print(ACS_PRINT_ERR, L"Input PE Table Pointer is NULL. Cannot create PE INFO \n");
    return;
  }

  gMadtHdr = (EFI_ACPI_6_1_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *) pal_get_madt_ptr();

  if (gMadtHdr != NULL) {
    TableLength =  gMadtHdr->Header.Length;
    bsa_print(ACS_PRINT_INFO, L" MADT is at %x and length is %x \n", gMadtHdr, TableLength);
  } else {
    bsa_print(ACS_PRINT_DEBUG, L"MADT not found..Checking DT \n");
    pal_pe_create_info_table_dt(PeTable);
    return;
  }

  PeTable->header.num_of_pe = 0;

  Entry = (EFI_ACPI_6_1_GIC_STRUCTURE *) (gMadtHdr + 1);
  Length = sizeof (EFI_ACPI_6_1_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER);
  Ptr = PeTable->pe_info;

  do {

    if (Entry->Type == EFI_ACPI_6_1_GIC) {
      //Fill in the cpu num and the mpidr in pe info table
      Ptr->mpidr    = Entry->MPIDR;
      Ptr->pe_num   = PeTable->header.num_of_pe;
      Ptr->pmu_gsiv = Entry->PerformanceInterruptGsiv;
      bsa_print(ACS_PRINT_DEBUG, L"MPIDR %x PE num %x \n", Ptr->mpidr, Ptr->pe_num);
      pal_pe_data_cache_ops_by_va((UINT64)Ptr, CLEAN_AND_INVALIDATE);
      Ptr++;
      PeTable->header.num_of_pe++;

      MpidrAff0Max = UPDATE_AFF_MAX(MpidrAff0Max, Entry->MPIDR, 0x000000ff);
      MpidrAff1Max = UPDATE_AFF_MAX(MpidrAff1Max, Entry->MPIDR, 0x0000ff00);
      MpidrAff2Max = UPDATE_AFF_MAX(MpidrAff2Max, Entry->MPIDR, 0x00ff0000);
      MpidrAff3Max = UPDATE_AFF_MAX(MpidrAff3Max, Entry->MPIDR, 0xff00000000);
    }

    Length += Entry->Length;
    Entry = (EFI_ACPI_6_1_GIC_STRUCTURE *) ((UINT8 *)Entry + (Entry->Length));

  }while(Length < TableLength);

  gMpidrMax = MpidrAff0Max | MpidrAff1Max | MpidrAff2Max | MpidrAff3Max;
  g_num_pe = PeTable->header.num_of_pe;
  pal_pe_data_cache_ops_by_va((UINT64)PeTable, CLEAN_AND_INVALIDATE);
  pal_pe_data_cache_ops_by_va((UINT64)&gMpidrMax, CLEAN_AND_INVALIDATE);
  PalAllocateSecondaryStack(gMpidrMax);

}

/**
  @brief  Install Exception Handler using UEFI CPU Architecture protocol's
          Register Interrupt Handler API

  @param  ExceptionType  - AARCH64 Exception type
  @param  esr            - Function pointer of the exception handler

  @return status of the API
**/
UINT32
pal_pe_install_esr(UINT32 ExceptionType,  VOID (*esr)(UINT64, VOID *))
{

  EFI_STATUS  Status;
  EFI_CPU_ARCH_PROTOCOL   *Cpu;

  // Get the CPU protocol that this driver requires.
  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Unregister the default exception handler.
  Status = Cpu->RegisterInterruptHandler (Cpu, ExceptionType, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Register to receive interrupts
  Status = Cpu->RegisterInterruptHandler (Cpu, ExceptionType, (EFI_CPU_INTERRUPT_HANDLER)esr);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  @brief  Make the SMC call using AARCH64 Assembly code
          SMC calls can take up to 7 arguments and return up to 4 return values.
          Therefore, the 4 first fields in the ARM_SMC_ARGS structure are used
          for both input and output values.

  @param  Argumets to pass to the EL3 firmware

  @return  None
**/
VOID
pal_pe_call_smc(ARM_SMC_ARGS *ArmSmcArgs)
{
  ArmCallSmc (ArmSmcArgs);
}

VOID
ModuleEntryPoint();

/**
  @brief  Make a PSCI CPU_ON call using SMC instruction.
          Pass PAL Assembly code entry as the start vector for the PSCI ON call

  @param  Argumets to pass to the EL3 firmware

  @return  None
**/
VOID
pal_pe_execute_payload(ARM_SMC_ARGS *ArmSmcArgs)
{
  ArmSmcArgs->Arg2 = (UINT64)ModuleEntryPoint;
  pal_pe_call_smc(ArmSmcArgs);
}

/**
  @brief Update the ELR to return from exception handler to a desired address

  @param  context - exception context structure
  @param  offset - address with which ELR should be updated

  @return  None
**/
VOID
pal_pe_update_elr(VOID *context, UINT64 offset)
{
  ((EFI_SYSTEM_CONTEXT_AARCH64*)context)->ELR = offset;
}

/**
  @brief Get the Exception syndrome from UEFI exception handler

  @param  context - exception context structure

  @return  ESR
**/
UINT64
pal_pe_get_esr(VOID *context)
{
  return ((EFI_SYSTEM_CONTEXT_AARCH64*)context)->ESR;
}

/**
  @brief Get the FAR from UEFI exception handler

  @param  context - exception context structure

  @return  FAR
**/
UINT64
pal_pe_get_far(VOID *context)
{
  return ((EFI_SYSTEM_CONTEXT_AARCH64*)context)->FAR;
}

VOID
DataCacheCleanInvalidateVA(UINT64 addr);

VOID
DataCacheCleanVA(UINT64 addr);

VOID
DataCacheInvalidateVA(UINT64 addr);

/**
  @brief Perform cache maintenance operation on an address

  @param addr - address on which cache ops to be performed
  @param type - type of cache ops

  @return  None
**/
VOID
pal_pe_data_cache_ops_by_va(UINT64 addr, UINT32 type)
{
  switch(type){
      case CLEAN_AND_INVALIDATE:
          DataCacheCleanInvalidateVA(addr);
      break;
      case CLEAN:
          DataCacheCleanVA(addr);
      break;
      case INVALIDATE:
          DataCacheInvalidateVA(addr);
      break;
      default:
          DataCacheCleanInvalidateVA(addr);
  }

}

/**
  @brief  This API fills in the PE_INFO_TABLE  with information about PMU
          in the system. This is achieved by parsing the DT.

  @param  PeTable  - Address where the PMU information needs to be filled.

  @return  None
**/
VOID
pal_pe_info_table_pmu_gsiv_dt(PE_INFO_TABLE *PeTable)
{
  int i, offset, prop_len;
  UINT64 dt_ptr = 0;
  UINT32 *Pintr;
  int index = 0;
  int interrupt_cell, interrupt_frame_count;
  PE_INFO_ENTRY *Ptr = NULL;


  if (PeTable == NULL)
    return;

  dt_ptr = pal_get_dt_ptr();
  if (dt_ptr == 0) {
      bsa_print(ACS_PRINT_ERR, L"dt_ptr is NULL \n");
      return;
  }

  Ptr = PeTable->pe_info;

  for (i = 0; i < (sizeof(pmu_dt_arr)/PMU_COMPATIBLE_STR_LEN); i++) {

      /* Search for pmu nodes*/
      offset = fdt_node_offset_by_compatible((const void *)dt_ptr, -1, pmu_dt_arr[i]);
      if (offset < 0) {
          bsa_print(ACS_PRINT_DEBUG, L"PMU compatible value not found for index:%d\n", i);
          continue; /* Search for next compatible item*/
      }

      while (offset != -FDT_ERR_NOTFOUND) {
          /* Get interrupts property from frame */
          Pintr = (UINT32 *)
                    fdt_getprop_namelen((void *)dt_ptr, offset, "interrupts", 10, &prop_len);
          if ((prop_len < 0) || (Pintr == NULL)) {
              bsa_print(ACS_PRINT_ERR, L" PROPERTY interrupts offset %x, Error %d\n",
                        offset, prop_len);
              return;
          }

          interrupt_cell = fdt_interrupt_cells((const void *)dt_ptr, offset);
          bsa_print(ACS_PRINT_DEBUG, L" interrupt_cell  %d\n", interrupt_cell);
          if (interrupt_cell < 1 || interrupt_cell > 3) {
              bsa_print(ACS_PRINT_ERR, L" Invalid interrupt cell : %d \n", interrupt_cell);
              return;
          }

          interrupt_frame_count = ((prop_len/sizeof(int))/interrupt_cell);
          bsa_print(ACS_PRINT_DEBUG, L" interrupt frame count : %d \n", interrupt_frame_count);

          if (interrupt_frame_count == 0) {
              bsa_print(ACS_PRINT_ERR, L" interrupt_frame_count is invalid\n");
              return;
          }
          /* Handle Single PMU node with Single PPI or SPI */
          if (interrupt_frame_count == 1) {
              for (i = 0; i < PeTable->header.num_of_pe; i++) {
                  if (interrupt_cell == 3) {
                      if (Pintr[0])
                          Ptr->pmu_gsiv = fdt32_to_cpu(Pintr[1]) + PPI_OFFSET;
                      else
                          Ptr->pmu_gsiv = fdt32_to_cpu(Pintr[1]) + SPI_OFFSET;
                  }
                  else
                    Ptr->pmu_gsiv = fdt32_to_cpu(Pintr[0]);
                  Ptr++;
              }
              return;
          }

          /* Handle Multiple PMU node with multiple SPI frames
           * the pmu_gsiv should be in same order of CPU nodes */
          for (i = 0; i < interrupt_frame_count; i++) {
              if (interrupt_cell == 3) {
                  if (Pintr[index++])
                      Ptr->pmu_gsiv = fdt32_to_cpu(Pintr[index++]) + PPI_OFFSET;
                  else
                      Ptr->pmu_gsiv = fdt32_to_cpu(Pintr[index++]) + SPI_OFFSET;
                  index++; /*Skip flag*/
              } else if (interrupt_cell == 2) {
                  Ptr->pmu_gsiv = fdt32_to_cpu(Pintr[index++]);
                  index++; /*Skip flag*/
              } else
                Ptr->pmu_gsiv = fdt32_to_cpu(Pintr[index++]);

              Ptr++;
          }

          offset =
              fdt_node_offset_by_compatible((const void *)dt_ptr, offset, pmu_dt_arr[i]);
      }
  }
}

/**
  @brief  This API fills in the PE_INFO Table with information about the PEs in the
          system. This is achieved by parsing the DT blob.

  @param  PeTable  - Address where the PE information needs to be filled.

  @return  None
**/
VOID
pal_pe_create_info_table_dt(PE_INFO_TABLE *PeTable)
{
  PE_INFO_ENTRY *Ptr = NULL;
  UINT64 dt_ptr;
  UINT32 *prop_val;
  UINT32 reg_val[2];
  UINT64 MpidrAff0Max = 0, MpidrAff1Max = 0, MpidrAff2Max = 0, MpidrAff3Max = 0;
  int prop_len, addr_cell, size_cell;
  int offset, parent_offset;


  dt_ptr = pal_get_dt_ptr();
  if (dt_ptr == 0) {
    bsa_print(ACS_PRINT_ERR, L" dt_ptr is NULL\n");
    return;
  }

  PeTable->header.num_of_pe = 0;

  /* Find the first cpu node offset in DT blob */
  offset = fdt_node_offset_by_prop_value((const void *) dt_ptr, -1, "device_type", "cpu", 4);

  if (offset != -FDT_ERR_NOTFOUND) {
      parent_offset = fdt_parent_offset((const void *) dt_ptr, offset);
      bsa_print(ACS_PRINT_DEBUG, L" NODE cpu offset %d\n", offset);

      size_cell = fdt_size_cells((const void *) dt_ptr, parent_offset);
      bsa_print(ACS_PRINT_DEBUG, L" NODE cpu size cell %d\n", size_cell);
      if (size_cell != 0) {
        bsa_print(ACS_PRINT_ERR, L" Invalid size cell for node cpu\n");
        return;
      }

      addr_cell = fdt_address_cells((const void *) dt_ptr, parent_offset);
      bsa_print(ACS_PRINT_DEBUG, L" NODE cpu  addr cell %d\n", addr_cell);
      if (addr_cell <= 0 || addr_cell > 2) {
        bsa_print(ACS_PRINT_ERR, L" Invalid address cell for node cpu\n");
        return;
      }
  } else {
        bsa_print(ACS_PRINT_ERR, L" No CPU node found \n");
        return;
  }

  Ptr = PeTable->pe_info;

  /* Perform a DT traversal till all cpu node are parsed */
  while (offset != -FDT_ERR_NOTFOUND) {
      bsa_print(ACS_PRINT_DEBUG, L" SUBNODE cpu%d offset %x\n", PeTable->header.num_of_pe, offset);

      prop_val = (UINT32 *)fdt_getprop_namelen((void *)dt_ptr, offset, "reg", 3, &prop_len);
      if ((prop_len < 0) || (prop_val == NULL)) {
        bsa_print(ACS_PRINT_ERR, L" PROPERTY reg offset %x, Error %d\n", offset, prop_len);
        return;
      }

      reg_val[0] = fdt32_to_cpu(prop_val[0]);
      bsa_print(ACS_PRINT_DEBUG, L" reg_val<0> = %x\n", reg_val[0]);
      if (addr_cell == 2) {
        reg_val[1] = fdt32_to_cpu(prop_val[1]);
        bsa_print(ACS_PRINT_DEBUG, L" reg_val<1> = %x\n", reg_val[1]);
        Ptr->mpidr = (((INT64)(reg_val[0] & PROPERTY_MASK_PE_AFF3) << 32) |
                     (reg_val[1] & PROPERTY_MASK_PE_AFF0_AFF2));
      } else {
        Ptr->mpidr = (reg_val[0] & PROPERTY_MASK_PE_AFF0_AFF2);
      }

      Ptr->pe_num   = PeTable->header.num_of_pe;
      pal_pe_data_cache_ops_by_va((UINT64)Ptr, CLEAN_AND_INVALIDATE);
      PeTable->header.num_of_pe++;

      MpidrAff0Max = UPDATE_AFF_MAX(MpidrAff0Max, Ptr->mpidr, 0x000000ff);
      MpidrAff1Max = UPDATE_AFF_MAX(MpidrAff1Max, Ptr->mpidr, 0x0000ff00);
      MpidrAff2Max = UPDATE_AFF_MAX(MpidrAff2Max, Ptr->mpidr, 0x00ff0000);
      MpidrAff3Max = UPDATE_AFF_MAX(MpidrAff3Max, Ptr->mpidr, 0xff00000000);
      Ptr++;

      offset =
          fdt_node_offset_by_prop_value((const void *) dt_ptr, offset, "device_type", "cpu", 4);
  }
  gMpidrMax = MpidrAff0Max | MpidrAff1Max | MpidrAff2Max | MpidrAff3Max;
  g_num_pe = PeTable->header.num_of_pe;
  pal_pe_info_table_pmu_gsiv_dt(PeTable);
  pal_pe_info_table_gmaint_gsiv_dt(PeTable);
  pal_pe_data_cache_ops_by_va((UINT64)PeTable, CLEAN_AND_INVALIDATE);
  pal_pe_data_cache_ops_by_va((UINT64)&gMpidrMax, CLEAN_AND_INVALIDATE);
  PalAllocateSecondaryStack(gMpidrMax);

  dt_dump_pe_table(PeTable);
}
