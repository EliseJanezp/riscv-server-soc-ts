/** @file
 * Copyright (c) 2020, 2022-2023, Arm Limited or its affiliates. All rights reserved.
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

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 5)
#define TEST_RULE  "MF_ECM_090_010, MF_ECM_100_010"
#define TEST_DESC  "Check configuration space access behavior under scenarios including: \
            non-existent devices, post-FLR (Function Level Reset) devices, \
            CRS (Configuration Request Retry Status) states, and link-down conditions......"

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
  uint32_t cap_base;
  uint8_t af_cap;

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
                    /* Check this device support FLR */
                    if (val_pcie_find_capability(dev_bdf, PCIE_ECAP, ECID_AF, &cap_base) == PCIE_SUCCESS) {
                      val_pcie_read_cfg_width(dev_bdf, cap_base + AF_CAPABILITY_OFFSET, &af_cap, PCI_WIDTH_UINT8);
                      if ((af_cap & FLR_CAP_EN) == FLR_CAP_EN) {
                        return dev_bdf;
                      }
                    }
                  }
              }
           }
       }
  }

   return 0;
}

/* Returns the nf bdf value for that bus from bdf table */
static 
uint32_t 
get_nf_bdf(uint32_t segment, uint32_t bus)
{
  pcie_device_bdf_table *bdf_tbl_ptr;
  uint32_t seg_num;
  uint32_t bus_num;
  uint32_t dev_num, max_dev_num;
  uint32_t func_num, max_func_num;
  uint32_t bdf;
  uint32_t tbl_index = 0;
  uint32_t nf_bdf = 0;

  bdf_tbl_ptr = val_pcie_bdf_table_ptr();
  max_dev_num = 0;
  for (tbl_index = 0; tbl_index < bdf_tbl_ptr->num_entries; tbl_index++)
  {
      bdf = bdf_tbl_ptr->device[tbl_index].bdf;
      seg_num = PCIE_EXTRACT_BDF_SEG(bdf);
      bus_num = PCIE_EXTRACT_BDF_BUS(bdf);
      dev_num = PCIE_EXTRACT_BDF_DEV(bdf);

      if ((segment == seg_num) && (bus_num == bus) && (dev_num > max_dev_num)) {
        max_dev_num = dev_num;
      }
  }

  max_func_num = 0;
  if (max_dev_num == (PCIE_MAX_DEV - 1)) {
    for (tbl_index = 0; tbl_index < bdf_tbl_ptr->num_entries; tbl_index++)
    {
        bdf = bdf_tbl_ptr->device[tbl_index].bdf;
        seg_num = PCIE_EXTRACT_BDF_SEG(bdf);
        bus_num = PCIE_EXTRACT_BDF_BUS(bdf);
        dev_num = PCIE_EXTRACT_BDF_DEV(bdf);
        func_num = PCIE_EXTRACT_BDF_FUNC(bdf);

        if ((segment == seg_num) && (bus_num == bus) && (dev_num == max_dev_num) && (func_num > max_func_num)) {
          max_func_num = func_num;
        }
    }
  }

  if (max_dev_num != (PCIE_MAX_DEV - 1)) {
    nf_bdf = PCIE_CREATE_BDF(segment, bus, (max_dev_num + 1), max_func_num);
  } else if (max_func_num != (PCIE_MAX_FUNC - 1)) {
    nf_bdf = PCIE_CREATE_BDF(segment, bus, max_dev_num, (max_func_num + 1));
  }
  val_print(ACS_PRINT_DEBUG, "\n NF Device dev num 0x%x", nf_bdf);
  return nf_bdf;
}

static
uint32_t
wait_device_discovery(uint32_t bdf)
{
  uint32_t retry = 1000;
  uint32_t reg_value = 0;
  uint32_t timeout = TIMEOUT_MEDIUM;

  for (int i = 0; i < retry; i++) {
    val_pcie_read_cfg_width(bdf, TYPE01_VIDR, &reg_value, PCI_WIDTH_UINT32);
    if (reg_value != 0xFFFFFFFF){
      return 1;
    }
    while ((--timeout > 0)){};
  }
  return 0;
}

static
void
payload(void)
{

  uint32_t bdf;
  uint32_t dp_type;
  uint32_t hart_index;
  uint32_t tbl_index;
  uint32_t reg_value;
  uint32_t cap_base;
  uint32_t af_cap_base;
  uint32_t status;
  uint32_t test_skip = 1;
  uint32_t rp_bus;
  uint32_t rp_seg;
  uint32_t nf_bdf;
  uint32_t dev_bdf;
  uint32_t ret;
  uint16_t reg_value16;
  uint8_t flr_reg;
  pcie_device_bdf_table *bdf_tbl_ptr;

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

  tbl_index = 0;
  while (tbl_index < bdf_tbl_ptr->num_entries)
  {
      bdf = bdf_tbl_ptr->device[tbl_index++].bdf;
      dp_type = val_pcie_device_port_type(bdf);

      if (dp_type == RP)
      {
        rp_seg = PCIE_EXTRACT_BDF_SEG(bdf);
        rp_bus = PCIE_EXTRACT_BDF_BUS(bdf);
        test_skip = 0;

        /* 1.Check RP header type is type 1 */
        if (val_pcie_function_header_type(bdf) != TYPE1_HEADER) {
          val_print(ACS_PRINT_ERR, "\n       RP header type is not type1 for BDF  0x%x", bdf);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 01));
          return;
        }

        /* 2.Check all 1's for NF device's vendorid */
        nf_bdf = get_nf_bdf(rp_seg, rp_bus);
        if (nf_bdf == 0) {
          val_print(ACS_PRINT_DEBUG, "\n       No NF Device under rp_bus 0x%x", bdf);
        } else {
          ret = val_pcie_read_cfg_width(nf_bdf, TYPE01_VIDR, &reg_value, PCI_WIDTH_UINT32);
          if (ret == PCIE_NO_MAPPING || (reg_value != 0xFFFFFFFF)) {
            val_print(ACS_PRINT_ERR,
                  "\n         Incorrect vendor ID 1 %04x    ", reg_value);
            val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 02));
            return;
          }
          /* Write command register offset of NF and verify no errors or exceptions occur */
          val_pcie_enable_bme(nf_bdf);
        }

        /* 3.Make an unaligned 2 and 4 byte read to configuration space of RP and verify all 1’s returned. */
        val_pcie_read_cfg_width(bdf, (TYPE01_VIDR + 1), &reg_value16, PCI_WIDTH_UINT16);
        if (reg_value16 != 0xFFFF) {
          val_print(ACS_PRINT_ERR, "\n       Incorrect vendor ID 2 %02x    ", reg_value16);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 03));
          return;
        }
        val_pcie_read_cfg_width(bdf, (TYPE01_VIDR + 3), &reg_value, PCI_WIDTH_UINT32);
        if (reg_value != 0xFFFFFFFF) {
          val_print(ACS_PRINT_ERR, "\n       Incorrect vendor ID 3 %04x    ", reg_value);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 04));
          return;
        }

        /* 4.Check device under RP support FLR */
        /** Skip Check if there is an Ethernet or Display controller
         * under the RP device, and this no device support FLR.**/
        dev_bdf = check_bdf_under_rp(bdf);
        if (dev_bdf == 0)
        {
          val_print(ACS_PRINT_DEBUG, "\n       There is no device support FLR for RP BDF 0x%x", bdf);
          continue;
        }
        /* 5.Check RP support CSR */
        val_pcie_find_capability(bdf, PCIE_CAP, CID_PCIECS, &cap_base);
        val_pcie_read_cfg_width(bdf, cap_base + ROOTCP_OFFSET, &reg_value, PCI_WIDTH_UINT16);
        if ((reg_value & 0x01) != 0x01) {
           val_print(ACS_PRINT_DEBUG,     "\n       RRS software visibility is not enabled on RP: 0x%x", bdf);
           continue;
        }

        /* 6. Check device header type is type0 */
        if (val_pcie_function_header_type(dev_bdf) != TYPE0_HEADER) {
          val_print(ACS_PRINT_ERR, "\n       Device header type is not type0 for BDF  0x%x", dev_bdf);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 05));
          return;
        }

        /* 7.Check device ecam acess when disable CSR and FLR */
        //  Disable CRS software visibility in R
        val_pcie_read_cfg_width(bdf, cap_base + RTCTRL_OFFSET, &reg_value, PCI_WIDTH_UINT16);
        if ((reg_value & CSR_EN_SHIFT) == CSR_ENABLE) {
          reg_value &= ~CSR_ENABLE;
          val_pcie_write_cfg_width(bdf, cap_base + RTCTRL_OFFSET, &reg_value, PCI_WIDTH_UINT16);
        }
        // Set founction level reset for device D
        val_pcie_find_capability(dev_bdf, PCIE_ECAP, ECID_AF, &af_cap_base);
        val_pcie_read_cfg_width(dev_bdf, af_cap_base + AF_CTRL, &flr_reg, PCI_WIDTH_UINT8);
        flr_reg |= FLR_EN;
        val_pcie_write_cfg_width(dev_bdf, af_cap_base + AF_CTRL, &flr_reg, PCI_WIDTH_UINT8);
        val_pcie_read_cfg_width(bdf, TYPE01_VIDR, &reg_value, PCI_WIDTH_UINT16);
        if (reg_value != 0xFFFF) {
          val_print(ACS_PRINT_ERR, "\n       Incorrect vendor ID %04x    ", reg_value);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 06));
          return;
        }
        // Keep reading vendor ID till D is discovered
        ret = wait_device_discovery(dev_bdf);
        if (ret == 0) {
          val_print(ACS_PRINT_ERR, "\n       Device re discover fail for BDF  0x%x", dev_bdf);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 06));
          return;
        }

        /* 8.Check device ecam acess when enable CSR and FLR */
        val_pcie_read_cfg_width(bdf, cap_base + RTCTRL_OFFSET, &reg_value, PCI_WIDTH_UINT16);
        if ((reg_value & CSR_EN_SHIFT) != CSR_ENABLE) {
          reg_value |= CSR_ENABLE;
          val_pcie_write_cfg_width(bdf, cap_base + RTCTRL_OFFSET, &reg_value, PCI_WIDTH_UINT16);
        }
        // Set founction level reset for device D
        val_pcie_read_cfg_width(dev_bdf, af_cap_base + AF_CTRL, &flr_reg, PCI_WIDTH_UINT8);
        flr_reg |= FLR_EN;
        val_pcie_write_cfg_width(dev_bdf, af_cap_base + AF_CTRL, &flr_reg, PCI_WIDTH_UINT8);
        val_pcie_read_cfg_width(bdf, TYPE01_VIDR, &reg_value, PCI_WIDTH_UINT16);
        if (reg_value != 0x0001) { // CSR status
          val_print(ACS_PRINT_ERR, "\n       Incorrect vendor ID %04x    ", reg_value);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 7));
          return;
        }
        val_pcie_read_cfg_width(bdf, TYPE01_VIDR + 2, &reg_value, PCI_WIDTH_UINT16);
        if (reg_value != 0xFFFF) {
          val_print(ACS_PRINT_ERR, "\n       Incorrect device ID %04x    ", reg_value);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 7));
          return;
        }
        // Keep reading vendor ID till D is discovered
        ret = wait_device_discovery(dev_bdf);
        if (ret == 0) {
          val_print(ACS_PRINT_ERR, "\n       Device re discover fail for BDF  0x%x", dev_bdf);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 11));
          return;
        }

        /* 9. Check device ecam access when rp link down */
        /* Disable rp link*/
        val_pcie_find_capability(bdf, PCIE_CAP, CID_PCIECS, &cap_base);
        val_pcie_read_cfg(bdf, cap_base + LCTRLR_OFFSET, &reg_value);
        if ((reg_value & LINK_DISABLE_SHIFT) != LINK_DISABLE) {
          val_print(ACS_PRINT_ERR, "\n       Link control is not link disable and disable it", 0);
          reg_value |= LINK_DISABLE;
          val_pcie_write_cfg(bdf, cap_base + LCTRLR_OFFSET, reg_value);
        }
        val_pcie_read_cfg_width(bdf, TYPE01_VIDR, &reg_value, PCI_WIDTH_UINT16);
        if (reg_value != 0xFFFF) {
          val_print(ACS_PRINT_ERR, "\n       Incorrect vendor ID %04x    ", reg_value);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 12));
          return;
        }
        /* Enable rp link*/
        if ((reg_value & LINK_DISABLE_SHIFT) == LINK_DISABLE) {
          val_print(ACS_PRINT_ERR, "\n       Link control is link disable and relink it", 0);
          reg_value &= ~LINK_DISABLE;
          val_pcie_write_cfg(bdf, cap_base + LCTRLR_OFFSET, reg_value);
        }
      }

exception_return:
      /*Write back original value */
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

  if (test_skip == 1) {
      val_print(ACS_PRINT_DEBUG,
        "\n       No RP type device found.", 0);
      val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  }
  else
      val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_p005_entry(uint32_t num_hart)
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
