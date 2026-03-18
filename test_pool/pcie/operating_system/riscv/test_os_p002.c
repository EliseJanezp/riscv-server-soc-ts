/** @file
 * Copyright (c) 2016-2018, 2021-2023 Arm Limited or its affiliates. All rights reserved.
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

#define DEVICE_NUM_PER_BUS   256
#define MEM_SIZE_PER_DEVICE  4096
#define MAX_ECAM_NUM         20

#define TEST_NUM   (ACS_PCIE_TEST_NUM_BASE + 2)
#define TEST_RULE  "MF_ECM_030_010, MF_ECM_040_010"
#define TEST_DESC  "Check the continuity and non-overlapping of ECAM at different hierarchies  "

typedef struct {
  uint64_t base;
  uint64_t size;
  uint32_t segment;
  uint32_t start_bus;
  uint32_t end_bus;
} ecam_space;

static ecam_space ecam_data[MAX_ECAM_NUM];

static void *branch_to_test;

static
void
esr(uint64_t interrupt_type, void *context)
{
  uint32_t hart_index;

  hart_index = val_hart_get_index_mpid(val_hart_get_mpid());

  /* Update the ELR to return to test specified address */
  val_hart_update_elr(context, (uint64_t)branch_to_test);

  val_print(ACS_PRINT_INFO, "\n       Received exception of type: %d", interrupt_type);
  val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
}

void sort_ecam_data(uint32_t num_ecam) 
{
  ecam_space temp;
  uint8_t i, j;

  for (i = 0; i < num_ecam - 1; i++) {
    for (j = 0; j < num_ecam - 1 - i; j++) {
      if (ecam_data[j].segment > ecam_data[j + 1].segment ||
        (ecam_data[j].segment == ecam_data[j + 1].segment &&
        ecam_data[j].start_bus > ecam_data[j + 1].start_bus)) {
          temp = ecam_data[j];
          ecam_data[j] = ecam_data[j + 1];
          ecam_data[j + 1] = temp;
      }
    }
  }
}

static
void
payload(void)
{

  uint32_t num_ecam;
  uint64_t ecam_base;
  uint64_t ecam_size;
  uint32_t index;
  uint32_t bus, next_bus, segment;
  uint32_t end_bus;
  uint32_t status;
  uint32_t ecam_index;
  uint64_t current_ecam_base = 0;
  uint64_t current_ecam_size = 0;
  uint64_t next_ecam_base = 0;
  uint64_t next_ecam_size = 0;

  index = val_hart_get_index_mpid(val_hart_get_mpid());

  /* Install sync and async handlers to handle exceptions.*/
  status = val_hart_install_esr(EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, esr);
  status |= val_hart_install_esr(EXCEPT_AARCH64_SERROR, esr);
  if (status)
  {
      val_print(ACS_PRINT_ERR, "\n      Failed in installing the exception handler", 0);
      val_set_status(index, RESULT_FAIL(TEST_NUM, 1));
      return;
  }

  branch_to_test = &&exception_return;

  num_ecam = val_pcie_get_info(PCIE_INFO_NUM_ECAM, 0);
  if (num_ecam == 0) {
      val_print(ACS_PRINT_DEBUG, "\n       No ECAM in MCFG                   ", 0);
      val_set_status(index, RESULT_SKIP(TEST_NUM, 1));
      return;
  }

  while (num_ecam) {
      num_ecam--;
      ecam_base = val_pcie_get_info(PCIE_INFO_ECAM, num_ecam);
      if (ecam_base == 0) {
          val_print(ACS_PRINT_ERR, "\n       ECAM Base in MCFG is 0            ", 0);
          val_set_status(index, RESULT_SKIP(TEST_NUM, 1));
          return;
      }

      segment = val_pcie_get_info(PCIE_INFO_SEGMENT, num_ecam);
      bus = val_pcie_get_info(PCIE_INFO_START_BUS, num_ecam);
      end_bus = val_pcie_get_info(PCIE_INFO_END_BUS, num_ecam);

      ecam_size = (end_bus - bus + 1) * DEVICE_NUM_PER_BUS * MEM_SIZE_PER_DEVICE;
      ecam_data[num_ecam].base = ecam_base;
      ecam_data[num_ecam].size = ecam_size;
      ecam_data[num_ecam].segment = segment;
      ecam_data[num_ecam].start_bus = bus;
      ecam_data[num_ecam].end_bus = end_bus;
      val_print(ACS_PRINT_INFO, "\n       ECAM Base in MCFG is 0x%lx", ecam_base);
      val_print(ACS_PRINT_INFO, "\n       ECAM Base in MCFG segment is 0x%lx", segment);
      val_print(ACS_PRINT_INFO, "\n       ECAM Base in MCFG start_bus is 0x%lx", bus);
      val_print(ACS_PRINT_INFO, "\n       ECAM Base in MCFG end_bus is 0x%lx", end_bus);
      val_print(ACS_PRINT_INFO, "\n       ECAM Base in MCFG size is 0x%lx", ecam_size);
  }

  // Sort ECAM data by segment and start_bus
  if (num_ecam > 1) {
    sort_ecam_data(num_ecam);
  }

  // Check ECAM continuity and non-overlapping
  for (ecam_index = 0; ecam_index < num_ecam; ecam_index++) {
    current_ecam_base = ecam_data[ecam_index].base;
    current_ecam_size = ecam_data[ecam_index].size;
    if ((ecam_index + 1) < num_ecam) {
      // Check BUS continuity under same segment
      if (ecam_data[ecam_index].segment == ecam_data[ecam_index + 1].segment) {
        bus = ecam_data[ecam_index].start_bus;
        end_bus = ecam_data[ecam_index].end_bus;
        next_bus = ecam_data[ecam_index + 1].start_bus;
        if ((end_bus + 1) != next_bus) {
          val_print(ACS_PRINT_ERR, "\n       Bus in MCFG is not continuous", 0);
          val_set_status(index, RESULT_FAIL(TEST_NUM, 1));
          return;
        }

        // Check ECAM address continuity
        next_ecam_base = ecam_data[ecam_index + 1].base;
        if ((current_ecam_base + current_ecam_size) != next_ecam_base) {
          val_print(ACS_PRINT_ERR, "\n       ECAM %d base address is not continuous            ", ecam_index);
          val_set_status(index, RESULT_FAIL(TEST_NUM, 1));
          return;
        }
      } else if (ecam_data[ecam_index].segment != ecam_data[ecam_index + 1].segment) {
        // Check ECAM address overlap between different segments
        next_ecam_base = ecam_data[ecam_index + 1].base;
        next_ecam_size = ecam_data[ecam_index + 1].size;
        if (((current_ecam_base + current_ecam_size) >= next_ecam_base)
          && ((next_ecam_base + next_ecam_size) >= current_ecam_base)) {
          val_print(ACS_PRINT_ERR, "\n       ECAM %d base address is overlap            ", ecam_index);
          val_set_status(index, RESULT_FAIL(TEST_NUM, 1));
          return;
        }
      }
    }

    // Check ECAM address alignment with size
    if (current_ecam_base % current_ecam_size != 0) {
      val_print(ACS_PRINT_ERR, "\n       ECAM %d base address is not naturally aligned to the size            ", ecam_index);
      val_set_status(index, RESULT_FAIL(TEST_NUM, 1));
      return;
    }
  }

  val_set_status(index, RESULT_PASS(TEST_NUM, 1));

exception_return:
  return;
}

uint32_t
os_p002_entry(uint32_t num_hart)
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
