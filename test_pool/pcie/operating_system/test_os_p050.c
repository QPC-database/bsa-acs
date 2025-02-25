/** @file
 * Copyright (c) 2016-2018, 2021, Arm Limited or its affiliates. All rights reserved.
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

#include "val/include/bsa_acs_pe.h"
#include "val/include/bsa_acs_pcie.h"
#include "val/include/bsa_acs_memory.h"

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 50)
#define TEST_DESC  "PCI_LI_03: Legacy SPI are programme as lvl-sensitiv"

static
void
payload(void)
{
  uint32_t status;
  uint32_t bdf;
  uint32_t pe_index;
  uint32_t tbl_index;
  uint32_t reg_value;
  uint32_t test_skip = 1;
  uint32_t intr_pin, intr_line;
  PERIPHERAL_IRQ_MAP *intr_map;
  pcie_device_bdf_table *bdf_tbl_ptr;
  INTR_TRIGGER_INFO_TYPE_e trigger_type;

  pe_index = val_pe_get_index_mpid(val_pe_get_mpid());

  /* Allocate memory for interrupt mappings */
  intr_map = val_memory_alloc(sizeof(PERIPHERAL_IRQ_MAP));
  if (!intr_map) {
    val_print (ACS_PRINT_ERR, "\n       Memory allocation error", 0);
    val_set_status(pe_index, RESULT_FAIL (TEST_NUM, 01));
    return;
  }

  tbl_index = 0;
  bdf_tbl_ptr = val_pcie_bdf_table_ptr();

  while (tbl_index < bdf_tbl_ptr->num_entries)
  {
      bdf = bdf_tbl_ptr->device[tbl_index++].bdf;

      /* Read Interrupt Line Register */
      val_pcie_read_cfg(bdf, TYPE01_ILR, &reg_value);

      intr_pin = (reg_value >> TYPE01_IPR_SHIFT) & TYPE01_IPR_MASK;
      if ((intr_pin == 0) || (intr_pin > 0x4))
        continue;

      status = val_pci_get_legacy_irq_map(bdf, intr_map);
      if (!status) {
        // Skip the test if the Legacy IRQ map does not exist
        val_set_status(pe_index, RESULT_SKIP(TEST_NUM, 02));
        return;
      }

      /* If test runs for atleast an endpoint */
      test_skip = 0;

      intr_line = intr_map->legacy_irq_map[intr_pin-1].irq_list[0];

      /* Read GICD_ICFGR register to Check for Level/Edge Sensitive. */
      status = val_gic_get_intr_trigger_type(intr_line, &trigger_type);
      if (status) {
        val_set_status(pe_index, RESULT_FAIL(TEST_NUM, 03));
        return;
      }

      if (trigger_type != INTR_TRIGGER_INFO_LEVEL_HIGH) {
        val_set_status(pe_index, RESULT_FAIL(TEST_NUM, 04));
        return;
      }
  }

  if (test_skip == 1)
      val_set_status(pe_index, RESULT_SKIP(TEST_NUM, 01));
  else
      val_set_status(pe_index, RESULT_PASS(TEST_NUM, 01));
}

uint32_t
os_p050_entry(uint32_t num_pe)
{

  uint32_t status = ACS_STATUS_FAIL;

  num_pe = 1;  //This test is run on single processor

  status = val_initialize_test(TEST_NUM, TEST_DESC, num_pe);
  if (status != ACS_STATUS_SKIP)
      val_run_test_payload(TEST_NUM, num_pe, payload, 0);

  /* get the result from all PE and check for failure */
  status = val_check_for_error(TEST_NUM, num_pe);

  val_report_status(0, BSA_ACS_END(TEST_NUM));

  return status;
}
