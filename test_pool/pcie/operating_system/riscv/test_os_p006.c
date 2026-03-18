/** @file
 * Copyright (c) 2016-2018, 2021, 2023, Arm Limited or its affiliates. All rights reserved.
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
#include "val/include/bsa_acs_gic.h"
#include "val/include/bsa_acs_memory.h"

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 6)
#define TEST_RULE  "ME_MMS_010_010, ME_MMS_020_010"
#define TEST_DESC  "Verify each host bridge has a memory range available for 64-bit BARs and 32-bit BARs"

#define AML_DEVICE_OP_BYTE0     0x5B
#define AML_DEVICE_OP_BYTE1     0x82
#define AML_NAME_OP             0x08
#define AML_END_TAG             0x79
#define AML_QWORD_MEM_OP        0x8A
#define AML_DWORD_MEM_OP        0x87
#define AML_DWORD_MEM_BYTE_OP   0x01
#define AML_CRS_BUFFER_OP       0x11

#define PCIE_CRS_LEN            100
#define CRS_MEM_FLAG_LEN        4

#define MAX_MMIO_RANGE_NUM      10
static uint8_t host_num;

typedef struct {
  uint32_t mem32_start;
  uint32_t mem32_len;
  uint64_t mem64_start;
  uint64_t mem64_len;
} pcie_mmio_range;

pcie_mmio_range mmio_range[MAX_MMIO_RANGE_NUM] = {0};

static
uint8_t
strncmp (char *str1, const char *str2, uint8_t len)
{
  if ((str1 == NULL) || (str2 == NULL) || (len == 0)) {
    return 1;
  }

  while ((len > 1) && (*str1 != '\0') && (*str2 != '\0') && (*str1 == *str2)) {
    str1++;
    str2++;
    len--;
  }

  return *str1 - *str2;
}


uint32_t parse_pkg_length(uint8_t **pkg) {
    uint8_t *ptr = *pkg;
    uint32_t length = 0;
    
    if ((*ptr & 0xC0) == 0x00) { // 1 byte length
        length = *ptr & 0x3F;
        ptr += 1;
    } else if ((*ptr & 0xC0) == 0x40) { // 2 byte length
        length = (ptr[1] << 4) | (ptr[0] & 0x0F);
        ptr += 2;
    } else if ((*ptr & 0xC0) == 0x80) { // 3 byte length
        length = (ptr[2]  << 12) | (ptr[1] << 4) | (ptr[0] & 0x0F);
        ptr += 3;
    }
    
    *pkg = ptr;
    return length;
}

static
void 
parse_crs(uint8_t *crs_start, uint8_t *crs_end) {
  uint8_t *ptr = crs_start;
  uint32_t offset = 0;
  uint8_t find_mem64, find_mem32;
  uint8_t range_count = host_num;

  find_mem64 = 0;
  find_mem32 = 0;
  while ((ptr < crs_end) && (*ptr != AML_END_TAG)) {
      switch (*ptr) {
          case AML_QWORD_MEM_OP:  // 64-bit memory
              // val_print(ACS_PRINT_INFO, "qword mem ptr = 0x%llx\n", (uint64_t)(ptr));
              // Skip OP + LEN + Flags + offset, 14 bytes
              offset = 1 + 1 + CRS_MEM_FLAG_LEN + sizeof(uint64_t);
              mmio_range[range_count].mem64_start = *(uint64_t*)(ptr + offset);
              offset += 24;
              mmio_range[range_count].mem64_len = *(uint64_t*)(ptr + offset);
              ptr += 0x2B;  // qword mem len
              find_mem64 = 1;
              break;

          case AML_DWORD_MEM_OP:  // 32-bit memory
              offset = 1 + 1 + 3; // Skip OP + LEN + Flags - 1, 5 bytes
              if (*(ptr + offset) != AML_DWORD_MEM_BYTE_OP) {
                ptr += offset;
                break;
              }
              // val_print(ACS_PRINT_INFO, "dword mem ptr = 0x%llx\n", (uint64_t)(ptr));
              // Skip OP + LEN + Flags + offset, 10 bytes
              offset = 1 + 1 + CRS_MEM_FLAG_LEN + sizeof(uint32_t);
              mmio_range[range_count].mem32_start = *(uint32_t*)(ptr + offset);
              offset += 12;
              mmio_range[range_count].mem32_len = *(uint32_t*)(ptr + offset);
              ptr += 0x17; // dword mem len
              find_mem32 = 1;
              break;

          default:
              ptr += 1;
              break;
      }
  }
  // 打印结果
  for (int i = 0; i < host_num + 1; i++) {
      val_print(ACS_PRINT_INFO, "mem64_start = 0x%llx\n", mmio_range[i].mem64_start);
      val_print(ACS_PRINT_INFO, "mem64_len = 0x%llx\n", mmio_range[i].mem64_len);
      val_print(ACS_PRINT_INFO, "mem32_start = 0x%llx\n", mmio_range[i].mem32_start);
      val_print(ACS_PRINT_INFO, "mem32_len = 0x%llx\n", mmio_range[i].mem32_len);
  }
}

static
uint8_t
find_pci_host_bridges (uint64_t ptr)
{
  uint32_t table_length;
  uint32_t pkg_length;
  uint8_t *aml_start;
  uint8_t *cur_ptr;
  uint8_t *device_end;
  uint32_t buf_len;

  host_num = 0;

  table_length = *(uint32_t *)(ptr + 4);
  aml_start = (uint8_t *)(ptr);  // skip common header
  val_print(ACS_PRINT_INFO, "dsdt table length = 0x%x\n", table_length);
  while (aml_start < (uint8_t *)(ptr + table_length)){
    if ((*aml_start == AML_DEVICE_OP_BYTE0) && (*(aml_start + 1) == AML_DEVICE_OP_BYTE1)) {
      cur_ptr = aml_start + 2;
      pkg_length = parse_pkg_length(&cur_ptr);
      // val_print(ACS_PRINT_INFO, "current ptr = 0x%llx\n", (uint64_t)cur_ptr);
      // val_print(ACS_PRINT_INFO, "dsdt device pkg length = 0x%x\n", pkg_length);
      if (strncmp((char *)(cur_ptr), "PCI", 3) == 0) {
          // find _crs
          val_print(ACS_PRINT_INFO, "Find PCIe host bridge %d\n", host_num);
          device_end = cur_ptr + pkg_length;
          cur_ptr += 4; // skip "PCIx"
          while (cur_ptr < device_end) {
            if ((strncmp((char *)cur_ptr, "_CRS", 4) == 0) && (*(cur_ptr + 4) == AML_CRS_BUFFER_OP)) {
                // val_print(ACS_PRINT_INFO, "Find _CRS: 0x%lx\n", (uint64_t)cur_ptr);
                cur_ptr += 5;    // skip "_CRS" and AML_BUFFER_OP
                buf_len = parse_pkg_length(&cur_ptr);
                // val_print(ACS_PRINT_INFO, "_CRS buf len: 0x%lx\n", buf_len);
                if (buf_len > PCIE_CRS_LEN) {
                  parse_crs(cur_ptr, cur_ptr + buf_len);
                }
                cur_ptr += buf_len;
            } else {
              cur_ptr++;
            }
          }
          host_num += 1;
      }
      aml_start += pkg_length;
    } else {
      aml_start += 1;
    }
  }

  return 0;
}


static
void
payload(void)
{
  uint32_t hart_index;
  uint32_t test_skip = 1;
  uint64_t dsdt_ptr;
  uint32_t index;

  hart_index = val_hart_get_index_mpid(val_hart_get_mpid());

  dsdt_ptr = val_pcie_get_info(PCIE_INFO_DSDT_PTR, 0);
  if (dsdt_ptr != 0) {
    val_print(ACS_PRINT_INFO, "dsdt_ptr = 0x%llx\n", dsdt_ptr);
    find_pci_host_bridges(dsdt_ptr);
  }

  if (host_num != 0) {
    test_skip = 0;
    val_print(ACS_PRINT_INFO, "host_num = %d\n", host_num);
    for (index = 0; index < host_num; index++) {
      if ((mmio_range[index].mem64_len == 0) || (mmio_range[index].mem32_len == 0)) {
        val_print(ACS_PRINT_ERR, "\n       PCIe Host Bridge have invalid mem64 or mem32", 0);
        val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
      }
    }
  }

  if (test_skip == 1) {
    val_print(ACS_PRINT_ERR, "\n       No Host Bridge in DSDT", 0);
    val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  }
  else
      val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_p006_entry(uint32_t num_hart)
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
