#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include <sbi/riscv_asm.h>

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 22)
#define TEST_RULE  "ME_IOM_220_010"
#define TEST_DESC  "Check IOMMU PPN width               "

#define RISCV_IOMMU_REG_CAP          0x0
#define RISCV_IOMMU_CAP_PAS_MSK      (0x3FULL << 32)
#define RISCV_IOMMU_CAP_PAS_BIT      32UL

#define CSR_REG_HGATP                0x680
#define CSR_HGATP_MODE_MSK           (3ULL << 30)
#define CSR_HGATP_MODE_BIT           30UL


/**
 * @brief 1. Determine the width of the PPN field in hgatp and multiply that by 4096 to
 *  determine the PA size supported by the hart.
 * 2. Verify that the capabilities.PAS is greater than equal to the PA size supported
 *  by the hart.
 */
static
void
payload()
{
  uint32_t index;
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t iommu_num = val_iommu_get_num();
  uint64_t reg_cap;
  uint8_t iommu_pas;
  uint8_t hgatp_mode;
  uint8_t pa_bits;

  hgatp_mode = (csr_read(CSR_REG_HGATP) & CSR_HGATP_MODE_MSK) >> CSR_HGATP_MODE_BIT;
  val_print(ACS_PRINT_INFO, "\n       HGATP mode - 0x%x", hgatp_mode);
  if (hgatp_mode == 0) {
    val_print(ACS_PRINT_ERR, "\n       HGATP mode is bare mode- no translation or protection", 0);
    val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
    return;
  }

  switch (hgatp_mode) {
    case 0x8:           // Sv39/Sv39x4
    case 0x9:           // Sv48/Sv48x4
    case 0xA:           // Sv57/Sv57x4
      pa_bits = 56 - 12; 
      break;
    case 0x1:           // Sv32x4 (RV32)
      pa_bits = 34 - 12;
      break;
  }

  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      reg_cap = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_CAP, 2);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap - 0x%lx", reg_cap);
      iommu_pas = (reg_cap & RISCV_IOMMU_CAP_PAS_MSK) >> RISCV_IOMMU_CAP_PAS_BIT;
      val_print(ACS_PRINT_INFO, "\n       IOMMU PAS - 0x%x", iommu_pas);
      if (iommu_pas <= pa_bits*4096) {
        val_print(ACS_PRINT_ERR, "\n       IOMMU PAS is not greater than equal to PA size", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }
    }
  }

  val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  return;
}

uint32_t
os_iom022_entry(uint32_t num_hart)
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
