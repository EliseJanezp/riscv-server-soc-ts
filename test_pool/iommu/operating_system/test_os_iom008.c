#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 8)
#define TEST_RULE  "ME_IOM_080_010"
#define TEST_DESC  "Check IOMMU MSI MRIF AND AMO MRIF               "

#define RISCV_IOMMU_REG_CAP              0x0
#define RISCV_IOMMU_CAP_MSI_MRIF_MSK     (1 << 23)
#define RISCV_IOMMU_CAP_AMO_MRIF_MSK     (1 << 21)
#define RISCV_IOMMU_CAP_MSI_MRIF_BIT     (23UL)
#define RISCV_IOMMU_CAP_AMO_MRIF_BIT     (21UL)


/**
 * @brief For each IOMMU, verify that if capabilities.MSI_MRIF is equal to
 * capabilities.AMO_MRIF
 */
static
void
payload()
{
  uint32_t index;
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t iommu_num = val_iommu_get_num();
  uint64_t reg_cap;

  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      reg_cap = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_CAP, 2);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap - 0x%lx", reg_cap);
      if (((reg_cap & RISCV_IOMMU_CAP_MSI_MRIF_MSK) >> RISCV_IOMMU_CAP_MSI_MRIF_BIT) \
          != (reg_cap & RISCV_IOMMU_CAP_AMO_MRIF_MSK) >> RISCV_IOMMU_CAP_AMO_MRIF_BIT) {
        val_print(ACS_PRINT_ERR, "\n       IOMMU reg_cap MSI_MRIF - 0x%lx", (reg_cap & RISCV_IOMMU_CAP_MSI_MRIF_MSK) >> RISCV_IOMMU_CAP_MSI_MRIF_BIT);
        val_print(ACS_PRINT_ERR, "\n       IOMMU reg_cap AMO_MRIF - 0x%lx", (reg_cap & RISCV_IOMMU_CAP_AMO_MRIF_MSK) >> RISCV_IOMMU_CAP_AMO_MRIF_BIT);
        val_print(ACS_PRINT_ERR, "\n       MSI_MRIF NOT EQUAL TO AMO MRIF", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }
    }
  }

  val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_iom008_entry(uint32_t num_hart)
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
