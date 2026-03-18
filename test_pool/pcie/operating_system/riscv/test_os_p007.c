/** @file
 * Copyright (c) 2021, 2023, Arm Limited or its affiliates. All rights reserved.
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

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 7)
#define TEST_RULE  "MF_MMS_040_010, MF_MMS_050_010"
#define TEST_DESC  "Check all 1's for accesses memory outside the PCIe bus range or when the link is down..."

static void *branch_to_test;

static
void
esr(uint64_t interrupt_type, void *context)
{
  uint32_t hart_index;

  hart_index = val_hart_get_index_mpid(val_hart_get_mpid());

  /* Update the ELR to return to test specified address */
  val_hart_update_elr(context, (uint64_t)branch_to_test);

  val_print(ACS_PRINT_ERR, "\n       Received exception of type: %d", interrupt_type);
  val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 01));
}

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
                  if ((base_cc != CNTRL_CC) && (base_cc != DP_CNTRL_CC) && (base_cc != MAS_CC)) {
                    return 1;
                  }
              }
           }
       }
  }

   return 0;
}


static
void
payload(void)
{

  uint32_t hart_index;
  uint32_t bdf;
  uint32_t dp_type;
  uint32_t tbl_index;
  pcie_device_bdf_table *bdf_tbl_ptr;
  uint32_t status;
  uint32_t test_skip = 1;
  uint64_t mem_base = 0, ori_mem_base = 0;
  uint64_t mem_lim = 0, new_mem_lim;
  uint32_t mem_offset = 0;
  uint32_t reg_value, value;
  uint32_t cap_base = 0;
  uint8_t ret = 0;

  tbl_index = 0;
  bdf_tbl_ptr = val_pcie_bdf_table_ptr();
  hart_index = val_hart_get_index_mpid(val_hart_get_mpid());

  /* Install sync and async handlers to handle exceptions.*/
  status = val_hart_install_esr(EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, esr);
  status |= val_hart_install_esr(EXCEPT_AARCH64_SERROR, esr);
  if (status)
  {
      val_print(ACS_PRINT_ERR, "\n       Failed in installing the exception handler", 0);
      val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 01));
      return;
  }

  branch_to_test = &&exception_return;

  while (tbl_index < bdf_tbl_ptr->num_entries)
  {
      bdf = bdf_tbl_ptr->device[tbl_index++].bdf;
      dp_type = val_pcie_device_port_type(bdf);
      if (dp_type == RP) {
        /** Skip Check if there is an Ethernet or Display controller
         * under the RP device.**/
        ret = check_bdf_under_rp(bdf);
        if (ret == 0) {
          val_print(ACS_PRINT_DEBUG, "\n       Skipping for RP BDF 0x%x", bdf);
          continue;
        }

        /* Enable Bus Master Enable */
        val_pcie_enable_bme(bdf);
        /* Enable Memory Space Access */
        val_pcie_enable_msa(bdf);
        /* Clearing UR in Device Status Register */
        val_pcie_clear_urd(bdf);
        val_print(ACS_PRINT_DEBUG, "\n       BDF is 0x%x", bdf);

        /* 
         * Check When Address is outside the Range of NP Memory Range.
        */
        /* Read Function's Memory Base Limit Register */
        val_pcie_read_cfg(bdf, TYPE1_NP_MEM, &reg_value);
        if (reg_value == 0)
          continue;
        mem_base = (reg_value & MEM_BA_MASK) << MEM_BA_SHIFT;
        mem_lim = (reg_value & MEM_LIM_MASK) | MEM_LIM_LOWER_BITS;
        val_print(ACS_PRINT_DEBUG, "\n       Memory base is 0x%llx", mem_base);
        val_print(ACS_PRINT_DEBUG, " Memory lim is  0x%llx", mem_lim);
        /* If Memory Limit is programmed with value less the Base, then Skip.*/
        if (mem_lim < mem_base) {
          val_print(ACS_PRINT_DEBUG, "\n       No NP memory on secondary side of the Bridge", 0);
          val_print(ACS_PRINT_DEBUG, "\n       Skipping Bdf - 0x%x", bdf);
          continue;
        }

        /* If test runs for atleast an endpoint */
        test_skip = 0;
        mem_offset = val_pcie_mem_get_offset(bdf, NON_PREFETCH_MEMORY);
        if ((mem_base + mem_offset) > mem_lim)
        {
            val_print(ACS_PRINT_ERR,
                    "\n       Memory offset + base 0x%llx", mem_base + mem_offset);
            val_print(ACS_PRINT_ERR, " exceeds the memory limit 0x%llx", mem_lim);
            val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 02));
            return;
        }

        /**Check_1: Accessing out of NP memory limit range must return 0xFFFFFFFF
         * If the limit exceeds 1MB then modify the range to be 1MB
         * and access out of the limit set
         **/
        ori_mem_base = mem_base;
        if ((mem_lim >> MEM_SHIFT) > (mem_base >> MEM_SHIFT))
        {
           val_print(ACS_PRINT_DEBUG, "\n       Entered Check_1 for bdf 0x%x", bdf);
           new_mem_lim = mem_base + MEM_OFFSET_LARGE;
           mem_base = mem_base | (mem_base  >> 16);
           val_pcie_write_cfg(bdf, TYPE1_NP_MEM, mem_base);
           val_pcie_read_cfg(bdf, TYPE1_NP_MEM, &reg_value);

           val_pcie_bar_mem_read(bdf, new_mem_lim + MEM_OFFSET_SMALL, &value);
           val_print(ACS_PRINT_DEBUG, "  Value read is 0x%llx", value);
           if (value != PCIE_UNKNOWN_RESPONSE)
           {
               val_print(ACS_PRINT_ERR, "\n       Memory range for bdf 0x%x", bdf);
               val_print(ACS_PRINT_ERR, " is 0x%x", reg_value);
               val_print(ACS_PRINT_ERR,
                       "\n       Out of range 0x%x", (new_mem_lim + MEM_OFFSET_SMALL));
               val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 03));
           }

           /* Check_2 device bar access when rp link down */
           /* Disable rp link*/
          val_pcie_find_capability(bdf, PCIE_CAP, CID_PCIECS, &cap_base);
          val_pcie_read_cfg(bdf, cap_base + LCTRLR_OFFSET, &reg_value);
          if ((reg_value & LINK_DISABLE_SHIFT) != LINK_DISABLE) {
            val_print(ACS_PRINT_ERR, "\n       Link control is not link disable and disable it", 0);
            reg_value |= LINK_DISABLE;
            val_pcie_write_cfg(bdf, cap_base + LCTRLR_OFFSET, reg_value);
            val_pcie_bar_mem_read(bdf, new_mem_lim + MEM_OFFSET_SMALL, &value);
            val_print(ACS_PRINT_DEBUG, "\n Check_2 Value read is 0x%llx", value);
            if (value != PCIE_UNKNOWN_RESPONSE)
            {
                val_print(ACS_PRINT_ERR, "\n  Check_2 Memory range for bdf 0x%x", bdf);
                val_print(ACS_PRINT_ERR, " is 0x%x", reg_value);
                val_print(ACS_PRINT_ERR,
                        "\n       Out of range 0x%x", (new_mem_lim + MEM_OFFSET_SMALL));
                val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 03));
            }
          }
        }

exception_return:
        /*Write back original value */
        if ((mem_lim >> MEM_SHIFT) > (ori_mem_base >> MEM_SHIFT))
        {
            val_pcie_write_cfg(bdf, TYPE1_P_MEM,
                                              ((mem_lim & MEM_LIM_MASK) | (ori_mem_base  >> 16)));
            val_pcie_write_cfg(bdf, TYPE1_P_MEM_LU, (mem_lim >> 32));
        }

        /* Memory Space might have constraint on RW/RO behaviour
         * So not checking for Read-Write Data mismatch.
        */
        if (IS_TEST_FAIL(val_get_status(hart_index))) {
          val_print(ACS_PRINT_ERR,
              "\n       Failed exception on Memory Access For Bdf : 0x%x", bdf);
          val_pcie_clear_urd(bdf);
          return;
        }
     }
  }

  if (test_skip == 1) {
      val_print(ACS_PRINT_DEBUG,
        "\n       No RP type device for test found ", 0);
      val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  }
  else
      val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_p007_entry(uint32_t num_hart)
{

  uint32_t status = ACS_STATUS_FAIL;

  num_hart = 1;  //This test is run on single processor

  status = val_initialize_test(TEST_NUM, TEST_DESC, num_hart);
  if (status != ACS_STATUS_SKIP)
      val_run_test_payload(TEST_NUM, num_hart, payload, 0);

  /* get the result from single HART and check for failure */
  status = val_check_for_error(TEST_NUM, num_hart, TEST_RULE);

  val_report_status(0, BSA_ACS_END(TEST_NUM), NULL);

  return status;
}
