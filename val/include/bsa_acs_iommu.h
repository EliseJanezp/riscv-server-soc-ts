#ifndef __BSA_ACS_IOMMU_H__
#define __BSA_ACS_IOMMU_H__

#include <stdbool.h>

#define EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU               0
#define EFI_ACPI_6_5_RIMT_DEVICE_TYPE_PCIE_ROOT_COMPLEX   1
#define EFI_ACPI_6_5_RIMT_DEVICE_TYPE_Platform_DEVICE     2

uint32_t
os_iom001_entry(uint32_t num_hart);

uint32_t
os_iom002_entry(uint32_t num_hart);

uint32_t
os_iom003_entry(uint32_t num_hart);

uint32_t
os_iom004_entry(uint32_t num_hart);

uint32_t
os_iom005_entry(uint32_t num_hart);

uint32_t
os_iom008_entry(uint32_t num_hart);

uint32_t
os_iom011_entry(uint32_t num_hart);

uint32_t
os_iom013_entry(uint32_t num_hart);

uint32_t
os_iom014_entry(uint32_t num_hart);

uint32_t
os_iom017_entry(uint32_t num_hart);

uint32_t
os_iom019_entry(uint32_t num_hart);

uint32_t
os_iom022_entry(uint32_t num_hart);
#endif
