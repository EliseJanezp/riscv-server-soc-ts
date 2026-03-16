#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 11)
#define TEST_RULE  "ME_IOM_110_010"
#define TEST_DESC  "Check RCiEP and Governing IOMMU ATS capability               "

#define RISCV_IOMMU_REG_CAP         0x0
#define RISCV_IOMMU_CAP_ATS_MSK     (1 << 25)
#define RISCV_IOMMU_CAP_ATS_BIT     (25UL)

#define ATS_CAP_ID    0xF

static
uint8_t
check_pcie_ext_cap_ats(uint32_t bdf, uint32_t ext_cap_id)
{
  uint32_t next_cap_offset;
  uint32_t reg_value;

  next_cap_offset = PCIE_ECAP_START;
  while (next_cap_offset)
  {
      val_pcie_read_cfg(bdf, next_cap_offset, &reg_value);
      if ((reg_value & PCIE_ECAP_CIDR_MASK) == ext_cap_id)
      {
          return 1;
      }
      next_cap_offset = ((reg_value >> PCIE_ECAP_NCPR_SHIFT) & PCIE_ECAP_NCPR_MASK);
  }

  return 0;
}

/**
 * @brief 1. Use PCIe discovery to locate all RCiEPs.
 * 2. For each RCiEP:
 *  a. If PCIe ATS capability not supported by the RCiEP then continue.
 *  b. Locate the governing IOMMU using ACPI RIMT table.
 *  c. Verify that the capabilities.ATS is 1 in the governing IOMMU.
 */
static
void
payload()
{
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t iommu_num = val_iommu_get_num();
  uint64_t reg_cap;
  uint32_t bdf = 0;
  uint32_t dp_type;
  uint32_t tbl_index = 0;
  uint32_t iommu_index = 0xff;
  uint8_t ret = 0xff;
  uint8_t ats_support = 0xff;
  pcie_device_bdf_table *bdf_tbl_ptr;

  bdf_tbl_ptr = val_pcie_bdf_table_ptr();

  while (tbl_index < bdf_tbl_ptr->num_entries) {

      bdf = bdf_tbl_ptr->device[tbl_index++].bdf;
      val_print(ACS_PRINT_DEBUG, "\n       BDF - 0x%x", bdf);

      dp_type = val_pcie_device_port_type(bdf);
      if (dp_type == RCiEP) {
        ats_support = check_pcie_ext_cap_ats(bdf, ATS_CAP_ID);
        if (ats_support == 1) {
          // Check IOMMU ATS Capability
          ret = val_iommu_check_governing_iommu (bdf, &iommu_index);
          if (ret == 1) {
            val_print(ACS_PRINT_ERR, "\n       Governing IOMMU index: 0x%x", iommu_index);
            if (iommu_index > iommu_num) {
              val_print(ACS_PRINT_ERR, "\n       Illegal Governing IOMMU index: 0x%x", iommu_index);
              val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
              return;
            }
            reg_cap = val_iommu_read_iommu_reg(iommu_index, RISCV_IOMMU_REG_CAP, 2);
            if (reg_cap & RISCV_IOMMU_CAP_ATS_MSK) {
              val_print(ACS_PRINT_INFO, "\n       IOMMU ATS capability is supported", 0);
              val_set_status(hart_index, RESULT_PASS(TEST_NUM, 0));
            } else {
              val_print(ACS_PRINT_ERR, "\n       IOMMU ATS capability is not supported", 0);
              val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
              return;
            }
          } else {
            val_print(ACS_PRINT_ERR, "\n       RCiEP Governing IOMMU is not exit", 0);
            val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
            return;
          }
        } else {
          val_print(ACS_PRINT_ERR, "\n       PCIe ATS capability not supported by the RCiEP", 0);
          continue;
        }
      }
  }

  val_print(ACS_PRINT_ERR, "\n       RCiEP device is not exit or not support ATS capability", 0);
  val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  return;
}

uint32_t
os_iom011_entry(uint32_t num_hart)
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
