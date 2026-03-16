#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 24)
#define TEST_RULE  "ME_IOM_240_010"
#define TEST_DESC  "Check RCiEP IOMMU device               "

#define PCIE_BASECLASS_IOMMU              0x08
#define PCIE_SUBCLASS_IOMMU               0x06

/**
 * @brief 1. Do a PCIe scan to locate all RCiEP of IOMMU class and report the
 * bus:device:function numbers of the IOMMUs in the test output log.
 */
static
void
payload()
{
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t bdf = 0;
  uint32_t dp_type;
  uint32_t class_code;
  uint32_t tbl_index = 0;
  pcie_device_bdf_table *bdf_tbl_ptr;

  bdf_tbl_ptr = val_pcie_bdf_table_ptr();

  while (tbl_index < bdf_tbl_ptr->num_entries) {
     bdf = bdf_tbl_ptr->device[tbl_index++].bdf;

    dp_type = val_pcie_device_port_type(bdf);
    if (dp_type == RCiEP) {
      val_pcie_read_cfg(bdf, TYPE01_RIDR, &class_code);
      if ((((class_code >> CC_BASE_SHIFT) & CC_BASE_MASK) == PCIE_BASECLASS_IOMMU) &&
           (((class_code >> CC_SUB_SHIFT) & CC_SUB_MASK)) == PCIE_SUBCLASS_IOMMU) {
        val_print(ACS_PRINT_DEBUG, "\n       Find RCiEP IOMMU Device, BDF - 0x%x", bdf);
        val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
        val_print(ACS_PRINT_ERR, "\n       Bus - %d", (bdf & 0x00ff0000) >> 16);
        val_print(ACS_PRINT_ERR, "\n       Dev - %d", (bdf & 0x0000ff00) >> 8);
        val_print(ACS_PRINT_ERR, "\n       Func - %d", bdf & 0x000000ff);
        return;
      }
    }
  }

  val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  return;
}

uint32_t
os_iom024_entry(uint32_t num_hart)
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
