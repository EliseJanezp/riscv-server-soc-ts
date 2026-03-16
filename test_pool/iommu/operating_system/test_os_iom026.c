#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 26)
#define TEST_RULE  "ME_IOM_260_010"
#define TEST_DESC  "Check IOMMU DDTP which governing multiple segment PCIe RC               "

#define RISCV_IOMMU_REG_DDTP            0x10
#define RISCV_IOMMU_DDTP_LVL_MSK        0xf
#define RISCV_IOMMU_DDTP_LVL_1          2
#define RISCV_IOMMU_DDTP_LVL_2          3
#define RISCV_IOMMU_DDTP_LVL_3          4

#define IOMMU_MAP_MAX             30

typedef struct {
  uint32_t iommu_index;
  uint32_t pcierc_index;
  uint32_t pcierc_seg;
} IOMMU_MAP;

static
uint8_t
check_iommu_ddtp_level(uint32_t iommu_index)
{
  uint32_t iommu_num = val_iommu_get_num();
  uint64_t reg_ddtp;

  if (iommu_index >= iommu_num) {
    val_print(ACS_PRINT_ERR, "\n       IOMMU index error - 0x%x", iommu_index);
    return 0;
  }

  reg_ddtp = val_iommu_read_iommu_reg(iommu_index, RISCV_IOMMU_REG_DDTP, 2);
  val_print(ACS_PRINT_INFO, "\n       IOMMU reg_ddtp - 0x%lx", reg_ddtp);
  if ((reg_ddtp & RISCV_IOMMU_DDTP_LVL_MSK) != RISCV_IOMMU_DDTP_LVL_3) {
    val_print(ACS_PRINT_ERR, "\n       IOMMU ddtp level id not 3 level - 0x%x", reg_ddtp & RISCV_IOMMU_DDTP_LVL_MSK);
    return 0;
  } 

  return 1;
}

static
uint8_t
check_governing_iommu (uint32_t destiommu_offset, uint32_t *iommu_index)
{
  uint32_t iommu_num = val_iommu_get_num();
  uint32_t iommu_offset;
  uint32_t index;

  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      iommu_offset = val_iommu_get_info (index, IOMMU_INFO_IOMMU_OFFSET);
      if (destiommu_offset == iommu_offset) {
        *iommu_index = index;
        return 1;
      }
    }
  }
  return 0;
}

/**
 * @brief 1. Parse the PCIe root complex device binding structures from ACPI RIMT table
 *   and build a mapping of root complexes associated with each IOMMU.
 * 2. For each IOMMU determine the PCIe segment number of the associated PCIe
 *   root complexes and create a list of IOMMUs that govern multiple root complexes
 *   where the PCIe root complexes belong to two or more PCIe segments.
 * 3. For each IOMMU that governs PCIe root complexes that are part of different
 *   PCIe segments verify that the ddtp supports 3 level DDT.
 */
static
void
payload()
{
  uint32_t index,index1;
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t iommu_num = val_iommu_get_num();
  uint8_t ret = 0xff;
  uint32_t iommu_index;
  uint32_t iommu_offset;
  uint8_t map_index = 0;
  uint8_t iommu_multi_list[iommu_num];
  uint8_t idmap_num;
  uint8_t idmap_index;
  IOMMU_MAP iommu_map[IOMMU_MAP_MAX];

  for (index = 0; index < IOMMU_MAP_MAX; index++) {
    iommu_map[index].iommu_index = 0xff;
  }
  for (index = 0; index < iommu_num; index++) {
    iommu_multi_list[index] = 0xff;
  }

  //Get the PCIe root complex device binding structures from ACPI RIMT table
  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_PCIE_ROOT_COMPLEX) {
      idmap_num = val_iommu_get_idmap_num (index);
      for (idmap_index = 0; idmap_index < idmap_num; idmap_index++) {
        iommu_offset = val_iommu_get_pcierc_platform_info (index, idmap_index, IOMMU_INFO_DEST_IOMMU_OFFSET);
        val_print(ACS_PRINT_INFO, "\n       IOMMU PCIe DEST IOMMU OFFSET: 0x%x", iommu_offset);
        ret = check_governing_iommu (iommu_offset, &iommu_index);
        if (ret == 1) {
          val_print(ACS_PRINT_ERR, "\n       Governing IOMMU index: 0x%x", iommu_index);
          if ((iommu_index < iommu_num) && (map_index < IOMMU_MAP_MAX)) {
            iommu_map [map_index].iommu_index = iommu_index;
            iommu_map [map_index].pcierc_index = index;
            iommu_map [map_index].pcierc_seg = val_iommu_get_pcierc_platform_info(index, 0, IOMMU_INFO_PCIE_RC_SEG);
            map_index++;
          }
        }
      }
    }
  }

  // Check if there is any IOMMU that governs PCIe root complexes that are part of different PCIe segments
  val_print(ACS_PRINT_ERR, "\n       IOMMU - PCIe Root Complex map table:", 0);
  for (index = 0; index < IOMMU_MAP_MAX; index++) {
    if (iommu_map[index].iommu_index != 0xff) {
      val_print(ACS_PRINT_ERR, "\n         IOMMU index: 0x%x", iommu_map[index].iommu_index);
      val_print(ACS_PRINT_ERR, "\n         PCIe RC index: 0x%x", iommu_map[index].pcierc_index);
      val_print(ACS_PRINT_ERR, "\n         PCIe RC segment: 0x%x\n", iommu_map[index].pcierc_seg);
    }
  }

  for (index = 0; index < IOMMU_MAP_MAX - 1; index++) {
    for (index1 = index + 1; index1 < IOMMU_MAP_MAX; index1++) {
      if ((iommu_map[index].iommu_index != 0xFF) \
        && (iommu_map[index].iommu_index == iommu_map[index1].iommu_index) \
        && (iommu_map[index].pcierc_seg != iommu_map[index1].pcierc_seg)) {
          iommu_multi_list[iommu_index] = 1;
          val_print(ACS_PRINT_ERR, "\n       IOMMU Multi index: 0x%x", iommu_map[index].iommu_index);
      }
    }
  }

  ret = 0xff;
  for (index = 0; index < iommu_num; index++) {
    if (iommu_multi_list[index] == 1) {
      ret = check_iommu_ddtp_level(index);
      if (ret == 0) {
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
      }
    }
  }

  if (ret == 1) {
    val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  } else {
    val_print(ACS_PRINT_ERR, "\n       SKIP: NO MULTIPLE SEGMENT PCIE RP GOVERNING IOMMU NODE", 0);
    val_set_status(hart_index, RESULT_SKIP(TEST_NUM, 1));
  }

  return;
}

uint32_t
os_iom026_entry(uint32_t num_hart)
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
