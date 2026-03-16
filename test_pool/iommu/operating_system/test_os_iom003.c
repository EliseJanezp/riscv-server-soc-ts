#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 3)
#define TEST_RULE  "ME_IOM_030_010"
#define TEST_DESC  "Check IOMMUs which governing PCIe root ports capabilities and ddtp register               "

#define RISCV_IOMMU_REG_CAP             0x0
#define RISCV_IOMMU_CAP_MSIFLAT_MSK     (1 << 22)
#define MSIFLAT_ENABLE                  (1 << 22)

#define RISCV_IOMMU_REG_DDTP            0x10
#define RISCV_IOMMU_DDTP_LVL_MSK        0xf
#define RISCV_IOMMU_DDTP_LVL_2          3
#define RISCV_IOMMU_DDTP_LVL_3          4

static
uint8_t
check_iommu_cap_ddtp_reg (uint32_t index)
{
  uint64_t reg_cap, reg_ddtp;
  uint32_t iommu_num = val_iommu_get_num();

  if (index >= iommu_num) {
    val_print(ACS_PRINT_ERR, "\n       IOMMU index error - 0x%x", index);
    return 0;
  }

  if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
    reg_cap = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_CAP, 2);
    reg_ddtp = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_DDTP, 2);

    val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap - 0x%lx", reg_cap);
    val_print(ACS_PRINT_INFO, "\n       IOMMU reg_ddtp - 0x%lx", reg_ddtp);
    val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap ddtp level- 0x%x", reg_ddtp & RISCV_IOMMU_DDTP_LVL_MSK);
    if ((reg_cap & RISCV_IOMMU_CAP_MSIFLAT_MSK) != MSIFLAT_ENABLE) {
        val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap msi_flat- 0", 0);
        if (((reg_ddtp & RISCV_IOMMU_DDTP_LVL_MSK) == RISCV_IOMMU_DDTP_LVL_2) || \
            ((reg_ddtp & RISCV_IOMMU_DDTP_LVL_MSK) == RISCV_IOMMU_DDTP_LVL_3)) {
            return 1;
        }
    } else {
        val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap msi_flat- 1", 0);
        if (((reg_ddtp & RISCV_IOMMU_DDTP_LVL_MSK) == RISCV_IOMMU_DDTP_LVL_3)) {
            return 1;
        }
    }
  }

  return 0;
}

/**
 * @brief For each application processor hart:
 * 1. Locate all IOMMUs governing PCIe root ports.
 * 2. For each located IOMMU:
 *   a. if capabilities.MSI_FLAT is 0, then the ddtp must support at least 2 level DDT.
 *   b. if capabilities.MSI_FLAT is 1, then the ddtp must support 3 level DDT.
 */
static
void
payload()
{
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t bdf = 0;
  uint32_t dp_type;
  uint32_t tbl_index = 0;
  uint8_t ret = 0xff;
  pcie_device_bdf_table *bdf_tbl_ptr;
  uint32_t iommu_index = 0;

  bdf_tbl_ptr = val_pcie_bdf_table_ptr();

  while (tbl_index < bdf_tbl_ptr->num_entries) {

      bdf = bdf_tbl_ptr->device[tbl_index++].bdf;
      val_print(ACS_PRINT_DEBUG, "\n       BDF - 0x%x", bdf);

      dp_type = val_pcie_device_port_type(bdf);
      if (dp_type == RP) {
        ret = val_iommu_check_governing_iommu (bdf, &iommu_index);
        if (ret == 0) {
          val_print(ACS_PRINT_ERR, "\n       IOMMU is not governing for bdf: 0x%x", bdf);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
          return;
        } else {
          val_print(ACS_PRINT_INFO, "\n       IOMMU is governing for bdf: 0x%x", bdf);
          val_print(ACS_PRINT_INFO, "\n       Governing IOMMU index: 0x%x", iommu_index);
          ret = check_iommu_cap_ddtp_reg (iommu_index);
          if (ret == 0) {
            val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
            return;
          }
        }
      }
  }

  if (ret == 1) {
    val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  } else {
    val_print(ACS_PRINT_ERR, "\n       SKIP: NO RP GOVERNING IOMMU NODE", 0);
    val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  }
  return;
}

uint32_t
os_iom003_entry(uint32_t num_hart)
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
