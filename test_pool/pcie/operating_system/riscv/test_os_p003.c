/** @file
 * Copyright (c) 2016-2018, 2021,2023, Arm Limited or its affiliates. All rights reserved.
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

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 3)
#define TEST_RULE  "MF_ECAM_060_010"
#define TEST_DESC  "Check access the configuration space when PCIe link down"

uint32_t original_vendor[PCIE_MAX_DEV][PCIE_MAX_FUNC];

static
uint32_t
check_bdf_under_rp(uint32_t rp_bdf)
{
  uint32_t reg_value;
  uint32_t rp_sec_bus, rp_sub_bus;
  uint32_t dev_bdf, dev_bus, dev_sec_bus;
  uint32_t rp_seg, dev_seg;
  uint32_t base_cc;
  uint32_t dev_num, func_num;

  rp_seg = PCIE_EXTRACT_BDF_SEG(rp_bdf);
  val_pcie_read_cfg(rp_bdf, TYPE1_PBN, &reg_value);
  rp_sec_bus = ((reg_value >> SECBN_SHIFT) & SECBN_MASK);
  rp_sub_bus = ((reg_value >> SUBBN_SHIFT) & SUBBN_MASK);

  for (dev_sec_bus = rp_sec_bus; dev_sec_bus <= rp_sub_bus; dev_sec_bus++)
  {
      for (dev_num = 0; dev_num < PCIE_MAX_DEV; dev_num++)
      {
          for (func_num = 0; func_num < PCIE_MAX_FUNC; func_num++)
          {
              dev_bdf = PCIE_CREATE_BDF(rp_seg, dev_sec_bus, dev_num, func_num);
              val_pcie_read_cfg(dev_bdf, TYPE01_VIDR, &reg_value);
              if (reg_value == PCIE_UNKNOWN_RESPONSE)
                  continue;

              dev_bus = PCIE_EXTRACT_BDF_BUS(dev_bdf);
              dev_seg = PCIE_EXTRACT_BDF_SEG(dev_bdf);
              if ((dev_seg == rp_seg) && ((dev_bus >= rp_sec_bus) && (dev_bus <= rp_sub_bus)))
              {
                  val_pcie_read_cfg(dev_bdf, TYPE01_RIDR, &reg_value);
                  val_print(ACS_PRINT_DEBUG, "\n       Class code is 0x%x", reg_value);
                  base_cc = reg_value >> TYPE01_BCC_SHIFT;
                  if ((base_cc != CNTRL_CC) && (base_cc != DP_CNTRL_CC) && (base_cc != MAS_CC))
                      return 1;
              }
           }
       }
  }

   return 0;
}

static
uint8_t
enumerate_pcie_device (uint8_t segment_num, uint8_t bus_num, uint8_t first)
{
  uint8_t dev_index;
  uint8_t func_index;
  uint32_t bdf;
  uint32_t vendor;

  for (dev_index = 0; dev_index < PCIE_MAX_DEV; dev_index++) {
    for (func_index = 0; func_index < PCIE_MAX_FUNC; func_index++) {
      bdf = PCIE_CREATE_BDF(segment_num, bus_num, dev_index, func_index);
      val_pcie_read_cfg_width(bdf, TYPE01_VIDR, &vendor, PCI_WIDTH_UINT32);
      if (first) {
        original_vendor[dev_index][func_index] = vendor;
        // val_print(ACS_PRINT_INFO, "\n       Vendor ID is 0x%04x ", vendor);
      } else if (vendor != original_vendor[dev_index][func_index]) {
        val_print(ACS_PRINT_ERR, "\n       Vendor ID is changed from 0x%04x ", original_vendor[dev_index][func_index]);
        val_print(ACS_PRINT_ERR, "\n       Vendor ID is changed to 0x%04x ", vendor);
        return 0;
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
  uint32_t dp_type;
  uint32_t hart_index;
  uint32_t tbl_index;
  uint32_t test_skip;
  pcie_device_bdf_table *bdf_tbl_ptr;
  uint8_t p_bus;
  uint8_t p_seg;
  uint8_t ret;
  uint32_t cap_base;
  uint32_t reg_value = 0xFFFFFFFF;

  tbl_index = 0;
  bdf_tbl_ptr = val_pcie_bdf_table_ptr();
  hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  test_skip = 1;

  while (tbl_index < bdf_tbl_ptr->num_entries) {
      /*
       * If a function is in the hierarchy domain
       * originated by a Root Port, check its ECAM
       * is same as its RootPort ECAM.
       */
      bdf = bdf_tbl_ptr->device[tbl_index++].bdf;
      val_print(ACS_PRINT_DEBUG, "\n       BDF - 0x%x", bdf);
      dp_type = val_pcie_device_port_type(bdf);
      if (dp_type == RP) {
        /** Skip Check_2 if there is an Ethernet or Display controller under the RP device **/
        if (check_bdf_under_rp(bdf) == 0) {
            val_print(ACS_PRINT_DEBUG, "\n       Skipping for RP BDF 0x%x", bdf);
            continue;
        } else {
            p_seg = PCIE_EXTRACT_BDF_SEG(bdf);
            val_print(ACS_PRINT_DEBUG, "\n       Root port segment number is 0x%x", p_seg);
            p_bus = PCIE_EXTRACT_BDF_BUS(bdf);
            val_print(ACS_PRINT_DEBUG, "\n       Root port bus number is 0x%x", p_bus);
            test_skip = 0;
            break;
        }
      }
  }

  if (test_skip == 1) {
      val_print(ACS_PRINT_DEBUG,
          "\n       No RP type device for test found. Skipping test", 0);
      val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
      return;
  }

  /* Accessing the BDF PCIe config range first time*/
  ret = enumerate_pcie_device(p_seg, p_bus, 1);

  /* Disable rp link*/
  val_pcie_find_capability(bdf, PCIE_CAP, CID_PCIECS, &cap_base);
  val_pcie_read_cfg(bdf, cap_base + LCTRLR_OFFSET, &reg_value);
  if ((reg_value & LINK_DISABLE_SHIFT) != LINK_DISABLE) {
     val_print(ACS_PRINT_ERR, "\n       Link control is not link disable and disable it", 0);
     reg_value |= LINK_DISABLE;
     val_pcie_write_cfg(bdf, cap_base + LCTRLR_OFFSET, reg_value);
  }

  /* Accessing the BDF PCIe config range second time*/
  ret = enumerate_pcie_device(p_seg, p_bus, 0);
  if (ret == 0) {
    val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
  } else {
    val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  }

  /* Enable rp link*/
  if ((reg_value & LINK_DISABLE_SHIFT) == LINK_DISABLE) {
     val_print(ACS_PRINT_ERR, "\n       Link control is link disable and relink it", 0);
     reg_value &= ~LINK_DISABLE;
     val_pcie_write_cfg(bdf, cap_base + LCTRLR_OFFSET, reg_value);
  }

}

uint32_t
os_p003_entry(uint32_t num_hart)
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
