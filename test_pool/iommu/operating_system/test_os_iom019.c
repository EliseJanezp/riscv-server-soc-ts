#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"
#include "val/include/bsa_acs_pcie.h"

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 19)
#define TEST_RULE  "ME_IOM_190_010"
#define TEST_DESC  "Check IOMMU HPM and cycles counter support               "

#define RISCV_IOMMU_REG_CAP          0x0
#define RISCV_IOMMU_CAP_HPM_MSK      (1ULL << 30)
#define RISCV_IOMMU_CAP_HPM_BIT      30UL

#define RISCV_IOMMU_REG_IOCOUNTOVF               88
#define RISCV_IOMMU_REG_IOCOUNTINH               92
#define RISCV_IOMMU_REG_IOCOUNT_CY_MSK           (1)
#define RISCV_IOMMU_REG_IOCOUNT_HPM_MSK          0xFFFFFFFEULL

#define RISCV_IOMMU_REG_IOHPMCYCLES               96
#define RISCV_IOMMU_REG_IOHPMCYCLES_OF_MSK        (1ULL << 63)
#define RISCV_IOMMU_REG_COUNTER_MSK               0xFFFFFFFFFFULL

#define RISCV_IOMMU_REG_IOHPMCTRL1   104
#define RISCV_IOMMU_REG_IOHPMCTRL2   112
#define RISCV_IOMMU_REG_IOHPMCTRL3   120
#define RISCV_IOMMU_REG_IOHPMCTRL4   128

/**
 * @brief For each IOMMU:
 * 1. if capabilities.HPM is 0 then continue.
 * 2. Verify iohpmcycles and its OF bit are writeable and the cycles counter is at least
 *    40-bit wide.
 * 3. Verify at least four programmable HPM counters are supported and the counters
 *    for each are at least 40-bit wide.
 * 4. Verify that the bits corresponding to the implemented HPM counters in
 *    iocountovf and iocountinh are writeable.
 * 5. Verify that the iohpmcycles is at least 40-bit wide.
 * 6. Verify that the CY bit in iocountovf and iocountinh is writeable.
 */
static
void
payload()
{
  uint32_t index;
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  uint32_t iommu_num = val_iommu_get_num();
  uint64_t reg_cap, reg_iohpmcycles_old, reg_iohpmcycles, reg_iohpmcycles_new;
  uint64_t reg_iohpmctrl_old, reg_iohpmctrl, reg_iohpmctrl_new;
  uint64_t offset;
  uint32_t reg_iocountovf_old, reg_iocountovf, reg_iocountovf_new;
  uint32_t reg_iocountinh_old, reg_iocountinh, reg_iocountinh_new;

  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info (index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      // check if HPM is supported
      reg_cap = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_CAP, 2);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap - 0x%lx", reg_cap);
      if (((reg_cap & RISCV_IOMMU_CAP_HPM_MSK) >> RISCV_IOMMU_CAP_HPM_BIT) == 0) {
        continue;
      }

      // check if iohpmcycles and its OF bit are writeable and the cycles counter is at least 40-bit wide
      reg_iohpmcycles_old = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_IOHPMCYCLES, 2);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iohpmcycles old - 0x%lx", reg_iohpmcycles_old);
      if ((reg_iohpmcycles_old & RISCV_IOMMU_REG_IOHPMCYCLES_OF_MSK) == 0) {
        reg_iohpmcycles = reg_iohpmcycles_old | RISCV_IOMMU_REG_IOHPMCYCLES_OF_MSK;
      } else {
        reg_iohpmcycles = reg_iohpmcycles_old & (~RISCV_IOMMU_REG_IOHPMCYCLES_OF_MSK);
      }
      reg_iohpmcycles = reg_iohpmcycles_old & (~RISCV_IOMMU_REG_COUNTER_MSK);
      reg_iohpmcycles = reg_iohpmcycles_old | RISCV_IOMMU_REG_COUNTER_MSK;
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iohpmcycles - 0x%lx", reg_iohpmcycles);
      val_iommu_write_iommu_reg(index, RISCV_IOMMU_REG_IOHPMCYCLES, 2, reg_iohpmcycles);
      reg_iohpmcycles_new = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_IOHPMCYCLES, 2);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iohpmcycles_new - 0x%lx", reg_iohpmcycles_new);
      if (reg_iohpmcycles_new < reg_iohpmcycles) {      // counter is still runing
        val_print(ACS_PRINT_ERR, "\n       IOMMU reg_iohpmcycles is not writeable or counter are not support 40-bits width", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }
      // write back the old value
      val_iommu_write_iommu_reg(index, RISCV_IOMMU_REG_IOHPMCYCLES, 2, reg_iohpmcycles_old);

      // check if at least four programmable HPM counters are supported and the counters for each are at least 40-bit wide
      for (uint8_t count = 0; count < 4; count++) {
        offset = RISCV_IOMMU_REG_IOHPMCTRL1 + 8 * count;
        reg_iohpmctrl_old = val_iommu_read_iommu_reg(index, offset, 2);
        val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iohpmctrl old- 0x%lx", reg_iohpmctrl_old);
        reg_iohpmctrl = reg_iohpmctrl_old & (~RISCV_IOMMU_REG_COUNTER_MSK);
        val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iohpmctrl - 0x%lx", reg_iohpmctrl);
        val_iommu_write_iommu_reg(index, offset, 2, reg_iohpmctrl);
        reg_iohpmctrl_new = val_iommu_read_iommu_reg(index, offset, 2);
        val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iohpmctrl_new - 0x%lx", reg_iohpmctrl_new);
        if (reg_iohpmctrl_new != reg_iohpmctrl) {
          val_print(ACS_PRINT_ERR, "\n       IOMMU is not support 4 HPM or counter are not support 40-bits wide", 0);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
          return;
        }
        reg_iohpmctrl = reg_iohpmctrl_old | RISCV_IOMMU_REG_COUNTER_MSK;
        val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iohpmctrl 1- 0x%lx", reg_iohpmctrl);
        val_iommu_write_iommu_reg(index, offset, 2, reg_iohpmctrl);
        reg_iohpmctrl_new = val_iommu_read_iommu_reg(index, offset, 2);
        val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iohpmctrl_new 1 - 0x%lx", reg_iohpmctrl_new);
        if (reg_iohpmctrl_new != reg_iohpmctrl) {
          val_print(ACS_PRINT_ERR, "\n       IOMMU is not support 4 HPM or counter are not support 40-bits wide", 0);
          val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
          return;
        }
        // restore the old value
        val_iommu_write_iommu_reg(index, offset, 2, reg_iohpmcycles_old);
      }

      // check if the iocountovf HPM bits and CY bit are writeable
      reg_iocountovf_old = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_IOCOUNTOVF, 1);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iocountovf_old - 0x%lx", reg_iocountovf_old);
      if ((reg_iocountovf_old & RISCV_IOMMU_REG_IOCOUNT_CY_MSK) == 0) {
        reg_iocountovf = reg_iocountovf_old | RISCV_IOMMU_REG_IOCOUNT_CY_MSK;
      } else {
        reg_iocountovf = reg_iocountovf_old & (~RISCV_IOMMU_REG_IOCOUNT_CY_MSK);
      }
      reg_iocountovf = reg_iocountovf_old & (~RISCV_IOMMU_REG_IOCOUNT_HPM_MSK);
      reg_iocountovf = reg_iocountovf_old | RISCV_IOMMU_REG_IOCOUNT_HPM_MSK;
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iocountovf - 0x%lx", reg_iocountovf);
      val_iommu_write_iommu_reg(index, RISCV_IOMMU_REG_IOCOUNTOVF, 1, reg_iocountovf);
      reg_iocountovf_new = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_IOCOUNTOVF, 1);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iocountovf_new - 0x%lx", reg_iocountovf_new);
      if (reg_iocountovf_new != reg_iocountovf) {
        val_print(ACS_PRINT_ERR, "\n       IOMMU iocountovf register is not writeable", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }
      val_iommu_write_iommu_reg(index, RISCV_IOMMU_REG_IOCOUNTOVF, 1, reg_iocountovf_old);

      // check if the iocountinh HPM bits and CY bit are writeable
      reg_iocountinh_old = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_IOCOUNTINH, 1);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iocountinh_old - 0x%lx", reg_iocountinh_old);
      if ((reg_iocountinh_old & RISCV_IOMMU_REG_IOCOUNT_CY_MSK) == 0) {
        reg_iocountinh = reg_iocountinh_old | RISCV_IOMMU_REG_IOCOUNT_CY_MSK;
      } else {
        reg_iocountinh = reg_iocountinh_old & (~RISCV_IOMMU_REG_IOCOUNT_CY_MSK);
      }
      reg_iocountinh = reg_iocountinh_old & (~RISCV_IOMMU_REG_IOCOUNT_HPM_MSK);
      reg_iocountinh = reg_iocountinh_old | RISCV_IOMMU_REG_IOCOUNT_HPM_MSK;
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iocountovf - 0x%lx", reg_iocountinh);
      val_iommu_write_iommu_reg(index, RISCV_IOMMU_REG_IOCOUNTINH, 1, reg_iocountinh);
      reg_iocountinh_new = val_iommu_read_iommu_reg(index, RISCV_IOMMU_REG_IOCOUNTINH, 1);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_iocountovf_new - 0x%lx", reg_iocountinh_new);
      if (reg_iocountinh_new != reg_iocountinh) {
        val_print(ACS_PRINT_ERR, "\n       IOMMU iocountovf register is not writeable", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }
      val_iommu_write_iommu_reg(index, RISCV_IOMMU_REG_IOCOUNTINH, 1, reg_iocountinh_old);
    }
  }

  val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
  return;
}

uint32_t
os_iom019_entry(uint32_t num_hart)
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
