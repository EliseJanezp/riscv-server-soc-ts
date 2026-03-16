#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 5)
#define TEST_RULE  "ME_IOM_050_010"
#define TEST_DESC  "Check IOMMU supported page mmu mode               "

#define RISCV_IOMMU_REG_CAP             0x0
#define RISCV_IOMMU_CAP_SV32_MSK        (1 << 8)
#define RISCV_IOMMU_CAP_SV39_MSK        (1 << 9)
#define RISCV_IOMMU_CAP_SV48_MSK        (1 << 10)
#define RISCV_IOMMU_CAP_SV57_MSK        (1 << 11)

#define SATP_MODE_SV39  0UL
#define SATP_MODE_SV48  1UL
#define SATP_MODE_SV57  2UL
#define SATP_MODE_SV64  3UL

/**
 * @brief For each application processor hart:
 * 1. Parse ISA string in ACPI RHCT table and determine the page based virtual
 *    memory systems supported by the harts.
 * 2. For each IOMMU in reported:
 *    a. Verify that the capabilities register enumerates support for each of the
 *    page based virtual memory system modes supported by the harts.
 */
static
void
payload()
{
  uint32_t index;
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t iommu_num = val_iommu_get_num();
  uint64_t reg_cap;
  char8_t *isa_string;
  char8_t *ptr;
  uint8_t mmu_type = 0xff;

  isa_string = val_hart_get_isa_string(hart_index);

  ptr = val_strstr(isa_string, "sv39");
  if (ptr != NULL) {
    mmu_type = SATP_MODE_SV39;
  } else {
    ptr = val_strstr(isa_string, "sv48");
    if (ptr != NULL) {
      mmu_type = SATP_MODE_SV48;
    } else {
      ptr = val_strstr(isa_string, "sv57");
      if (ptr != NULL) {
        mmu_type = SATP_MODE_SV57;
      } else {
        val_print(ACS_PRINT_ERR, "\n       ISA string does not contain sv39, sv48 or sv57", 0);
        mmu_type = val_hart_get_mmu_type(hart_index);
      }
    }
  }
  val_print(ACS_PRINT_INFO, "\n       HART MMU TYPE: %d", mmu_type);
  if (mmu_type == 0xff) {
    val_print(ACS_PRINT_ERR, "\n       Did not found supportted mmu type", 0);
    val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
    return;
  }

  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      reg_cap = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_CAP, 2);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap - 0x%lx", reg_cap);
      if ((mmu_type == SATP_MODE_SV39) && (reg_cap & RISCV_IOMMU_CAP_SV39_MSK) != RISCV_IOMMU_CAP_SV39_MSK) {
        val_print(ACS_PRINT_ERR, "\n       Unsupportted MMU TYPE : SATP_MODE_SV39", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      } else if ((mmu_type == SATP_MODE_SV48) && (reg_cap & RISCV_IOMMU_CAP_SV48_MSK) != RISCV_IOMMU_CAP_SV48_MSK) {
        val_print(ACS_PRINT_ERR, "\n       Unsupportted MMU TYPE : SATP_MODE_SV48", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      } else if ((mmu_type == SATP_MODE_SV57) && (reg_cap & RISCV_IOMMU_CAP_SV57_MSK) != RISCV_IOMMU_CAP_SV57_MSK) {
        val_print(ACS_PRINT_ERR, "\n       Unsupportted MMU TYPE : SATP_MODE_SV57", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }
    }
  }

  val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  return;
}

uint32_t
os_iom005_entry(uint32_t num_hart)
{

  uint32_t status = ACS_STATUS_FAIL;

  num_hart = 1;  //This IOMMU test is run on single processor
  // TODO: test all processor

  status = val_initialize_test(TEST_NUM, TEST_DESC, num_hart);

  if (status != ACS_STATUS_SKIP)
      val_run_test_payload(TEST_NUM, num_hart, payload, 0);

  /* get the result from all HART and check for failure */
  status = val_check_for_error(TEST_NUM, num_hart, TEST_RULE);

  val_report_status(0, BSA_ACS_END(TEST_NUM), NULL);

  return status;
}
