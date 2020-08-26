#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <windows.h>

#define EXTERNAL_FILE(startsym, endsym, path) \
           __asm__(startsym ":\r\n"); \
           __asm__(".incbin \"" path "\"\r\n"); \
           __asm__(endsym ":\r\n");

EXTERNAL_FILE("qemu_start",  "qemu_end",  QEMU_FILE);
EXTERNAL_FILE("uefi_start",  "uefi_end",  EFI_FILE);
EXTERNAL_FILE("artos_start", "artos_end", ARTOS_FILE);

extern const char qemu_start, qemu_end;
extern const char uefi_start, uefi_end;
extern const char artos_start, artos_end;

char *boot_pattern = "BdsDxe: starting";

char *qemu_cmd = "qemu\\qemu-system-aarch64.exe "
                 "-M virt "
                 "-cpu cortex-a57 "
                 "-m 1G "
                 "-display none "
                 "-serial stdio "
                 "-bios firmware\\uefi_code.fd "
                 "-drive file=fat:rw:disk,if=virtio,format=raw "
                 "-icount shift=1,align=off,sleep=off "
                 "";

void start_program() {
  char TempDirPath[512];
  char CWDPath[512];

  setvbuf(stdout, NULL, _IONBF, 0);

  printf("\n");

  printf("Starting ARTOS emulator...\n");

  GetTempPathA(sizeof(TempDirPath)-1, TempDirPath);
  _chdir(TempDirPath);

  _mkdir("TempARTOS");
  _chdir("TempARTOS");

  _getcwd(CWDPath, sizeof(CWDPath));
  printf("Tempdir: %s\n", CWDPath);

  printf("\n");
}

void close_program(int code) {
  HMODULE hntdll;
  hntdll = GetModuleHandle("ntdll.dll");
  if (hntdll) {
    if (GetProcAddress(hntdll, "wine_get_version") == NULL) {
      printf("\n");
      system("pause");
    }
  }
  exit(code);
}

void extract_artos() {
  FILE *fd;
  const char *ptr_start;
  const char *ptr_end;

  ptr_start = &artos_start;
  ptr_end   = &artos_end;

  printf("Extracting ARTOS disk image...                          ");

  _mkdir("disk");
  _mkdir("disk\\EFI");
  _mkdir("disk\\EFI\\boot");

  fd = fopen("disk\\EFI\\boot\\bootaa64.efi", "wb");
  fwrite(ptr_start, ptr_end-ptr_start, 1, fd);
  fclose(fd);

  printf("[DONE]\n");
}

void extract_uefi() {
  FILE *fd;
  const char *ptr_start;
  const char *ptr_end;

  ptr_start = &uefi_start;
  ptr_end   = &uefi_end;

  printf("Extracting the firmware image...                        ");

  _mkdir("firmware");

  fd = fopen("firmware\\uefi_code.fd", "wb");
  fwrite(ptr_start, ptr_end-ptr_start, 1, fd);
  fclose(fd);

  printf("[DONE]\n");
}

void extract_qemu() {
  const char *buff;
  const char *p;
  int n;
  FILE *f = NULL;
  char *fname;
  size_t bytes_read;
  int filesize;
  BOOL arch_end;

  printf("Extracting QEMU...                                      ");

  buff = &qemu_start;
  arch_end = FALSE;

  while (arch_end == FALSE) {
    arch_end = TRUE;

    for (n = 511; n >= 0; --n) {
      if (buff[n] != '\0') {
        arch_end = FALSE;
      }
    }

    if (arch_end == FALSE) {
      filesize = 0;

      p = buff + 124;
      n = 12;

      while ((*p < '0' || *p > '7') && n > 0) {
        ++p;
        --n;
      }

      while (*p >= '0' && *p <= '7' && n > 0) {
        filesize *= 8;
        filesize += *p - '0';
        ++p;
        --n;
      }

      switch (buff[156]) {
        case '1':
          break;

        case '2':
          break;

        case '3':
          break;

        case '4':
          break;

        case '5':
          _mkdir(buff);
          filesize = 0;
          break;

        case '6':
          break;

        default:
          f = fopen(buff, "wb+");
          if (f == NULL) {
            fname = strrchr(buff, '/');
            if (fname != NULL) {
              *fname = '\0';
              _mkdir(buff);
              *fname = '/';
              f = fopen(buff, "wb+");
            }
          }
          break;
      }

      while (filesize > 0) {
        buff += 512;

        if (filesize < 512) {
          bytes_read = filesize;
        } else {
          bytes_read = 512;
        }

        if (f != NULL) {
          if (fwrite(buff, 1, bytes_read, f) != bytes_read) {
            fprintf(stderr, "Failed write\n");
            fclose(f);
            f = NULL;
          }
        }

        filesize -= bytes_read;
      }

      if (f != NULL) {
        fclose(f);
        f = NULL;
      }

      buff += 512;
    }
  }

  printf("[DONE]\n");
}

void qemu_callback(PVOID lpParameter, BOOLEAN TimerOrWaitFired) {
  HANDLE hProcess;
  DWORD  ExitCode;
  hProcess = *((PHANDLE) lpParameter);
  GetExitCodeProcess(hProcess, &ExitCode);
  close_program(ExitCode);
}

void exec_qemu() {
  SECURITY_ATTRIBUTES saAttr = {0};
  PROCESS_INFORMATION processInfo = {0};
  STARTUPINFO info = {0};
  CHAR chBuf[100], chCmd[100];
  HANDLE hPipeRd, hPipeWr;
  HANDLE hWaitObject;
  DWORD dwRead;
  BOOL bSuccess = FALSE;
  BOOL bDone    = FALSE;
  BOOL bEscaped = FALSE;
  BOOL bBooted  = TRUE;
  int i, j, k;

  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  if (!CreatePipe(&hPipeRd, &hPipeWr, &saAttr, 0)) {
    printf("\nERROR: Unable to create named pipe.\n");
    close_program(1);
  }

  info.cb = sizeof(STARTUPINFO);
  info.dwFlags = STARTF_USESTDHANDLES;
  info.hStdOutput = hPipeWr;

  if (!CreateProcess("qemu\\qemu-system-aarch64.exe",
                     qemu_cmd,
                     NULL,
                     NULL,
                     TRUE,
                     0,
                     NULL,
                     NULL,
                     &info,
                     &processInfo)) {
    printf("\nERROR: Unable to execute QEMU.\n");
    close_program(1);
  }

  RegisterWaitForSingleObject(&hWaitObject,
                              processInfo.hProcess,
                              qemu_callback,
                              &processInfo.hProcess,
                              INFINITE,
                              WT_EXECUTEONLYONCE);

  printf("Booting up UEFI...                                      ");

  bBooted  = FALSE;
  bEscaped = FALSE;
  bDone    = FALSE;

  i = 0;
  j = 0;
  k = 0;

  while (bDone == FALSE) {
    bSuccess = ReadFile(hPipeRd, chBuf, sizeof(chBuf), &dwRead, NULL);
    if (!bSuccess || dwRead == 0) {
      bDone = TRUE;
    } else {
      for (i = 0; i < dwRead; i++) {
        if (bEscaped == TRUE) {
          chCmd[j++] = chBuf[i];
          if (chBuf[i] >= 'A' && chBuf[i] != ';') {
            chCmd[j] = 0;
            if (bBooted == TRUE) {
              printf("TTY CMD: %s\n", chCmd);
            }
            bEscaped = FALSE;
          }
        } else {
          if (chBuf[i] == 0x1B) {
            bEscaped = TRUE;
            j = 0;
          } else {
            if (bBooted == TRUE) {
              printf("%c", chBuf[i]);
            }
          }
        }
        if (bBooted == FALSE) {
          if (boot_pattern[k] == '\0') {
            if (chBuf[i] == '\n') {
              printf("[DONE]\n");
              bBooted = TRUE;
            }
          } else {
            if (chBuf[i] == boot_pattern[k]) {
              k++;
            } else {
              k = 0;
            }
          }
        }
      }
    }
  }
}

int main() {
  start_program();

  extract_artos();
  extract_uefi();
  extract_qemu();

  exec_qemu();

  close_program(0);

  return 0;
}