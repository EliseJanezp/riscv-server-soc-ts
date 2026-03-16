#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 14)
#define TEST_RULE  "ME_IOM_140_010, OE_IOM_150_010"
#define TEST_DESC  "Check IOMMU BIG-ENDIAN               "

#define RISCV_IOMMU_REG_FCTL         0x8
#define RISCV_IOMMU_FCTL_BE_MSK      1

#define RISCV_IOMMU_FCTL_BE_RO_0      1
#define RISCV_IOMMU_FCTL_BE_W         2

/**
 * @brief For each IOMMU, verify that if fctl.BE is either read-only zero or is writeable. Verify
 * that the support is identical for all IOMMUs. If big-endian mode supported then emit
 * the support status in the test output log.
 */
static
void
payload()
{
  uint32_t index;
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t iommu_num = val_iommu_get_num();
  uint64_t reg_fctl_old;
  uint64_t reg_fctl;
  uint64_t reg_fctl_new;
  static uint8_t be_mode = 0;
  static uint8_t be_mode_next = 0x0;

  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      reg_fctl_old = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_FCTL, 1);
      val_print(ACS_PRINT_INFO, "\n       IOMMU read reg_fctl - 0x%lx", reg_fctl_old);
      reg_fctl = reg_fctl_old;
      if ((reg_fctl & RISCV_IOMMU_FCTL_BE_MSK) == 0) {
        be_mode_next = RISCV_IOMMU_FCTL_BE_RO_0;
        reg_fctl |= RISCV_IOMMU_FCTL_BE_MSK;
      } else {
        val_print(ACS_PRINT_ERR, "\n       IOMMU fctl support big-endian", 0);
        reg_fctl &= ~RISCV_IOMMU_FCTL_BE_MSK;
      }
      // Write IOMMU fctl reg to 1 or 0 and read it back to check if it is writeable
      val_print(ACS_PRINT_INFO, "\n       IOMMU write reg_fctl - 0x%lx", reg_fctl);
      val_iommu_write_iommu_reg(index, RISCV_IOMMU_REG_FCTL, 1, reg_fctl);
      reg_fctl_new = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_FCTL, 1);
      val_print(ACS_PRINT_INFO, "\n       IOMMU read reg_fctl again - 0x%lx", reg_fctl_new);
      if ((reg_fctl_new == reg_fctl)) {
        be_mode_next = RISCV_IOMMU_FCTL_BE_W;
        val_print(ACS_PRINT_ERR, "\n       IOMMU fctl.be is writeable", 0);
        if ((reg_fctl & RISCV_IOMMU_FCTL_BE_MSK) == 1) {
          val_print(ACS_PRINT_ERR, "\n       IOMMU fctl support big-endian", 0);
        }
        // restore the original value
        val_iommu_write_iommu_reg(index, RISCV_IOMMU_REG_FCTL, 1, reg_fctl_old);
      }

      if ((be_mode_next != RISCV_IOMMU_FCTL_BE_RO_0) && (be_mode_next != RISCV_IOMMU_FCTL_BE_W)) {
        val_print(ACS_PRINT_ERR, "\n       IOMMU fctl.be not read only zero and writeable", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }

      if (index > 0) {
        if (be_mode_next != be_mode) {
          val_print(ACS_PRINT_ERR, "\n       be_mode_next:0x%x", be_mode_next);
          val_print(ACS_PRINT_ERR, "\n       be_mode:0x%x", be_mode);
          val_print(ACS_PRINT_ERR, "\n       IOMMU fctl.be not consistency", 0);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
          return;
        }
      }
      be_mode = be_mode_next;
    }
  }

  val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  return;
}

uint32_t
os_iom014_entry(uint32_t num_hart)
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
