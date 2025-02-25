/** @file
 * Copyright (c) 2021, Arm Limited or its affiliates. All rights reserved.
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

#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"

#include "val/include/bsa_acs_pcie.h"
#include "val/include/bsa_acs_pe.h"
#include "val/include/bsa_acs_memory.h"

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 60)
#define TEST_DESC  "PCI_IN_16: Check all 1's for out of range          "

/* Returns the maximum bdf value for that segment from bdf table */
static uint32_t get_max_bdf(uint32_t segment)
{
  pcie_device_bdf_table *bdf_tbl_ptr;
  uint32_t seg_num;
  uint32_t bdf;
  uint32_t tbl_index = 0;
  uint32_t max_bdf = 0;

  bdf_tbl_ptr = val_pcie_bdf_table_ptr();
  for (tbl_index = 0; tbl_index < bdf_tbl_ptr->num_entries; tbl_index++)
  {
      bdf = bdf_tbl_ptr->device[tbl_index].bdf;
      seg_num = PCIE_EXTRACT_BDF_SEG(bdf);

      if (segment == seg_num)
          max_bdf = bdf;
  }

  return max_bdf;
}

static
void
payload(void)
{

  uint32_t reg_value;
  uint32_t pe_index;
  uint32_t ecam_index;
  uint32_t num_ecam;
  uint32_t end_bus;
  uint32_t cfg_addr;
  uint32_t bus_index;
  uint32_t dev_index;
  uint32_t func_index;
  uint32_t bdf;
  uint32_t segment = 0;
  addr_t   ecam_base = 0;

  pe_index = val_pe_get_index_mpid(val_pe_get_mpid());
  num_ecam = val_pcie_get_info(PCIE_INFO_NUM_ECAM, 0);

  for (ecam_index = 0; ecam_index < num_ecam; ecam_index++)
  {
      /* Get the maximum bus value from PCIe info table */
      end_bus = val_pcie_get_info(PCIE_INFO_END_BUS, ecam_index);
      segment = val_pcie_get_info(PCIE_INFO_SEGMENT, ecam_index);
      val_set_status(pe_index, RESULT_SKIP(TEST_NUM, 01));

      /* Get the highest BDF value for that segment */
      bdf = get_max_bdf(segment);

      /* Get the least highest of max bus number */
      bus_index = (PCIE_EXTRACT_BDF_BUS(bdf) < end_bus) ? PCIE_EXTRACT_BDF_BUS(bdf):end_bus;
      val_print(ACS_PRINT_INFO, "Maximum bus value is 0x%x", bus_index);
      bus_index += 1;

      /* Bus value should not exceed 255 */
      if (bus_index > 255) {
          val_print(ACS_PRINT_ERR, "\n Bus index exceeded maximum value", 0);
          return;
      }

      for (dev_index = 0; dev_index < PCIE_MAX_DEV; dev_index++)
      {
          for (func_index = 0; func_index < PCIE_MAX_FUNC; func_index++)
          {
              /* Form bdf using seg, bus, device, function numbers.
               * This BDF does not fall into the secondary and subordinate
               * bus of any of the rootports because the bus value is one
               * greater than the higest bus value. This BDF also doesn't
               * match any of the existing BDF.
               */
              bdf = PCIE_CREATE_BDF(segment, bus_index, dev_index, func_index);
              ecam_base = val_pcie_get_info(PCIE_INFO_ECAM, ecam_index);

              if (ecam_base == 0) {
                  val_print(ACS_PRINT_ERR, "\n       ECAM Base is zero ", 0);
                  val_set_status(pe_index, RESULT_FAIL(TEST_NUM, 01));
                  return;
              }

              /* There are 8 functions / device, 32 devices / Bus and each has a 4KB config space */
              cfg_addr = (bus_index * PCIE_MAX_DEV * PCIE_MAX_FUNC * 4096) + \
                         (dev_index * PCIE_MAX_FUNC * 4096) + (func_index * 4096);

              val_print(ACS_PRINT_INFO, "\n       Calculated config address is %lx",
                                                  ecam_base + cfg_addr + TYPE01_VIDR);
              reg_value = pal_mmio_read(ecam_base + cfg_addr + TYPE01_VIDR);
              if (reg_value != PCIE_UNKNOWN_RESPONSE)
              {
                  val_print(ACS_PRINT_ERR, "\n       Failed for BDF: 0x%x", bdf);
                  val_set_status(pe_index, RESULT_FAIL(TEST_NUM, 02));
                  return;
              }

              val_set_status(pe_index, RESULT_PASS(TEST_NUM, 01));
          }
      }
  }
}

uint32_t
os_p060_entry(uint32_t num_pe)
{

  uint32_t status = ACS_STATUS_FAIL;

  num_pe = 1;  //This test is run on single processor

  status = val_initialize_test(TEST_NUM, TEST_DESC, num_pe);
  if (status != ACS_STATUS_SKIP)
      val_run_test_payload(TEST_NUM, num_pe, payload, 0);

  /* get the result from single PE and check for failure */
  status = val_check_for_error(TEST_NUM, num_pe);

  val_report_status(0, BSA_ACS_END(TEST_NUM));

  return status;
}
