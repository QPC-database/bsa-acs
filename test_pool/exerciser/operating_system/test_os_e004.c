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

#include "val/include/bsa_acs_pcie.h"
#include "val/include/bsa_acs_memory.h"
#include "val/include/bsa_acs_iovirt.h"
#include "val/include/bsa_acs_smmu.h"
#include "val/include/bsa_acs_pcie_enumeration.h"
#include "val/include/bsa_acs_exerciser.h"

#define TEST_NUM   (ACS_EXERCISER_TEST_NUM_BASE + 4)
#define TEST_DESC  "PCI_MSI_2: MSI(-X) triggers interrupt with unique ID  "

static uint32_t irq_pending;
static uint32_t lpi_int_id = 0x204C;

static
void
intr_handler(void)
{
  /* Clear the interrupt pending state */
  irq_pending = 0;

  val_print(ACS_PRINT_INFO, "\n       Received MSI interrupt %x       ", lpi_int_id);
  val_gic_end_of_interrupt(lpi_int_id);
  return;
}

static
void
payload (void)
{

  uint32_t index;
  uint32_t e_bdf = 0;
  uint32_t timeout;
  uint32_t status;
  uint32_t instance;
  uint32_t num_cards;
  uint32_t num_smmus;
  uint32_t msi_index = 0;

  index = val_pe_get_index_mpid (val_pe_get_mpid());

  /* Read the number of excerciser cards */
  num_cards = val_exerciser_get_info(EXERCISER_NUM_CARDS, 0);

  /* Disable all SMMUs */
  num_smmus = val_iovirt_get_smmu_info(SMMU_NUM_CTRL, 0);
  for (instance = 0; instance < num_smmus; ++instance)
     val_smmu_disable(instance);

  for (instance = 0; instance < num_cards; instance++)
  {

    /* if init fail moves to next exerciser */
    if (val_exerciser_init(instance))
        continue;

    /* Get the exerciser BDF */
    e_bdf = val_exerciser_get_bdf(instance);

    status = val_gic_request_msi(e_bdf, lpi_int_id, msi_index);

    if (status) {
        val_print(ACS_PRINT_ERR,
            "\n       MSI Assignment failed for bdf : 0x%x", e_bdf);
        val_set_status(index, RESULT_FAIL(TEST_NUM, 01));
        return;
    }

    status = val_gic_install_isr(lpi_int_id, intr_handler);

    if (status) {
        val_print(ACS_PRINT_ERR,
            "\n       Intr handler registration failed for Interrupt : 0x%x", lpi_int_id);
        val_set_status(index, RESULT_FAIL(TEST_NUM, 02));
        return;
    }

    /* Set the interrupt trigger status to pending */
    irq_pending = 1;

    /* Trigger the interrupt */
    val_exerciser_ops(GENERATE_MSI, msi_index, instance);

    /* PE busy polls to check the completion of interrupt service routine */
    timeout = TIMEOUT_LARGE;
    while ((--timeout > 0) && irq_pending)
        {};

    if (timeout == 0) {
        val_print(ACS_PRINT_ERR,
            "\n       Interrupt trigger failed for : 0x%x, ", lpi_int_id);
        val_print(ACS_PRINT_ERR,
            "BDF : 0x%x   ", e_bdf);
        val_set_status(index, RESULT_FAIL(TEST_NUM, 03));
        val_gic_free_msi(e_bdf, lpi_int_id, msi_index);
        return;
    }

    /* Clear Interrupt and Mappings */
    val_gic_free_msi(e_bdf, lpi_int_id, msi_index);

  }

  /* Pass Test */
  val_set_status(index, RESULT_PASS(TEST_NUM, 01));

}

uint32_t
os_e004_entry(void)
{

  uint32_t status = ACS_STATUS_FAIL;

  uint32_t num_pe = 1;  //This test is run on single processor

  status = val_initialize_test(TEST_NUM, TEST_DESC, num_pe);
  if (status != ACS_STATUS_SKIP)
      val_run_test_payload(TEST_NUM, num_pe, payload, 0);

  /* get the result from all PE and check for failure */
  status = val_check_for_error(TEST_NUM, num_pe);

  val_report_status(0, BSA_ACS_END(TEST_NUM));

  return status;
}
