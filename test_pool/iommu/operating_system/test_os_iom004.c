#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 4)
#define TEST_RULE  "ME_IOM_040_010"
#define TEST_DESC  "Check IOMMUs which governing platform device ddtp mode and ddtp context width               "

#define RISCV_IOMMU_REG_CAP             0x0
#define RISCV_IOMMU_CAP_MSIFLAT_MSK     (1 << 22)
#define MSIFLAT_ENABLE                  (1 << 22)

#define RISCV_IOMMU_REG_DDTP            0x10
#define RISCV_IOMMU_DDTP_LVL_MSK        0xf
#define RISCV_IOMMU_DDTP_LVL_1          2
#define RISCV_IOMMU_DDTP_LVL_2          3
#define RISCV_IOMMU_DDTP_LVL_3          4

#define DDTP_LVL_1_BASE_WIDTH          7
#define DDTP_LVL_2_BASE_WIDTH          16
#define DDTP_LVL_3_BASE_WIDTH          24

#define DDTP_LVL_1_EXTENDED_WIDTH      6
#define DDTP_LVL_2_EXTENDED_WIDTH      15
#define DDTP_LVL_3_EXTENDED_WIDTH      24

static
uint8_t
check_iommu_cap_ddtp_reg(uint64_t reg_cap, uint64_t reg_ddtp, uint64_t max_deviceid)
{
    uint8_t ddtp_lvl1_bit = 0;
    uint8_t ddtp_lvl2_bit = 0;
    uint8_t ddtp_lvl3_bit = 0;
    uint8_t ddtp_lvl_bit = 0;

    if ((reg_cap & RISCV_IOMMU_CAP_MSIFLAT_MSK) != MSIFLAT_ENABLE) {
        val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap msi_flat- 0", 0);
        ddtp_lvl1_bit = DDTP_LVL_1_BASE_WIDTH;
        ddtp_lvl2_bit = DDTP_LVL_2_BASE_WIDTH;
        ddtp_lvl3_bit = DDTP_LVL_3_BASE_WIDTH;
    } else {
        val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap msi_flat- 1", 0);
        ddtp_lvl1_bit = DDTP_LVL_1_EXTENDED_WIDTH;
        ddtp_lvl2_bit = DDTP_LVL_2_EXTENDED_WIDTH;
        ddtp_lvl3_bit = DDTP_LVL_3_EXTENDED_WIDTH;
    }
    if ((reg_ddtp & RISCV_IOMMU_DDTP_LVL_MSK) == RISCV_IOMMU_DDTP_LVL_1) {
        ddtp_lvl_bit = ddtp_lvl1_bit;
    } else if ((reg_ddtp & RISCV_IOMMU_DDTP_LVL_MSK) == RISCV_IOMMU_DDTP_LVL_2) {
        ddtp_lvl_bit = ddtp_lvl2_bit;
    } else if ((reg_ddtp & RISCV_IOMMU_DDTP_LVL_MSK) == RISCV_IOMMU_DDTP_LVL_3) {
        ddtp_lvl_bit = ddtp_lvl3_bit;
    }

    if (max_deviceid > (2 << ddtp_lvl_bit)) {
        val_print(ACS_PRINT_ERR, "\n       IOMMU does not support the device id width", 0);
        return 0;
    } else {
        return 1;
    }

    return 0;
}

static
uint8_t
check_governing_iommu_info(uint32_t iommu_index, uint32_t iommu_offset)
{
  uint32_t iommu_num = val_iommu_get_num();
  uint32_t destiommu_offset;
  uint32_t index;
  uint64_t reg_cap, reg_ddtp;
  uint32_t destid_base;
  uint32_t numids;
  uint32_t idmap_index;
  uint16_t idmap_num;

  if (iommu_index >= iommu_num) {
    val_print(ACS_PRINT_ERR, "\n       IOMMU index error - 0x%x", iommu_index);
    return 0;
  }

  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) != EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
        idmap_num = val_iommu_get_idmap_num (index);
        for (idmap_index = 0; idmap_index < idmap_num; idmap_index++) {
            destiommu_offset = val_iommu_get_pcierc_platform_info (index, idmap_index, IOMMU_INFO_DEST_IOMMU_OFFSET);
            val_print(ACS_PRINT_INFO, "\n       IOMMU PCIe DEST IOMMU OFFSET: 0x%x", destiommu_offset);
            if (destiommu_offset == iommu_offset) {
                destid_base = val_iommu_get_pcierc_platform_info (index, idmap_index, IOMMU_INFO_DEST_ID_BASE);
                numids = val_iommu_get_pcierc_platform_info (index, idmap_index, IOMMU_INFO_NUM_IDS);
                val_print(ACS_PRINT_INFO, "\n       IOMMU PCIe DEST ID BASE: 0x%x", destid_base);
                val_print(ACS_PRINT_INFO, "\n       IOMMU PCIe NUM IDS: 0x%x", numids);
                reg_cap = val_iommu_read_iommu_reg(iommu_index, RISCV_IOMMU_REG_CAP, 2);
                reg_ddtp = val_iommu_read_iommu_reg(iommu_index, RISCV_IOMMU_REG_DDTP, 2);
                return check_iommu_cap_ddtp_reg(reg_cap, reg_ddtp, destid_base + numids);
            }
        }
    }
  }

  return 0xff;
}

/**
 * @brief For each IOMMU that does not govern a PCIe root port: . Parse the ACPI RIMT
structure of that IOMMU to determine the widest device ID. . Verify that the ddtp
supports a mode that supports the widest device ID.
 */
static
void
payload()
{
  uint32_t index;
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t iommu_num = val_iommu_get_num();
  uint8_t iommu_rp_flag[iommu_num];
  uint32_t bdf = 0;
  uint32_t dp_type;
  uint32_t tbl_index = 0;
  uint8_t ret = 0xff;
  pcie_device_bdf_table *bdf_tbl_ptr;
  uint32_t iommu_index;
  uint32_t iommu_offset;

  bdf_tbl_ptr = val_pcie_bdf_table_ptr();

  for (index = 0; index < iommu_num; index++) {
    iommu_rp_flag[index] = 0;
  }

  while (tbl_index < bdf_tbl_ptr->num_entries) {

      bdf = bdf_tbl_ptr->device[tbl_index++].bdf;
      val_print(ACS_PRINT_DEBUG, "\n       BDF - 0x%x", bdf);

      dp_type = val_pcie_device_port_type(bdf);
      if (dp_type == RP) {
        ret = val_iommu_check_governing_iommu (bdf, &iommu_index);
        if (ret == 1) {
          val_print(ACS_PRINT_ERR, "\n       Governing IOMMU index: 0x%x", iommu_index);
          iommu_rp_flag [iommu_index] = 1;
        }
      }
  }

  for (index = 0; index < iommu_num; index++) {
    if (iommu_rp_flag[index] == 1) {
      continue;  // IOMMU is a RP IOMMU, skip it.
    } else {
      iommu_offset = val_iommu_get_info (index, IOMMU_INFO_IOMMU_OFFSET);
      ret = check_governing_iommu_info(index, iommu_offset);
      if (ret == 0) {
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
      }
    }
  }

  if (ret == 1) {
    val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  } else {
    val_print(ACS_PRINT_ERR, "\n       SKIP: NO NON PCIE RP GOVERNING IOMMU NODE", 0);
    val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  }

  return;
}

uint32_t
os_iom004_entry(uint32_t num_hart)
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
