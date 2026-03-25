/** @file
 * Copyright (c) 2019-2020, Arm Limited or its affiliates. All rights reserved.
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

#include "val/include/bsa_acs_hart.h"
#include "val/include/bsa_acs_pcie.h"
#include "val/include/bsa_acs_memory.h"

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 20)
#define TEST_RULE  "ME_AER_070_010, ME_SID_100_010"
#define TEST_DESC  "Check RCEC device is fully function and correctly associated to RCiEP          "

static
uint32_t
check_associated_rciep_support_aer(uint32_t bdf, uint32_t bit_map, uint32_t next_bus, uint32_t last_bus)
{
  uint32_t index;
  uint32_t rcec_seg_num;
  uint32_t rcec_bus_num;
  uint32_t dev_bdf;
  uint32_t bus_num;
  uint32_t cap_base = 0;

  rcec_seg_num = PCIE_EXTRACT_BDF_SEG(bdf);
  rcec_bus_num = PCIE_EXTRACT_BDF_BUS(bdf);
  /* Check associated RCiEP under the same bus of RCEC */
  for (index = 0; index < PCIE_MAX_DEV; index++)
  {
    if ((1 << index) & bit_map) {
      dev_bdf = PCIE_CREATE_BDF(rcec_seg_num, rcec_bus_num, index, 0);
      if (val_pcie_device_port_type(dev_bdf) == RCiEP) {
        /* If AER Not Supported, Fail. */
        if (val_pcie_find_capability(dev_bdf, PCIE_ECAP, ECID_AER, &cap_base) != PCIE_SUCCESS) {
            val_print(ACS_PRINT_ERR, "\n       BDF 0x%x: AER Cap unsupported", dev_bdf);
            return 0;
        }
      }
    }
  }
  /* Check associated RCiEP not under the same bus of RCEC */
  if (last_bus >= next_bus) {
    for (bus_num = next_bus; bus_num <= last_bus; bus_num++)
    {
      for (index = 0; index < PCIE_MAX_DEV; index++)
      {
          dev_bdf = PCIE_CREATE_BDF(rcec_seg_num, bus_num, index, 0);
          if (val_pcie_device_port_type(dev_bdf) == RCiEP) {
            /* If AER Not Supported, Fail. */
            if (val_pcie_find_capability(dev_bdf, PCIE_ECAP, ECID_AER, &cap_base) != PCIE_SUCCESS) {
                val_print(ACS_PRINT_ERR, "\n       BDF 0x%x: AER Cap unsupported", dev_bdf);
                return 0;
            }
          }
      }
    }
  }

  return 1;
}

static
void
payload(void)
{

  uint32_t bdf;
  uint32_t hart_index;
  uint32_t tbl_index;
  uint32_t dp_type;
  uint32_t cap_base = 0;
  uint32_t reg_value;
  uint32_t rcec_ver;
  uint32_t rcec_bitmap;
  uint32_t rcec_next_bus;
  uint32_t rcec_last_bus;
  uint32_t ret;
  uint32_t rcec_dev_num;
  uint32_t test_fails;
  uint32_t test_skip = 1;
  pcie_device_bdf_table *bdf_tbl_ptr;

  hart_index = val_hart_get_index_mpid(val_hart_get_mpid());

  bdf_tbl_ptr = val_pcie_bdf_table_ptr();
  test_fails = 0;

  /* Check for all the function present in bdf table */
  for (tbl_index = 0; tbl_index < bdf_tbl_ptr->num_entries; tbl_index++)
  {
      bdf = bdf_tbl_ptr->device[tbl_index].bdf;
      dp_type = val_pcie_device_port_type(bdf);

      /* Check entry is RCEC */
      if (dp_type == RCEC)
      {
          val_print(ACS_PRINT_DEBUG, "\n       BDF - 0x%x", bdf);
          /* Test runs for atleast an endpoint */
          test_skip = 0;

          /* Read the RCECEA Capability. */
          if (val_pcie_find_capability(bdf, PCIE_ECAP, ECID_RCECEA, &cap_base) != PCIE_SUCCESS) {
              val_print(ACS_PRINT_ERR,
                "\n       BDF - 0x%x does not support RCEC Endpoint Association Capability", bdf);
              test_fails++;
              continue;
          }

          /* Read RCEC cap version */
          val_pcie_read_cfg(bdf, RCEC_EA_CAP_OFFSET, &reg_value);
          rcec_ver = VAL_EXTRACT_BITS(reg_value, RCEC_EA_CAP_VER_SHIFT, RCEC_EA_CAP_VER_SHIFT + 4);
          val_print(ACS_PRINT_DEBUG, "\n RCEC cap version %d", rcec_ver);
          /* Read Association Bitmap for RCiEP */
          val_pcie_read_cfg(bdf, RCEC_EA_BITMAP_OFFSET, &rcec_bitmap);
          val_print(ACS_PRINT_DEBUG, "\n RCEC Association Bitmap for RCiEP %d", rcec_bitmap);
          /* If RCEC cap version > 2h，read Association Bus Numbers for RCiEP*/
          if (rcec_ver > 2) {
            val_pcie_read_cfg(bdf, RCEC_EA_BUS_OFFSET, &reg_value);
            rcec_next_bus = VAL_EXTRACT_BITS(reg_value, RCEC_EA_NEXT_BUS_SHIFT, RCEC_EA_NEXT_BUS_SHIFT + 8);
            rcec_last_bus = VAL_EXTRACT_BITS(reg_value, RCEC_EA_LAST_BUS_SHIFT, RCEC_EA_LAST_BUS_SHIFT + 8);
            val_print(ACS_PRINT_DEBUG, "\n Association Next Bus Numbers for RCiEP %d", rcec_next_bus);
            val_print(ACS_PRINT_DEBUG, " Association Bus Last Numbers for RCiEP %d", rcec_last_bus);
          }

          // Check if associated RCiEP support AER capability. if not, fail.
          rcec_dev_num = PCIE_EXTRACT_BDF_DEV(bdf);
          if (((1 << rcec_dev_num) == rcec_bitmap) && (rcec_next_bus == 0xFF) && (rcec_last_bus == 0)) {
            val_print(ACS_PRINT_DEBUG, "\n RCEC BDF %d has no associated RCiEP", bdf);
            continue;
          } else {
            ret = check_associated_rciep_support_aer(bdf, rcec_bitmap, rcec_next_bus, rcec_last_bus);
            if (ret == 0) {
              test_fails++;
              continue;
            }
          }
      }
  }

  if (test_skip == 1) {
      val_print(ACS_PRINT_DEBUG, "\n       No RCEC type device found, Skipping test", 0);
      val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 2));
  }
  else if (test_fails)
      val_set_status(hart_index, RESULT_FAIL(TEST_NUM, test_fails));
  else
      val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_p020_entry(uint32_t num_hart)
{

  uint32_t status = ACS_STATUS_FAIL;

  num_hart = 1;  //This test is run on single processor

  status = val_initialize_test(TEST_NUM, TEST_DESC, num_hart);
  if (status != ACS_STATUS_SKIP)
      val_run_test_payload(TEST_NUM, num_hart, payload, 0);

  /* get the result from all HART and check for failure */
  status = val_check_for_error(TEST_NUM, num_hart, TEST_RULE);

  val_report_status(0, BSA_ACS_END(TEST_NUM), NULL);

  return status;
}
