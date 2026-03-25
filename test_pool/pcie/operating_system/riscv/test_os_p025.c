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

#include "val/include/bsa_acs_hart.h"
#include "val/include/bsa_acs_pcie.h"
#include "val/include/bsa_acs_memory.h"


#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 25)
#define TEST_RULE  "ME_SID_070_010"
#define TEST_DESC  "Check RCiEP support 64 bit MMIO bar          "

#define NO_BAR       0xFF

static
uint8_t
check_pcie_has_bar(uint32_t bdf, uint32_t *bar_type)
{
  uint32_t index;
  uint32_t bar_low32bits = 0;
  uint32_t bar_high32bits = 0;

  index = 0;
  while (index < TYPE1_MAX_BARS)
  {
    /* Read the base address register at loop index */
    val_pcie_read_cfg(bdf, TYPE01_BAR + index * 4, &bar_low32bits);
    /* Check if the BAR is Memory Mapped IO type */
    if (((bar_low32bits >> BAR_MIT_SHIFT) & BAR_MIT_MASK) == MMIO)
    {
        /* Check if the BAR is 64-bit decodable */
        if (((bar_low32bits >> BAR_MDT_SHIFT) & BAR_MDT_MASK) == BITS_64)
        {
            /* Read the second sequential BAR at next index */
            val_pcie_read_cfg(bdf, TYPE01_BAR + (index + 1) * 4, &bar_high32bits);

            /* Adjust the index to skip next sequential BAR */
            *bar_type = BAR_64_BIT;
            index++;

        } else if (((bar_low32bits >> BAR_MDT_SHIFT) & BAR_MDT_MASK) == BITS_32)
        {
          /* Fill lower 32-bits of 64-bit address with first sequential BAR */
          *bar_type = BAR_32_BIT;
        }
        return 1;
    }

    /* Adjust index to point to next BAR */
    index++;
  }

  return 0;
}

static
void
payload(void)
{

  uint32_t bdf;
  uint32_t hart_index;
  uint32_t tbl_index;
  uint32_t dp_type;
  uint32_t bar_type;
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
          if (check_pcie_has_bar(bdf, &bar_type) == 0) {
            val_print(ACS_PRINT_DEBUG, "\n       RCiEP (BDF:0x%x) does not have MMIO BAR. Skipping test", bdf);
            continue;
          }
          
          /* Test runs for atleast an endpoint */
          test_skip = 0;
          val_print(ACS_PRINT_DEBUG, "\n       BDF - 0x%x", bdf);

          if (bar_type != BAR_64_BIT) {
              val_print(ACS_PRINT_ERR,
                    "\n       64 bit bar Check Failed, Bdf : 0x%x", bdf);
              test_fails++;
          }
      }
  }

  if (test_skip == 1) {
      val_print(ACS_PRINT_DEBUG, "\n       No RCiEP type device which support bar found. Skipping device", 0);
      val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 2));
  }
  else if (test_fails)
      val_set_status(hart_index, RESULT_FAIL(TEST_NUM, test_fails));
  else
      val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_p025_entry(uint32_t num_hart)
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
