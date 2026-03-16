#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 17)
#define TEST_RULE  "ME_IOM_170_010"
#define TEST_DESC  "Check IOMMU PDT support               "

#define RISCV_IOMMU_REG_CAP          0x0
#define RISCV_IOMMU_CAP_PD8_MSK      (1ULL << 38)
#define RISCV_IOMMU_CAP_PD17_MSK     (1ULL << 39)
#define RISCV_IOMMU_CAP_PD20_MSK     (1ULL << 40)
#define RISCV_IOMMU_CAP_PD8_BIT      38UL
#define RISCV_IOMMU_CAP_PD17_BIT     39UL
#define RISCV_IOMMU_CAP_PD20_BIT     40UL

/**
 * @brief For each IOMMU, verify that if any of the PD8, PD17, or PD20 bits are 1 in the
 * capabilities register then PD20 bit must be 1.
 * 
 * IOMMU that supports PASID capability MUST support 20-bit PASID width and MAY support
 * 8-bit and 17-bit PASID widths.
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
      if (((reg_cap & RISCV_IOMMU_CAP_PD8_MSK) >> RISCV_IOMMU_CAP_PD8_BIT) == 1 \
        || ((reg_cap & RISCV_IOMMU_CAP_PD17_MSK) >> RISCV_IOMMU_CAP_PD17_BIT) == 1 \
        || ((reg_cap & RISCV_IOMMU_CAP_PD20_MSK) >> RISCV_IOMMU_CAP_PD20_BIT) == 1) {
          if (((reg_cap & RISCV_IOMMU_CAP_PD20_MSK) >> RISCV_IOMMU_CAP_PD20_BIT) != 1) {
            val_print(ACS_PRINT_ERR, "\n       IOMMU reg_cap PD8- 0x%lx", (reg_cap & RISCV_IOMMU_CAP_PD8_MSK) >> RISCV_IOMMU_CAP_PD8_BIT);
            val_print(ACS_PRINT_ERR, "\n       IOMMU reg_cap PD17- 0x%lx", (reg_cap & RISCV_IOMMU_CAP_PD17_MSK) >> RISCV_IOMMU_CAP_PD17_BIT);
            val_print(ACS_PRINT_ERR, "\n       IOMMU reg_cap PD20- 0x%lx", (reg_cap & RISCV_IOMMU_CAP_PD20_MSK) >> RISCV_IOMMU_CAP_PD20_BIT);
            val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
            return;
          }
      }
    }
  }

  val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  return;
}

uint32_t
os_iom017_entry(uint32_t num_hart)
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
