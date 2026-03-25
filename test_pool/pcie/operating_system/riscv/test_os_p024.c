/** @file
 * Copyright (c) 2019-2020,2021 Arm Limited or its affiliates. All rights reserved.
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
#include "val/include/bsa_acs_hart.h"
#include "val/include/bsa_acs_memory.h"

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 24)
#define TEST_RULE  "ME_SID_050_010"
#define TEST_DESC  "Check RCiEP PASID capability          "

#define MIN_PASID_SUPPORT 20

static
void
payload(void)
{

  uint32_t bdf;
  uint32_t hart_index;
  uint32_t tbl_index;
  uint32_t dp_type;
  uint32_t status;
  uint32_t max_pasids = 0;
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

      /* Check entry is RCiEP */
      if (dp_type == RCiEP)
      {
          val_print(ACS_PRINT_DEBUG, "\n       BDF - 0x%x", bdf);
          /* Get max PASID with from device bdf */
          status = val_pcie_get_max_pasid_width(bdf, &max_pasids);
          /* Skip the device if PASID extended capability not supported */
          if (status == PCIE_CAP_NOT_FOUND)
          {
              val_print(ACS_PRINT_DEBUG, "\n       PASID extended capability not supported.", 0);
              val_print(ACS_PRINT_DEBUG, " Skipping for BDF: 0x%x", bdf);
              continue;
          }
          /* Raise an error if any failure in obtaining the PASID max width */
          else if (status)
          {
              test_skip = 0;
              val_print(ACS_PRINT_ERR,
                              "\n       Error in obtaining the PASID max width for BDF: 0x%x", bdf);
              test_fails++;
              continue;
          }

          /* Test runs for atleast an endpoint */
          test_skip = 0;
          val_print(ACS_PRINT_DEBUG, "\n       BDF - 0x%x", bdf);
          val_print(ACS_PRINT_DEBUG, "- Max PASID bits - 0x%x", max_pasids);
          /* If PASID Support, Max PASID Width should be 20 bits. */
          if (max_pasids > 0)
          {
              if (max_pasids < MIN_PASID_SUPPORT)
              {
                  val_print(ACS_PRINT_ERR, "\n       Max PASID support less than 20 bits  ", 0);
                  test_fails++;
              }
          }
      }
  }

  if (test_skip == 1) {
      val_print(ACS_PRINT_DEBUG, "\n       No RCiEP type device support PASID found, Skipping test", 0);
      val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 2));
  }
  else if (test_fails)
      val_set_status(hart_index, RESULT_FAIL(TEST_NUM, test_fails));
  else
      val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_p024_entry(uint32_t num_hart)
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
