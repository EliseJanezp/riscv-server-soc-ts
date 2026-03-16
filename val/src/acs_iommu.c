
#include "include/bsa_acs_val.h"
#include "include/bsa_acs_iommu.h"
#include "include/bsa_acs_common.h"

IOMMU_INFO_TABLE  *g_iommu_info_table;

void val_memory_map_add_mmio (uint64_t  Address, uint64_t  Length);
uint32_t val_pcie_bar_mem_read(uint32_t bdf, uint64_t address, uint32_t *data);
uint32_t val_pcie_bar_mem_write(uint32_t bdf, uint64_t address, uint32_t data);

/**
  @brief   This API executes all the IOMMU tests sequentially
           1. Caller       -  Application layer.
           2. Prerequisite -  val_iommu_create_info_table()
  @param   num_hart - the number of HART to run these tests on.
  @param   g_sw_view - Keeps the information about which view tests to be run
  @return  Consolidated status of all the tests run.
**/
uint32_t
val_iommu_execute_tests(uint32_t num_hart, uint32_t *g_sw_view)
{
  uint32_t status, i;

  for (i = 0; i < g_num_skip; i++) {
      if (g_skip_test_num[i] == ACS_IOMMU_TEST_NUM_BASE) {
          val_print(ACS_PRINT_INFO, "\n       USER Override - Skipping all IOMMU tests\n", 0);
          return ACS_STATUS_SKIP;
      }
  }

  /* Check if there are any tests to be executed in current module with user override options*/
  status = val_check_skip_module(ACS_IOMMU_TEST_NUM_BASE);
  if (status) {
      val_print(ACS_PRINT_INFO, "\n       USER Override - Skipping all IOMMU tests\n", 0);
      return ACS_STATUS_SKIP;
  }

  val_print_test_start("IOMMU");
  status      = ACS_STATUS_PASS;
  g_curr_module = 1 << IOMMU_MODULE;

  if (g_sw_view[G_SW_OS]) {
      val_print(ACS_PRINT_ERR, "\nOperating System View:\n", 0);
      status |= os_iom001_entry(num_hart);
      status |= os_iom002_entry(num_hart);
  }
  val_print_test_end(status, "IOMMU");

  return status;
}

/**
  @brief   This API will call PAL layer to fill in the IOMMU information
           into the g_iommu_info_table pointer.
           1. Caller       -  Application layer.
           2. Prerequisite -  Memory allocated and passed as argument.
  @param   iommu_info_table  pre-allocated memory pointer for iommu_info
  @return  Error if Input param is NULL
**/
uint32_t
val_iommu_create_info_table(uint64_t *iommu_info_table)
{
  if (iommu_info_table == NULL) {
      val_print(ACS_PRINT_ERR, "Input for Create Info table cannot be NULL\n", 0);
      return ACS_STATUS_ERR;
  }
  val_print(ACS_PRINT_INFO, " Creating IOMMU INFO table\n", 0);

  g_iommu_info_table = (IOMMU_INFO_TABLE *)iommu_info_table;

  pal_iommu_create_info_table(g_iommu_info_table);
  val_print(ACS_PRINT_INFO, " RV porting: IOMMU info print to be added\n", 0);

  return ACS_STATUS_PASS;
}

/**
  @brief   This API frees the memory assigned for iommu info table
           1. Caller       -  Application Layer
           2. Prerequisite -  val_iommu_create_info_table
  @param   None
  @return  None
**/
void
val_iommu_free_info_table(void)
{
  pal_mem_free((void *)g_iommu_info_table);
}

/**
  @brief   This API returns the number of IOMMU from the g_iommu_info_table.
           1. Caller       -  Application layer, test Suite.
           2. Prerequisite -  val_hart_create_info_table.
  @param   none
  @return  the number of iommu node discovered
**/
uint32_t
val_iommu_get_num()
{
  if (g_iommu_info_table == NULL) {
      return 0;
  }
  return g_iommu_info_table->header.num_of_iommu;
}

/**
  @brief   This API returns the number of IOMMU from the g_iommu_info_table.
           1. Caller       -  Application layer, test Suite.
           2. Prerequisite -  val_hart_create_info_table.
  @param   none
  @return  the number of idmap table discovered
**/
uint16_t
val_iommu_get_idmap_num(int32_t index)
{
  if (g_iommu_info_table == NULL) {
      return 0;
  }
  return g_iommu_info_table->iommu_info[index].idmap_num;
}

/**
  @brief   This API is a single point of entry to retrieve
           information stored in the IOMMU Info table
           1. Caller       -  Test Suite
           2. Prerequisite -  val_hart_create_info_table
  @param   index  IOMMU index in the table
  @param   type   the type of information being requested
  @return  64-bit data
**/
uint64_t
val_iommu_get_info(int32_t index, IOMMU_INFO_e info_type)
{

  if (g_iommu_info_table == NULL)
      return 0;

  switch (info_type) {
    case IOMMU_INFO_TYPE:
        return g_iommu_info_table->iommu_info[index].type;
    case IOMMU_INFO_BASE_ADDRESS:
        return g_iommu_info_table->iommu_info[index].base_address;
    case IOMMU_INFO_FLAGS:
        return g_iommu_info_table->iommu_info[index].flags;
    case IOMMU_INFO_PCIE_SEG:
        return g_iommu_info_table->iommu_info[index].pcie_seg;
    case IOMMU_INFO_PCIE_BDF:
        return g_iommu_info_table->iommu_info[index].pcie_bdf;
    case IOMMU_INFO_IOMMU_OFFSET:
        return g_iommu_info_table->iommu_info[index].iommu_offset;
    default:
      return 0;
  }
}

/**
  @brief   This API is a single point of entry to retrieve
           information stored in the IOMMU Info idmap table
           1. Caller       -  Test Suite
           2. Prerequisite -  val_hart_create_info_table
  @param   index  IOMMU index in the table
  @param   idmap_index  IDMAP index in the table
  @param   type   the type of information being requested
  @return  64-bit data
**/
uint64_t
val_iommu_get_pcierc_platform_info(int32_t index, int32_t idmap_index, PCIERC_PLATFORM_INFO_e info_type)
{
  if (g_iommu_info_table == NULL)
      return 0;

  switch (info_type) {
    case IOMMU_INFO_PCIE_RC_SEG:
        return g_iommu_info_table->iommu_info[index].pcie_rc_seg;
    case IOMMU_INFO_SOURCE_ID_BASE:
        return g_iommu_info_table->iommu_info[index].id_mapping_table[idmap_index].sourceid_base;
    case IOMMU_INFO_NUM_IDS:
        return g_iommu_info_table->iommu_info[index].id_mapping_table[idmap_index].numids;
    case IOMMU_INFO_DEST_ID_BASE:
        return g_iommu_info_table->iommu_info[index].id_mapping_table[idmap_index].destid_base;
    case IOMMU_INFO_DEST_IOMMU_OFFSET:
        return g_iommu_info_table->iommu_info[index].id_mapping_table[idmap_index].destiommu_offset;
    default:
      return 0;
  }
}

/**
  @brief   This API is a single point of entry to get iommu reg value
           1. Caller       -  Test Suite
           2. Prerequisite -  val_hart_create_info_table
  @param   index  IOMMU index in the table
  @param   offset  offset of the register to be read
  @param   num  number of dword to be read
  @return  64-bit data
**/
uint64_t
val_iommu_read_iommu_reg(uint32_t index, uint32_t offset, uint32_t num)
{
  uint32_t flags = 0;
  uint64_t base_addr = 0;
  uint16_t pcie_seg = 0;
  uint16_t pcie_bdf = 0;
  uint32_t bdf = 0;
  uint64_t reg_value = 0;
  uint64_t datahigh, datalow;

  if (g_iommu_info_table == NULL)
      return 0;
  if ((offset > 1024) || (num > 2))
      return 0;

  if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      flags = val_iommu_get_info (index, IOMMU_INFO_FLAGS);
      // val_print(ACS_PRINT_INFO, "\n       IOMMU FLAGS: 0x%x", flags);
      if ((flags & 0x01) == 0) { // IOMMU is implemented as a platform device
        base_addr = val_iommu_get_info (index, IOMMU_INFO_BASE_ADDRESS);
        /* Map the IOMMU memory-mapped register region */
        val_memory_map_add_mmio(base_addr, 0x1000);
        if (num == 1) {
          reg_value = val_mmio_read(base_addr + offset);
        } else if (num == 2) {
          reg_value = val_mmio_read64(base_addr + offset);
        }
      } else if ((flags & 0x01) == 1) { // IOMMU is implemented as a PCIe device
        pcie_seg = val_iommu_get_info (index, IOMMU_INFO_PCIE_SEG);
        pcie_bdf = val_iommu_get_info (index, IOMMU_INFO_PCIE_BDF);
        // val_print(ACS_PRINT_INFO, "\n       IOMMU PCIE_RID: 0x%x", pcie_bdf);
        // pcie_bdf(PRID format bus << 8 dev << 3 func) transfer to pcie format(seg << 24 bus << 16 dev << 8 func)
        bdf = ((pcie_seg << 24) | (pcie_bdf & 0xff00) << 8) | ((pcie_bdf & 0x00f8) << 5) | ((pcie_bdf & 0x0007));
        // val_print(ACS_PRINT_INFO, "\n       IOMMU pcie_bdf: 0x%x", bdf);
        val_pcie_get_mmio_bar (bdf, &base_addr);
        if (num == 1) {
          val_pcie_bar_mem_read (bdf, base_addr + offset, (uint32_t *)&reg_value);
        } else if (num == 2) {
          val_pcie_bar_mem_read (bdf, base_addr + offset, (uint32_t *)&datalow);
          val_pcie_bar_mem_read (bdf, base_addr + offset + 4, (uint32_t *)&datahigh);
          reg_value = ((datahigh & 0xffffffff) << 32) | (datalow & 0xffffffff);
        }
      }
  }
  // val_print(ACS_PRINT_INFO, "\n       IOMMU base: 0x%lx", base_addr);
  // val_print(ACS_PRINT_INFO, "\n       IOMMU REG: 0x%x", reg_value);

  return reg_value;
}

/**
  @brief   This API is a single point of entry to get iommu reg value
           1. Caller       -  Test Suite
           2. Prerequisite -  val_hart_create_info_table
  @param   index  IOMMU index in the table
  @param   offset  offset of the register to be read
  @param   num  number of dword to be read
  @return  64-bit data
**/
void
val_iommu_write_iommu_reg(uint32_t index, uint32_t offset, uint32_t num, uint64_t data)
{
  uint32_t flags = 0;
  uint64_t base_addr = 0;
  uint16_t pcie_seg = 0;
  uint16_t pcie_bdf = 0;
  uint32_t bdf = 0;

  if (g_iommu_info_table == NULL)
      return;
  if ((offset > 1024) || (num > 2))
      return;

  if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      flags = val_iommu_get_info (index, IOMMU_INFO_FLAGS);
      // val_print(ACS_PRINT_INFO, "\n       IOMMU FLAGS: 0x%x", flags);
      if ((flags & 0x01) == 0) { // IOMMU is implemented as a platform device
        base_addr = val_iommu_get_info (index, IOMMU_INFO_BASE_ADDRESS);
        /* Map the IOMMU memory-mapped register region */
        val_memory_map_add_mmio(base_addr, 0x1000);
        if (num == 1) {
          val_mmio_write(base_addr + offset, (uint32_t)data);
        } else if (num == 2) {
          val_mmio_write64(base_addr + offset, (uint64_t)data);
        }
      } else if ((flags & 0x01) == 1) { // IOMMU is implemented as a PCIe device
        pcie_seg = val_iommu_get_info (index, IOMMU_INFO_PCIE_SEG);
        pcie_bdf = val_iommu_get_info (index, IOMMU_INFO_PCIE_BDF);
        // val_print(ACS_PRINT_INFO, "\n       IOMMU PCIE_RID: 0x%x", pcie_bdf);
        // pcie_bdf(PRID format bus << 8 dev << 3 func) transfer to pcie format(seg << 24 bus << 16 dev << 8 func)
        bdf = ((pcie_seg << 24) | (pcie_bdf & 0xff00) << 8) | ((pcie_bdf & 0x00f8) << 5) | ((pcie_bdf & 0x0007));
        // val_print(ACS_PRINT_INFO, "\n       IOMMU pcie_bdf: 0x%x", bdf);
        val_pcie_get_mmio_bar (bdf, &base_addr);
        if (num == 1) {
          val_pcie_bar_mem_write (bdf, base_addr + offset, (uint32_t)data);
        } else if (num == 2) {
          val_pcie_bar_mem_write (bdf, base_addr + offset, (uint32_t)data);
          val_pcie_bar_mem_write (bdf, base_addr + offset + 4, (uint32_t )((data & 0xffffffff00000000) >> 32));
        }
      }
  }
  // val_print(ACS_PRINT_INFO, "\n       IOMMU base: 0x%lx", base_addr);
  // val_print(ACS_PRINT_INFO, "\n       IOMMU REG value: 0x%x", data);

  return;
}

/**
  @brief   This API is to check governing iommu offset
           1. Caller       -  Test Suite
           2. Prerequisite -  val_hart_create_info_table
  @param   destiommu_offset  IOMMU offset
  @return  8-bit data
**/
static
uint8_t
val_iommu_check_governing_iommu_offset(uint32_t destiommu_offset, uint32_t **iommu_index)
{
  uint32_t iommu_num = val_iommu_get_num();
  uint32_t iommu_offset;
  uint32_t index;

  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      iommu_offset = val_iommu_get_info (index, IOMMU_INFO_IOMMU_OFFSET);
      if (destiommu_offset == iommu_offset) {
        **iommu_index = index;
        return 1;
      }
    }
  }
  return 0;
}

/**
  @brief   This API is to check governing iommu node
           1. Caller       -  Test Suite
           2. Prerequisite -  val_hart_create_info_table
  @param   bdf  PCIe BDF
  @param   iommu_index  Pointer of IOMMU index
  @return  8-bit data
**/
uint8_t
val_iommu_check_governing_iommu(uint32_t bdf, uint32_t *iommu_index)
{
  uint32_t index;
  uint16_t pcie_rc_seg;
  uint32_t sourceid_base;
  uint32_t numids;
  uint32_t destiommu_offset;
  uint32_t iommu_num = val_iommu_get_num();
  uint16_t prid;
  uint16_t segment;
  uint32_t idmap_index;
  uint16_t idmap_num;

  if (g_iommu_info_table == NULL)
    return 0;

  if (iommu_num == 0)
    return 0;

  // pcie format(seg << 24 bus << 16 dev << 8 func) transfer to prid(PRID format bus << 8 dev << 3 func)
  segment = (bdf & 0xff000000) >> 24;
  prid = ((bdf & 0x00ff0000) >> 8) | ((bdf & 0x0000ff00) >> 5) | (bdf & 0x000000ff);
  val_print(ACS_PRINT_INFO, "\n       IOMMU PCIe PRID: 0x%x", prid);
  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_PCIE_ROOT_COMPLEX) {
      pcie_rc_seg = val_iommu_get_pcierc_platform_info (index, 0, IOMMU_INFO_PCIE_RC_SEG);
      idmap_num = val_iommu_get_idmap_num (index);
      if ((pcie_rc_seg != segment) || (idmap_num == 0)) {
        continue;
      } else {
        for (idmap_index = 0; idmap_index < idmap_num; idmap_index++) {
          sourceid_base = val_iommu_get_pcierc_platform_info (index, idmap_index, IOMMU_INFO_SOURCE_ID_BASE);
          numids = val_iommu_get_pcierc_platform_info (index, idmap_index, IOMMU_INFO_NUM_IDS);
          val_print(ACS_PRINT_INFO, "\n       IOMMU PCIe SOURCE ID BASE: 0x%x", sourceid_base);
          val_print(ACS_PRINT_INFO, "\n       IOMMU PCIe NUM IDS: 0x%x", numids);
          if (prid <= (sourceid_base + numids)) {
            destiommu_offset = val_iommu_get_pcierc_platform_info (index, idmap_index, IOMMU_INFO_DEST_IOMMU_OFFSET);
            val_print(ACS_PRINT_INFO, "\n       IOMMU PCIe DEST IOMMU OFFSET: 0x%x", destiommu_offset);
            return val_iommu_check_governing_iommu_offset(destiommu_offset, &iommu_index);
          } else {
            continue;
          }
        }
      }
    }
  }
  return 0;
}