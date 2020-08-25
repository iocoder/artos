#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable)
{
  /* initialize EFI library */
  InitializeLib(ImageHandle, SystemTable);

  /* print empty line*/
  Print(L"\n");

  /* print splash */
  Print(L"Hello world!\n");

  /* inf wait */
  while (1);

  /* program should never reach here */
  return EFI_SUCCESS;
}