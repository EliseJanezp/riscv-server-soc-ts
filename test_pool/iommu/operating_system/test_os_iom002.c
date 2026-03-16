#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 2)
#define TEST_RULE  "ME_IOM_020_010"
#define TEST_DESC  "Verify RCiEP and RP exit governing IOMMU               "

/**
 * @brief For each application processor hart:
 * 1. Use PCIe discovery to locate all RCiEPs and PCIe RPs.
 * 2. Locate the ACPI RIMT tables of all IOMMUs.
 * 3. For each RCiEP, verify that there is a governing IOMMU.
 * 4. For each RP, verify that there is a governing IOMMU.
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

      dp_type = val_pcie_device_port_type(bdf);
      if (dp_type == RCiEP || dp_type == RP) {
        val_print(ACS_PRINT_DEBUG, "\n       BDF - 0x%x", bdf);
        val_print(ACS_PRINT_DEBUG, "\n       DP TYPE - %d", dp_type);
        ret = val_iommu_check_governing_iommu (bdf, &iommu_index);
        if (ret == 0) {
          val_print(ACS_PRINT_ERR, "\n       IOMMU is not governing for bdf: 0x%x", bdf);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
          return;
        }
      }
  }

  if (ret == 1) {
    val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  } else {
    val_print(ACS_PRINT_ERR, "\n       SKIP: NO RCiEP AND RP DEVICE", 0);
    val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  }

  return;
}

uint32_t
os_iom002_entry(uint32_t num_hart)
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
