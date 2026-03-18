/** @file
 * Copyright (c) 2016-2018, 2021-2023, Arm Limited or its affiliates. All rights reserved.
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

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 4)
#define TEST_RULE  "MF_ECM_070_010"
#define TEST_DESC  "Check RootPort configuration RRS software visibility       "

static
void
payload(void)
{

  uint32_t bdf;
  uint32_t dp_type;
  uint32_t hart_index;
  uint32_t tbl_index;
  uint32_t cap_base;
  uint16_t reg_value;
  uint32_t test_skip = 1;
  pcie_device_bdf_table *bdf_tbl_ptr;

  tbl_index = 0;
  bdf_tbl_ptr = val_pcie_bdf_table_ptr();
  hart_index = val_hart_get_index_mpid(val_hart_get_mpid());

  tbl_index = 0;
  while (tbl_index < bdf_tbl_ptr->num_entries)
  {
      bdf = bdf_tbl_ptr->device[tbl_index++].bdf;
      dp_type = val_pcie_device_port_type(bdf);

      if (dp_type == RP)
      {
        test_skip = 0;
        /* check CSR support state */
        val_pcie_find_capability(bdf, PCIE_CAP, CID_PCIECS, &cap_base);
        val_pcie_read_cfg_width(bdf, cap_base + ROOTCP_OFFSET, &reg_value, PCI_WIDTH_UINT16);
        if ((reg_value & 0x01) != 0x01) {
           val_print(ACS_PRINT_DEBUG,     "\n       RRS software visibility is not enabled on RP: 0x%x", bdf);
           val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
           return;
        }
      }
  }

  if (test_skip == 1) {
      val_print(ACS_PRINT_DEBUG,
        "\n       No RP type device found.", 0);
      val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  }
  else
      val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_p004_entry(uint32_t num_hart)
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
