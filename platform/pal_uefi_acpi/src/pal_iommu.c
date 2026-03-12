#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "Include/IndustryStandard/Acpi65.h"
#include <Protocol/AcpiTable.h>

#include "include/pal_uefi.h"

static EFI_ACPI_6_5_RISC_V_IO_MAPPING_TABLE_STRUCTURE *gRimtHdr;

UINT64
pal_get_rimt_ptr();

/**
  @brief  Populate information about the IOMMU sub-system at the input address.
          In a UEFI-ACPI framework, this information is part of the RIMT table.

  @param  GicTable  Address of the memory region where this information is to be filled in

  @return None
**/
VOID
pal_iommu_create_info_table(IOMMU_INFO_TABLE *IommuTable)
{
  UINT32                            TableLength;
  EFI_ACPI_6_5_RIMT_DEVICE_HEADER   *Entry;
  EFI_ACPI_6_5_RIMT_IOMMU_DEVICE_STRUCTURE  *IommuEntry;
  EFI_ACPI_6_5_RIMT_PCIE_ROOT_COMPLEX_DEVICE_BINDING_STRUCTURE  *PCIeRCEntry;
  EFI_ACPI_6_5_RIMT_PLATFORM_DEVICE_BINDING_STRUCTURE *PlatformEntry;
  EFI_ACPI_6_5_RIMT_ID_MAPPING_STRUCTURE *IdMapEntry;
  IOMMU_INFO_ENTRY                  *Ptr = NULL;
  ID_MAPPTING_TABLE                 *IdMapPtr = NULL;

  UINT32                            Length = 0;
  UINT32                            NodeLength = 0;

  bsa_print(ACS_PRINT_INFO, L" Creating IOMMU INFO\n");

  if (IommuTable == NULL) {
    bsa_print(ACS_PRINT_ERR, L" Input IOMMU Table Pointer is NULL. Cannot create IOMMU INFO\n");
    return;
  }

  gRimtHdr = (EFI_ACPI_6_5_RISC_V_IO_MAPPING_TABLE_STRUCTURE *) pal_get_rimt_ptr();

  if (gRimtHdr != NULL) {
    TableLength =  gRimtHdr->Header.Length;
    bsa_print(ACS_PRINT_INFO, L"  RIMT table is at %x and length is 0x%x\n", gRimtHdr, TableLength);
  } else {
    bsa_print(ACS_PRINT_ERR, L" RIMT not found\n");
    return;
  }

  IommuTable->header.num_of_iommu = 0;
  Entry = (EFI_ACPI_6_5_RIMT_DEVICE_HEADER *) ((UINT8 *)gRimtHdr + gRimtHdr->RimtDeviceOffset);
  Length = gRimtHdr->RimtDeviceOffset;
  Ptr = IommuTable->iommu_info;


  do {
    Ptr->iommu_num = IommuTable->header.num_of_iommu;
    Ptr->type = Entry->Type;
    Ptr->id = Entry->ID;
    bsa_print(ACS_PRINT_INFO, L"   RIMT device id %d, type %d\n", Ptr->id, Ptr->type);


    if (Entry->Type == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU) {
      IommuEntry = (EFI_ACPI_6_5_RIMT_IOMMU_DEVICE_STRUCTURE *) Entry;
      Ptr->iommu_offset = Length;
      Ptr->hardware_id  = IommuEntry->HardwareID;
      Ptr->base_address = IommuEntry->BaseAddress;
      Ptr->flags        = IommuEntry->Flags;
      Ptr->pcie_seg        = IommuEntry->PcieSegmentNumber;
      Ptr->pcie_bdf        = IommuEntry->PcieBDF;
      bsa_print(ACS_PRINT_INFO, L"    IOMMU base address 0x%lx\n", Ptr->base_address);
    } else if (Entry->Type == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_PCIE_ROOT_COMPLEX) {
      PCIeRCEntry = (EFI_ACPI_6_5_RIMT_PCIE_ROOT_COMPLEX_DEVICE_BINDING_STRUCTURE *) Entry;
      Ptr->pcie_rc_seg = PCIeRCEntry->PcieSegmentNumber;
      Ptr->idmap_offset = PCIeRCEntry->IdMappingOffset;
      // Ptr->idmap_num = PCIeRCEntry->IdMappingNumber;
      Ptr->idmap_num = 0;
      IdMapPtr = Ptr->id_mapping_table;
      NodeLength = Ptr->idmap_offset;
      IdMapEntry = (EFI_ACPI_6_5_RIMT_ID_MAPPING_STRUCTURE *)((UINT8 *)PCIeRCEntry + PCIeRCEntry->IdMappingOffset);
      do {
        IdMapPtr->sourceid_base = IdMapEntry->SourceIdBase;
        IdMapPtr->numids = IdMapEntry->IdNumber;
        IdMapPtr->destid_base = IdMapEntry->DestDeviceIdBase;
        IdMapPtr->destiommu_offset = IdMapEntry->DestIommuOffset;
        bsa_print(ACS_PRINT_INFO, L"    IOMMU sourceid base 0x%x\n", IdMapPtr->sourceid_base );
        bsa_print(ACS_PRINT_INFO, L"    IOMMU numids 0x%x\n", IdMapPtr->numids );
        bsa_print(ACS_PRINT_INFO, L"    IOMMU destid_base 0x%x\n", IdMapPtr->destid_base );
        bsa_print(ACS_PRINT_INFO, L"    IOMMU destiommu_offset 0x%x\n", IdMapPtr->destiommu_offset );
        NodeLength += sizeof(EFI_ACPI_6_5_RIMT_ID_MAPPING_STRUCTURE);
        IdMapPtr++;
        Ptr->idmap_num++;
      } while(NodeLength < Entry->Length);
    } else if (Entry->Type == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_Platform_DEVICE) {
      PlatformEntry = (EFI_ACPI_6_5_RIMT_PLATFORM_DEVICE_BINDING_STRUCTURE *) Entry;
      Ptr->idmap_offset = PlatformEntry->IdMappingOffset;
      // Ptr->idmap_num = PlatformEntry->IdMappingNumber;
      Ptr->idmap_num = 0;
      IdMapPtr = Ptr->id_mapping_table;
      NodeLength = Ptr->idmap_offset;
      IdMapEntry = (EFI_ACPI_6_5_RIMT_ID_MAPPING_STRUCTURE *)((UINT8 *)PlatformEntry + PlatformEntry->IdMappingOffset);
      do {
        IdMapPtr->sourceid_base = IdMapEntry->SourceIdBase;
        IdMapPtr->numids = IdMapEntry->IdNumber;
        IdMapPtr->destid_base = IdMapEntry->DestDeviceIdBase;
        IdMapPtr->destiommu_offset = IdMapEntry->DestIommuOffset;
        NodeLength += sizeof(EFI_ACPI_6_5_RIMT_ID_MAPPING_STRUCTURE);
        IdMapPtr++;
        Ptr->idmap_num++;
      } while(NodeLength < Entry->Length);
    }

    Length += Entry->Length;
    Entry = (EFI_ACPI_6_5_RIMT_DEVICE_HEADER *) ((UINT8 *)Entry + (Entry->Length));
    Ptr++;
    IommuTable->header.num_of_iommu++;
  } while (Length < TableLength);

  if (IommuTable->header.num_of_iommu != gRimtHdr->RimtDeviceNumber) {
    bsa_print(ACS_PRINT_ERR, L" RIMT device number mismatch!\n");
  }
}