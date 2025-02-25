/** @file
 * Copyright (c) 2016-2020,2021 Arm Limited or its affiliates. All rights reserved.
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

#include "include/bsa_acs_val.h"
#include "include/bsa_acs_exerciser.h"
#include "include/bsa_acs_pcie.h"
#include "include/bsa_acs_smmu.h"

EXERCISER_INFO_TABLE g_exercier_info_table;
/**
  @brief   This API popultaes information from all the PCIe stimulus generation IP available
           in the system into exerciser_info_table structure
  @param   exerciser_info_table - Table pointer to be filled by this API
  @return  exerciser_info_table - Contains info to communicate with stimulus generation hardware
**/
void val_exerciser_create_info_table(void)
{
  uint32_t Bdf;
  uint32_t reg_value;
  uint32_t num_bdf;
  pcie_device_bdf_table *bdf_table;

  bdf_table = val_pcie_bdf_table_ptr();
  /* if no bdf table ptr return error */
  if (bdf_table->num_entries == 0)
  {
      val_print(ACS_PRINT_DEBUG, "\n       No BDFs discovered            ", 0);
      return;
  }

  num_bdf = bdf_table->num_entries;
  while (num_bdf-- != 0)
  {

      Bdf = bdf_table->device[num_bdf].bdf;
      /* Probe pcie device Function with this bdf */
      if (val_pcie_read_cfg(Bdf, TYPE01_VIDR, &reg_value) == PCIE_NO_MAPPING)
      {
          /* Return if there is a bdf mapping issue */
          val_print(ACS_PRINT_ERR, "\n      BDF 0x%x mapping issue", Bdf);
          return;
      }

      /* Store the Function's BDF if there was a valid response */
      if (pal_is_bdf_exerciser(Bdf))
      {
          g_exercier_info_table.e_info[g_exercier_info_table.num_exerciser].bdf = Bdf;
          g_exercier_info_table.e_info[g_exercier_info_table.num_exerciser++].initialized = 0;
          val_print(ACS_PRINT_DEBUG, "    exerciser Bdf %x\n", Bdf);
      }
  }
  val_print(ACS_PRINT_DEBUG, "    exerciser cards in the system %x \n",
            g_exercier_info_table.num_exerciser);
}


/**
  @brief   This API returns the requested information about the PCIe stimulus hardware
  @param   type         - Information type required from the stimulus hadrware
  @param   instance     - Stimulus hadrware instance number
  @return  value        - Information value for input type
**/
uint32_t val_exerciser_get_info(EXERCISER_INFO_TYPE type, uint32_t instance)
{
    switch (type) {
    case EXERCISER_NUM_CARDS:
         return g_exercier_info_table.num_exerciser;
    default:
         return 0;
    }
}

/**
  @brief   This API writes the configuration parameters of the PCIe stimulus generation hardware
  @param   type         - Parameter type that needs to be set in the stimulus hadrware
  @param   value1       - Parameter 1 that needs to be set
  @param   value2       - Parameter 2 that needs to be set
  @param   instance     - Stimulus hardware instance number
  @return  status       - SUCCESS if the input paramter type is successfully written
**/
uint32_t val_exerciser_set_param(EXERCISER_PARAM_TYPE type, uint64_t value1, uint64_t value2,
                                 uint32_t instance)
{
    return pal_exerciser_set_param(type, value1, value2,
                                   g_exercier_info_table.e_info[instance].bdf);
}

uint32_t val_exerciser_get_bdf(uint32_t instance)
{
    return g_exercier_info_table.e_info[instance].bdf;
}
/**
  @brief   This API reads the configuration parameters of the PCIe stimulus generation hardware
  @param   type         - Parameter type that needs to be read from the stimulus hadrware
  @param   value1       - Parameter 1 that is read from hardware
  @param   value2       - Parameter 2 that is read from hardware
  @param   instance     - Stimulus hardware instance number
  @return  status       - SUCCESS if the requested paramter type is successfully read
**/
uint32_t val_exerciser_get_param(EXERCISER_PARAM_TYPE type, uint64_t *value1, uint64_t *value2,
                                 uint32_t instance)
{
    return pal_exerciser_get_param(type, value1, value2,
                                   g_exercier_info_table.e_info[instance].bdf);

}

/**
  @brief   This API sets the state of the PCIe stimulus generation hardware
  @param   state        - State that needs to be set for the stimulus hadrware
  @param   value        - Additional information associated with the state
  @param   instance     - Stimulus hardware instance number
  @return  status       - SUCCESS if the input state is successfully written to hardware
**/
uint32_t val_exerciser_set_state(EXERCISER_STATE state, uint64_t *value, uint32_t instance)
{
    return pal_exerciser_set_state(state, value, g_exercier_info_table.e_info[instance].bdf);
}

/**
  @brief   This API obtains the state of the PCIe stimulus generation hardware
  @param   state        - State that is read from the stimulus hadrware
  @param   instance     - Stimulus hardware instance number
  @return  status       - SUCCESS if the state is successfully read from hardware
**/
uint32_t val_exerciser_get_state(EXERCISER_STATE *state, uint32_t instance)
{
    return pal_exerciser_get_state(state, g_exercier_info_table.e_info[instance].bdf);
}

/**
  @brief   This API obtains initializes
  @param   instance     - Stimulus hardware instance number
  @return  status       - SUCCESS if the state is successfully read from hardware
**/
uint32_t val_exerciser_init(uint32_t instance)
{
  uint32_t Bdf;
  uint32_t Ecam;
  uint64_t cfg_addr;
  EXERCISER_STATE state;

  if (!g_exercier_info_table.e_info[instance].initialized)
  {
      Bdf = g_exercier_info_table.e_info[instance].bdf;
      if (pal_exerciser_get_state(&state, Bdf) || (state != EXERCISER_ON)) {
          val_print(ACS_PRINT_ERR, "\n   Exerciser Bdf %lx not ready", Bdf);
          return 1;
      }

      // setting command register for Memory Space Enable and Bus Master Enable
      Ecam = val_pcie_get_ecam_base(Bdf);

      /* There are 8 functions / device, 32 devices / Bus and each has a 4KB config space */
      cfg_addr = (PCIE_EXTRACT_BDF_BUS(Bdf) * PCIE_MAX_DEV * PCIE_MAX_FUNC * 4096) + \
                 (PCIE_EXTRACT_BDF_DEV(Bdf) * PCIE_MAX_FUNC * 4096) + \
                 (PCIE_EXTRACT_BDF_FUNC(Bdf) * 4096);

      pal_mmio_write((Ecam + cfg_addr + COMMAND_REG_OFFSET),
                  (pal_mmio_read((Ecam + cfg_addr) + COMMAND_REG_OFFSET) | BUS_MEM_EN_MASK));

      g_exercier_info_table.e_info[instance].initialized = 1;
  }
  else
           val_print(ACS_PRINT_DEBUG, "\n  Already initialized %d", instance);
  return 0;
}
/**
  @brief   This API performs the input operation using the PCIe stimulus generation hardware
  @param   ops          - Operation that needs to be performed with the stimulus hadrware
  @param   value        - Additional information to perform the operation
  @param   instance     - Stimulus hardware instance number
  @return  status       - SUCCESS if the operation is successfully performed using the hardware
**/
uint32_t val_exerciser_ops(EXERCISER_OPS ops, uint64_t param, uint32_t instance)
{
    return pal_exerciser_ops(ops, param, g_exercier_info_table.e_info[instance].bdf);
}

/**
  @brief   This API returns test specific data from the PCIe stimulus generation hardware
  @param   type         - data type for which the data needs to be returned
  @param   data         - test specific data to be be filled by pal layer
  @param   instance     - Stimulus hardware instance number
  @return  status       - SUCCESS if the requested data is successfully filled
**/
uint32_t val_exerciser_get_data(EXERCISER_DATA_TYPE type, exerciser_data_t *data,
                                uint32_t instance)
{
    uint32_t bdf = g_exercier_info_table.e_info[instance].bdf;
    uint64_t ecam = val_pcie_get_ecam_base(bdf);
    return pal_exerciser_get_data(type, data, bdf, ecam);
}

/**
  @brief   This API executes all the Exerciser tests sequentially
           1. Caller       -  Application layer.
  @return  Consolidated status of all the tests run.
**/
uint32_t
val_exerciser_execute_tests(uint32_t *g_sw_view)
{
  uint32_t status, i;
  uint32_t num_instances;

  status = ACS_STATUS_PASS;

  for (i = 0; i < MAX_TEST_SKIP_NUM; i++){
      if (g_skip_test_num[i] == ACS_EXERCISER_TEST_NUM_BASE) {
          val_print(ACS_PRINT_TEST, "\n      USER Override - Skipping all Exerciser tests \n", 0);
          return ACS_STATUS_SKIP;
      }
  }

  /* Create the list of valid Pcie Device Functions */
  if (val_pcie_create_device_bdf_table()) {
      val_print(ACS_PRINT_WARN, "\n     Create BDF Table Failed, Skipping Exerciser tests...\n", 0);
      return ACS_STATUS_SKIP;
  }

  val_exerciser_create_info_table();
  num_instances = val_exerciser_get_info(EXERCISER_NUM_CARDS, 0);

  if (num_instances == 0) {
      val_print(ACS_PRINT_WARN, "\n     No Exerciser Devices Found, Skipping tests...\n", 0);
      return ACS_STATUS_SKIP;
  }

  if (g_sw_view[G_SW_OS]) {
     val_print(ACS_PRINT_ERR, "\nOperating System:\n", 0);

     status |= os_e001_entry();
     status |= os_e004_entry();
     status |= os_e005_entry();
     status |= os_e006_entry();
     status |= os_e012_entry();
     status |= os_e013_entry();
     status |= os_e015_entry();

  }

  if (status != ACS_STATUS_PASS)
    val_print(ACS_PRINT_TEST, "\n      *** One or more tests have Failed/Skipped.*** \n", 0);
  else
    val_print(ACS_PRINT_TEST, "\n      All Exerciser tests passed!! \n", 0);

  return status;
}

