/***************************************************************************
 *
 *                   ARTOS Operating System.
 *                 Copyright (C) 2020  ARMKit.
 *
 ***************************************************************************
 * @file   port/src/translation.c
 * @brief  ARTOS port module: address translation.
 ***************************************************************************
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 ****************************************************************************/

/*****************************************************************************
 *                              INCLUDES
 ****************************************************************************/

/* Port includes. */
#include "port/inc/interface.h"
#include "port/inc/internal.h"

/*****************************************************************************
 *                          FUNCTION PROTOTYPES
 ****************************************************************************/

/* FIXME: THIS SHOULD BE ABSTRACTED IN A BETTER WAY. */
void *KernelMemoryPageAllocate   (void);
void  KernelMemoryPageDeallocate (void *pageBaseAddr);
void  KernelPrintFmt             (char *fmt, ...);

/*****************************************************************************
 *                           ASSEMBLY MACROS
 ****************************************************************************/

#define TLBI(variant)     __asm__("TLBI " #variant)
#define DSB(variant)      __asm__("DSB " #variant)
#define ISB()             __asm__("ISB")
#define MSR(sys_reg, var) __asm__("MSR " #sys_reg " , %0"::"r"(var))
#define MRS(var, sys_reg) __asm__("MRS %0, " #sys_reg : "=r"(var));

/*****************************************************************************
 *                              TCR MACROS
 ****************************************************************************/

/* TCR.TSZ field specification. */
#define TSZ_16_BITS             16
#define TSZ_20 BITS             20

/* TCR.EPD field specification. */
#define EPD_WALK_ON_TLB_MISS    0
#define EPD_EXCP_ON_TLB_MISS    1

/* TCR.IRGN field specification. */
#define IRGN_NON_CASHEABLE      0
#define IRGN_WB_RA_WA           1
#define IRGN_WT_RA_NWA          2
#define IRGN_WB_RA_NWA          1

/* TCR.ORGN field specification. */
#define ORGN_NON_CASHEABLE      0
#define ORGN_WB_RA_WA           1
#define ORGN_WT_RA_NWA          2
#define ORGN_WB_RA_NWA          1

/* TCR.SH field specification. */
#define SH_NON_SHAREABLE        0
#define SH_RESERVED             1
#define SH_OUTER_SHAREABLE      2
#define SH_INNER_SHAREABLE      3

/* TCR.TG field specification. */
#define TG_4KB                  0
#define TG_64KB                 1
#define TG_16KB                 2

/* TCR.A field specification. */
#define A_TTBR0_DEFINES_ASID    0
#define A_TTBR1_DEFINES_ASID    1

/* TCR.IPS field specification. */
#define IPS_32_BITS             0
#define IPS_36_BITS             1
#define IPS_40_BITS             2
#define IPS_42_BITS             3
#define IPS_44_BITS             4
#define IPS_48_BITS             5
#define IPS_52_BITS             6

/* TCR.AS field specification. */
#define AS_ASID_SIZE_8_BITS     0
#define AS_ASID_SIZE_16_BITS    1

/* TCR.TBI field specification. */
#define TBI_T0P_BYTE_USED       0
#define TBI_T0P_BYTE_IGNORED    1

/*****************************************************************************
 *                            SCTLR MACROS
 ****************************************************************************/

/* SCTLR.MMU field specification. */
#define MMU_DISABLE             0
#define MMU_ENABLE              1

/*****************************************************************************
 *                            PAGING MACROS
 ****************************************************************************/

/* Maximum possible physical address (max. is 0x0000FFFFFFFFFFFFUL). */
#define LAST_PHYSICAL_ADDR   0x0000007FFFFFFFFFUL

/* Table size. */
#define ENTRY_COUNT          512

/* Levels of memory */
#define L0_SIZE              (1UL<<48)
#define L1_SIZE              (1UL<<39)
#define L2_SIZE              (1UL<<30)
#define L3_SIZE              (1UL<<21)

/* How many L1 tables needed for TTB0. */
#define TTB0_L1_COUNT        ((LAST_PHYSICAL_ADDR+L1_SIZE)/L1_SIZE)

/* Alignment of L0/L1 tables. */
#define TBL_ALIGN             __attribute__((aligned(PAGE_SIZE)))

/* Convert 64-bit VA into address bits. */
#define TO_BLK_ADDR(PTR)     ((((uint64_t)PTR)>>30)&((1UL<<18)-1))
#define TO_TBL_ADDR(PTR)     ((((uint64_t)PTR)>>12)&((1UL<<36)-1))
#define TO_PAG_ADDR(PTR)     ((((uint64_t)PTR)>>12)&((1UL<<36)-1))
#define TO_TTB_ADDR(PTR)     ((((uint64_t)PTR)>> 1)&((1UL<<47)-1))

/* Convert address bites into 64-bit VA. */
#define FROM_BLK_ADDR(ADDR)  ((void *) ((uint64_t) ADDR<<30))
#define FROM_TBL_ADDR(ADDR)  ((void *) ((uint64_t) ADDR<<12))
#define FROM_PAG_ADDR(ADDR)  ((void *) ((uint64_t) ADDR<<12))
#define FROM_TTB_ADDR(ADDR)  ((void *) ((uint64_t) ADDR<< 1))

/* .VALID field specification. */
#define IS_INVALID              0
#define IS_VALID                1

/* .TYPE field specification. */
#define TYPE_BLOCK              0
#define TYPE_TABLE              1
#define TYPE_PAGE               1

/* .NS field specification. */
#define NS_SECURE               0
#define NS_NON_SECURE           1

/* .AP field specification. */
#define AP_RW_NONE              0
#define AP_RW_RW                1
#define AP_RO_NONE              2
#define AP_RO_RO                3

/* .AF field specification. */
#define AF_NON_ACCESSABLE       0
#define AF_ACCESSABLE           1

/* .NG field specification. */
#define NG_GLOBAL               0
#define NG_NON_GLOBAL           1

/* .CONT field specification. */
#define CONT_DISABLE            0
#define CONT_ENABLE             1

/* .PXN field specification.*/
#define PXN_PERMIT_EXEC         0
#define PXN_NOT_PERMIT_EXEC     1

/*****************************************************************************
 *                              TYPEDEFS
 ****************************************************************************/

/* Invalid entry descriptor. */
typedef struct INVENTRY
{
  unsigned long VALID          :1;
  unsigned long IGNORED        :63;
} __attribute__((packed)) INVENTRY_t;

/* Block entry descriptor (1GB). */
typedef struct BLKENTRY
{
  unsigned long VALID          :1;
  unsigned long TYPE           :1;
  unsigned long ATTRIDX        :3;
  unsigned long NS             :1;
  unsigned long AP             :2;
  unsigned long SH             :2;
  unsigned long AF             :1;
  unsigned long NG             :1;
  unsigned long RESV0          :18;
  unsigned long ADDR           :18;
  unsigned long RESV1          :4;
  unsigned long CONT           :1;
  unsigned long PXN            :1;
  unsigned long XN             :1;
  unsigned long IGNORED        :9;
} __attribute__((packed)) BLKENTRY_t;

/* Table entry descriptor. */
typedef struct TBLENTRY
{
  unsigned long VALID          :1;
  unsigned long TYPE           :1;
  unsigned long IGNORED0       :10;
  unsigned long ADDR           :36;
  unsigned long RESV           :4;
  unsigned long IGNORED1       :7;
  unsigned long PXN            :1;
  unsigned long UXN            :1;
  unsigned long AP             :2;
  unsigned long NS             :1;
} __attribute__((packed)) TBLENTRY_t;

/* Page entry descriptor. */
typedef struct PAGENTRY
{
  unsigned long VALID          :1;
  unsigned long TYPE           :1;
  unsigned long ATTRIDX        :3;
  unsigned long NS             :1;
  unsigned long AP             :2;
  unsigned long SH             :2;
  unsigned long AF             :1;
  unsigned long NG             :1;
  unsigned long ADDR           :36;
  unsigned long RESV0          :4;
  unsigned long CONT           :1;
  unsigned long PXN            :1;
  unsigned long UXN            :1;
  unsigned long IGNORED        :9;
} __attribute__((packed)) PAGENTRY_t;

/* TTBR0 and TTBR1 register format. */
typedef struct TTBR
{
  unsigned long RESV           :1;
  unsigned long ADDR           :47;
  unsigned long ASID           :16;
} __attribute__((packed)) TTBR_t;

/* TCR register format. */
typedef struct TCR
{
  unsigned long T0SZ           :6;
  unsigned long RESV0          :1;
  unsigned long EPD0           :1;
  unsigned long IRGN0          :2;
  unsigned long ORGN0          :2;
  unsigned long SH0            :2;
  unsigned long TG0            :2;
  unsigned long T1SZ           :6;
  unsigned long A1             :1;
  unsigned long EPD1           :1;
  unsigned long IRGN1          :2;
  unsigned long ORGN1          :2;
  unsigned long SH1            :2;
  unsigned long TG1            :2;
  unsigned long IPS            :3;
  unsigned long RESV1          :1;
  unsigned long AS             :1;
  unsigned long TBI0           :1;
  unsigned long TBI1           :1;
  unsigned long RESV2          :25;
} __attribute__((packed)) TCR_t;

/* SCTLR register format. */
typedef struct SCTLR
{
  unsigned long MMU            :1;
  unsigned long RESV           :63;
} __attribute__((packed)) SCTLR_t;

/*****************************************************************************
 *                           STATIC VARIABLES
 ****************************************************************************/

/* TTB0/TTB1 L0 page tables. */
static uint64_t PortTTB0[ENTRY_COUNT] TBL_ALIGN;
static uint64_t PortTTB1[ENTRY_COUNT] TBL_ALIGN;

/* TTB0 L1 page tables */
static uint64_t PortTTB0L1[TTB0_L1_COUNT][ENTRY_COUNT] TBL_ALIGN;

/*****************************************************************************
 *                         PortSetupTTB0()
 ****************************************************************************/

static void PortSetupTTB0 (void)
{
  /* Descriptors as integers. */
  uint64_t     invalidEntryValue = 0;
  uint64_t     tableEntryValue   = 0;
  uint64_t     blockEntryValue   = 0;

  /* Descriptors as structs. */
  INVENTRY_t  *invalidEntry      = NULL;
  TBLENTRY_t  *tableEntry        = NULL;
  BLKENTRY_t  *blockEntry        = NULL;

  /* Page tables. */
  uint64_t    *L0Table           = NULL;
  uint64_t    *L1Table           = NULL;

  /* Misc Variables. */
  uint64_t     curAddr           = 0;
  uint64_t     curL0Idx          = 0;
  uint64_t     curL1Idx          = 0;

  /* Setup pointers. */
  invalidEntry = (INVENTRY_t *) &invalidEntryValue;
  tableEntry   = (TBLENTRY_t *) &tableEntryValue;
  blockEntry   = (BLKENTRY_t *) &blockEntryValue;

  /* Setup invalid entry. */
  invalidEntry->VALID   = IS_INVALID;
  invalidEntry->IGNORED = 0;

  /* Setup table entry. */
  tableEntry->VALID     = IS_VALID;
  tableEntry->TYPE      = TYPE_TABLE;
  tableEntry->IGNORED0  = 0;
  tableEntry->ADDR      = 0;
  tableEntry->RESV      = 0;
  tableEntry->IGNORED1  = 0;
  tableEntry->PXN       = PXN_PERMIT_EXEC;
  tableEntry->UXN       = 0;
  tableEntry->AP        = AP_RW_NONE;
  tableEntry->NS        = NS_SECURE;

  /* Setup block entry. */
  blockEntry->VALID     = IS_VALID;
  blockEntry->TYPE      = TYPE_BLOCK;
  blockEntry->ATTRIDX   = 0;
  blockEntry->NS        = NS_SECURE;
  blockEntry->AP        = AP_RW_NONE;
  blockEntry->SH        = SH_INNER_SHAREABLE;
  blockEntry->AF        = AF_ACCESSABLE;
  blockEntry->NG        = NG_GLOBAL;
  blockEntry->RESV0     = 0;
  blockEntry->ADDR      = 0;
  blockEntry->RESV1     = 0;
  blockEntry->CONT      = CONT_ENABLE;
  blockEntry->PXN       = PXN_PERMIT_EXEC;
  blockEntry->XN        = 0;
  blockEntry->IGNORED   = 0;

  /* Obtain L0 table. */
  L0Table = PortTTB0;

  /* Loop over every gigabyte we want to setup. */
  do
  {
    /* If CurL1Idx is 512, reset to 0 to start a new L1 table. */
    curL1Idx %= ENTRY_COUNT;

    /* Beginning of a new L1 table? */
    if (curL1Idx == 0)
    {
      /* Allocate a new L1 table: */
      L1Table = PortTTB0L1[curL0Idx];

      /* Initialize corresponding entry in L0 Table: */
      tableEntry->ADDR  = TO_TBL_ADDR(L1Table);
      L0Table[curL0Idx] = tableEntryValue;

      /* Increase curL0Idx loop counter. */
      curL0Idx++;
    }

    /* Initialize corresponding entry in L1 table. */
    blockEntry->ADDR = TO_BLK_ADDR(curAddr);
    L1Table[curL1Idx] = blockEntryValue;

    /* Increase curL1Idx loop counter. */
    curL1Idx++;

    /* Next 1GB. */
    curAddr += 0x40000000;
  }
  while (curAddr != 0 && curAddr <= LAST_PHYSICAL_ADDR);

  /* Initialize remaining main page table entries in the last l1 table. */
  for (; curL1Idx < ENTRY_COUNT; curL1Idx++)
  {
    L1Table[curL1Idx] = invalidEntryValue;
  }

  /* Initialize remaining main page table entries in l0 table. */
  for (; curL0Idx < ENTRY_COUNT; curL0Idx++)
  {
    L0Table[curL0Idx] = invalidEntryValue;
  }

  /* Print table information. */
  KernelPrintFmt("TTB0 TABLE: %x\n", PortTTB0);
}

/*****************************************************************************
 *                           PortSetupTTB1()
 ****************************************************************************/

static void PortSetupTTB1 (void)
{
  /* Descriptors as integers. */
  uint64_t     invalidEntryValue = 0;

  /* Descriptors as structs. */
  INVENTRY_t  *invalidEntry      = NULL;

  /* Page tables. */
  uint64_t    *L0Table           = NULL;

  /* Misc Variables. */
  uint64_t     curL0Idx          = 0;

  /* Setup pointers. */
  invalidEntry = (INVENTRY_t *) &invalidEntryValue;

  /* Setup invalid entry. */
  invalidEntry->VALID   = 0;
  invalidEntry->IGNORED = 0;

  /* Obtain L0 table. */
  L0Table = PortTTB1;

  /* Initialize main page table entries. */
  for (curL0Idx = 0; curL0Idx < ENTRY_COUNT; curL0Idx++)
  {
    L0Table[curL0Idx] = invalidEntryValue;
  }

  /* Print table information. */
  KernelPrintFmt("TTB1 TABLE: %x\n", PortTTB1);
}

/*****************************************************************************
 *                          PortSetupTTBR0()
 ****************************************************************************/

static void PortSetupTTBR0 (void)
{
  /* Register as integer & struct. */
  uint64_t ttbr0Value   = 0;
  TTBR_t  *ttbr0Ptr     = NULL;

  /* Setup pointer. */
  ttbr0Ptr = (TTBR_t *) &ttbr0Value;

  /* Load old value. */
  MRS(ttbr0Value, TTBR0_EL1);

  /* Print old value in hex. */
  KernelPrintFmt("TTBR0_EL1:  %x", ttbr0Value);

  /* Initialize new value. */
  ttbr0Ptr->RESV = 0;
  ttbr0Ptr->ADDR = TO_TTB_ADDR(PortTTB0);
  ttbr0Ptr->ASID = 0;

  /* Print new value in hex. */
  KernelPrintFmt(" -> %x\n", ttbr0Value);

  /* Store new value. */
  MSR(TTBR0_EL1, ttbr0Value);

  /* Halt pipeline until MSR is completed. */
  ISB();
}

/*****************************************************************************
 *                           PortSetupTTBR1()
 ****************************************************************************/

static void PortSetupTTBR1 (void)
{
  /* Register as integer & struct. */
  uint64_t ttbr1Value   = 0;
  TTBR_t  *ttbr1Ptr     = NULL;

  /* Setup pointer. */
  ttbr1Ptr = (TTBR_t *) &ttbr1Value;

  /* Load old value. */
  MRS(ttbr1Value, TTBR1_EL1);

  /* Print old value in hex. */
  KernelPrintFmt("TTBR1_EL1:  %x", ttbr1Value);

  /* Initialize new value. */
  ttbr1Ptr->RESV = 0;
  ttbr1Ptr->ADDR = TO_TTB_ADDR(PortTTB1);
  ttbr1Ptr->ASID = 0;

  /* Print new value in hex. */
  KernelPrintFmt(" -> %x\n", ttbr1Value);

  /* Store new value. */
  MSR(TTBR1_EL1, ttbr1Value);

  /* Halt pipeline until MSR is completed. */
  ISB();
}

/*****************************************************************************
 *                            PortSetupTCR()
 ****************************************************************************/

static void PortSetupTCR (void)
{
  /* Register as integer & struct. */
  uint64_t tcrValue     = 0;
  TCR_t   *tcrPtr       = NULL;

  /* Setup pointer. */
  tcrPtr   = (TCR_t   *) &tcrValue;

  /* Load old value. */
  MRS(tcrValue, TCR_EL1);

  /* Print old value in hex. */
  KernelPrintFmt("TCR_EL1:    %x", tcrValue);

  /* Initialize new value. */
  tcrPtr->T0SZ  = TSZ_16_BITS;
  tcrPtr->EPD0  = EPD_WALK_ON_TLB_MISS;
  tcrPtr->IRGN0 = IRGN_WB_RA_WA;
  tcrPtr->ORGN0 = ORGN_WB_RA_WA;
  tcrPtr->SH0   = SH_INNER_SHAREABLE;
  tcrPtr->TG0   = TG_4KB;
  tcrPtr->T1SZ  = TSZ_16_BITS;
  tcrPtr->A1    = A_TTBR1_DEFINES_ASID;
  tcrPtr->EPD1  = EPD_WALK_ON_TLB_MISS;
  tcrPtr->IRGN1 = IRGN_WB_RA_WA;
  tcrPtr->ORGN1 = ORGN_WB_RA_WA;
  tcrPtr->SH1   = SH_INNER_SHAREABLE;
  tcrPtr->TG1   = TG_4KB;
  tcrPtr->IPS   = IPS_48_BITS;
  tcrPtr->RESV1 = 0;
  tcrPtr->AS    = AS_ASID_SIZE_16_BITS;
  tcrPtr->TBI0  = TBI_T0P_BYTE_USED;
  tcrPtr->TBI1  = TBI_T0P_BYTE_USED;
  tcrPtr->RESV2 = 0;

  /* Print new value in hex. */
  KernelPrintFmt(" -> %x\n", tcrValue);

  /* Store new value. */
  MSR(TCR_EL1, tcrValue);

  /* Halt pipeline until MSR is completed. */
  ISB();
}

/*****************************************************************************
 *                         PortSetupSCTLRPre()
 ****************************************************************************/

static void PortSetupSCTLRPre (void)
{
  /* Register as integer & struct. */
  uint64_t sctlrValue   = 0;
  SCTLR_t *sctlrPtr     = NULL;

  /* Setup pointer. */
  sctlrPtr = (SCTLR_t *) &sctlrValue;

  /* Load old value. */
  MRS(sctlrValue, SCTLR_EL1);

  /* Print old value in hex. */
  KernelPrintFmt("SCTLR_EL1:  %x", sctlrValue);

  /* Initialize new value. */
  sctlrPtr->MMU = MMU_DISABLE;

  /* Print new value in hex. */
  KernelPrintFmt(" -> %x\n", sctlrValue);

  /* Store new value. */
  MSR(SCTLR_EL1, sctlrValue);

  /* Halt pipeline until MSR is completed. */
  ISB();
  DSB(ishst);

  /* Flush TLB */
  TLBI(vmalle1is);

  /* Halt pipeline until TLBI is completed. */
  DSB(ish);
  ISB();
}

/*****************************************************************************
 *                         PortSetupSCTLRPost()
 ****************************************************************************/

static void PortSetupSCTLRPost (void)
{
  /* Register as integer & struct. */
  uint64_t sctlrValue   = 0;
  SCTLR_t *sctlrPtr     = NULL;

  /* Setup pointer. */
  sctlrPtr = (SCTLR_t *) &sctlrValue;

  /* Load old value. */
  MRS(sctlrValue, SCTLR_EL1);

  /* Print old value in hex. */
  KernelPrintFmt("SCTLR_EL1:  %x", sctlrValue);

  /* Initialize new value. */
  sctlrPtr->MMU = MMU_ENABLE;

  /* Print new value in hex. */
  KernelPrintFmt(" -> %x\n", sctlrValue);

  /* Store new value. */
  MSR(SCTLR_EL1, sctlrValue);

  /* Halt pipeline until MSR is completed. */
  ISB();
  DSB(ishst);

  /* Flush TLB */
  TLBI(vmalle1is);

  /* Halt pipeline until TLBI is completed. */
  DSB(ish);
  ISB();
}

/*****************************************************************************
 *                      PortTranslationInitialize()
 ****************************************************************************/

void PortTranslationInitialize (void)
{
  /* Allocate TTB0 and TTB1 tables. */
  PortSetupTTB0();
  PortSetupTTB1();

  /* Setup system registers. */
  PortSetupSCTLRPre();
  PortSetupTTBR0();
  PortSetupTTBR1();
  PortSetupTCR();
  PortSetupSCTLRPost();
}

/*****************************************************************************
 *                        PortTranslationSet()
 ****************************************************************************/

void *PortTranslationSet (void *virtualAddr, void *physicalAddr)
{
  /* Descriptors as integers. */
  uint64_t    invalidEntryValue   = 0;
  uint64_t    tableEntryValue     = 0;
  uint64_t    pageEntryValue      = 0;

  /* Descriptors as structs. */
  INVENTRY_t *invalidEntry        = NULL;
  TBLENTRY_t *tableEntry          = NULL;
  PAGENTRY_t *pageEntry           = NULL;

  /* Table entry numbers. */
  uint64_t    L0EntryNo           = 0;
  uint64_t    L1EntryNo           = 0;
  uint64_t    L2EntryNo           = 0;
  uint64_t    L3EntryNo           = 0;

  /* Table pointers. */
  uint64_t   *L0Table             = NULL;
  uint64_t   *L1Table             = NULL;
  uint64_t   *L2Table             = NULL;
  uint64_t   *L3Table             = NULL;

  /* Index counter for looping. */
  uint64_t    i                   = 0;

  /* Setup descriptor pointers. */
  invalidEntry = (INVENTRY_t *) &invalidEntryValue;
  tableEntry   = (TBLENTRY_t *) &tableEntryValue;
  pageEntry    = (PAGENTRY_t *) &pageEntryValue;

  /* Setup entry numbers. */
  L3EntryNo = (((uint64_t) virtualAddr) >> 12) & 0x1FF;
  L2EntryNo = (((uint64_t) virtualAddr) >> 21) & 0x1FF;
  L1EntryNo = (((uint64_t) virtualAddr) >> 30) & 0x1FF;
  L0EntryNo = (((uint64_t) virtualAddr) >> 39) & 0x1FF;

  /* Setup invalidEntry structure. */
  invalidEntry->VALID   = IS_INVALID;
  invalidEntry->IGNORED = 0;

  /* Start from L0Table (PortTTB1). */
  L0Table = PortTTB1;

  /* Read current descriptor at L0Table[L0EntryNo]. */
  tableEntryValue = L0Table[L0EntryNo];

  /* Descriptor is already in use? */
  if (tableEntry->VALID == 0)
  {
    /* Allocate new L1Table. */
    L1Table = KernelMemoryPageAllocate();

    /* Out of memory? */
    if (L1Table == NULL)
    {
      return NULL;
    }

    /* Clean up the new table. */
    for (i = 0; i < ENTRY_COUNT; i++)
    {
      L1Table[i] = invalidEntryValue;
    }

    /* Setup tableEntry. */
    tableEntry->VALID          = IS_VALID;
    tableEntry->TYPE           = TYPE_TABLE;
    tableEntry->IGNORED0       = 0;
    tableEntry->RESV           = 0;
    tableEntry->IGNORED1       = 0;
    tableEntry->PXN            = PXN_PERMIT_EXEC;
    tableEntry->UXN            = 0;
    tableEntry->ADDR           = TO_TBL_ADDR(L1Table);
    tableEntry->AP             = AP_RW_RW;
    tableEntry->NS             = NS_SECURE;

    /* Store the new entry. */
    L0Table[L0EntryNo] = tableEntryValue;
  }
  else
  {
    /* Use the current L1Table. */
    L1Table = FROM_TBL_ADDR(tableEntry->ADDR);
  }

  /* Read current descriptor at L1Table[L1EntryNo]. */
  tableEntryValue = L1Table[L1EntryNo];

  /* Descriptor is already in use? */
  if (tableEntry->VALID == 0)
  {
    /* Allocate new L2Table. */
    L2Table = KernelMemoryPageAllocate();

    /* Out of memory? */
    if (L2Table == NULL)
    {
      return NULL;
    }

    /* Clean up the new table. */
    for (i = 0; i < ENTRY_COUNT; i++)
    {
      L2Table[i] = invalidEntryValue;
    }

    /* Setup tableEntry. */
    tableEntry->VALID          = IS_VALID;
    tableEntry->TYPE           = TYPE_TABLE;
    tableEntry->IGNORED0       = 0;
    tableEntry->RESV           = 0;
    tableEntry->IGNORED1       = 0;
    tableEntry->PXN            = PXN_PERMIT_EXEC;
    tableEntry->UXN            = 0;
    tableEntry->ADDR           = TO_TBL_ADDR(L2Table);
    tableEntry->AP             = AP_RW_RW;
    tableEntry->NS             = NS_SECURE;

    /* Store the new entry. */
    L1Table[L1EntryNo] = tableEntryValue;

    /* Increase L1Table counter in L0Table */
    tableEntryValue = L0Table[L0EntryNo];
    tableEntry->IGNORED0++;
    L0Table[L0EntryNo] = tableEntryValue;
  }
  else
  {
    /* Use the current L2Table. */
    L2Table = FROM_TBL_ADDR(tableEntry->ADDR);
  }

  /* Read current descriptor at L2Table[L2EntryNo]. */
  tableEntryValue = L2Table[L2EntryNo];

  /* Descriptor is already in use? */
  if (tableEntry->VALID == 0)
  {
    /* Allocate new L3Table. */
    L3Table = KernelMemoryPageAllocate();

    /* Out of memory? */
    if (L3Table == NULL)
    {
      return NULL;
    }

    /* Clean up the new table. */
    for (i = 0; i < ENTRY_COUNT; i++)
    {
      L3Table[i] = invalidEntryValue;
    }

    /* Setup tableEntry. */
    tableEntry->VALID          = IS_VALID;
    tableEntry->TYPE           = TYPE_TABLE;
    tableEntry->IGNORED0       = 0;
    tableEntry->RESV           = 0;
    tableEntry->IGNORED1       = 0;
    tableEntry->PXN            = PXN_PERMIT_EXEC;
    tableEntry->UXN            = 0;
    tableEntry->ADDR           = TO_TBL_ADDR(L3Table);
    tableEntry->AP             = AP_RW_RW;
    tableEntry->NS             = NS_SECURE;

    /* Store the new entry. */
    L2Table[L2EntryNo] = tableEntryValue;

    /* Increase L2Table counter in L1Table. */
    tableEntryValue = L1Table[L1EntryNo];
    tableEntry->IGNORED0++;
    L1Table[L1EntryNo] = tableEntryValue;
  }
  else
  {
    /* Use the current L3Table. */
    L3Table = FROM_TBL_ADDR(tableEntry->ADDR);
  }

  /* Read current descriptor at L3Table[L3EntryNo]. */
  pageEntryValue = L3Table[L3EntryNo];

  /* Descriptor is already in use? */
  if (pageEntry->VALID == 0)
  {

    /* Setup pageEntry. */
    pageEntry->VALID           = IS_VALID;
    pageEntry->TYPE            = TYPE_PAGE;
    pageEntry->ATTRIDX         = 0;
    pageEntry->NS              = NS_SECURE;
    pageEntry->AP              = AP_RW_RW;
    pageEntry->SH              = SH_INNER_SHAREABLE;
    pageEntry->AF              = AF_ACCESSABLE;
    pageEntry->NG              = NG_NON_GLOBAL;
    pageEntry->RESV0           = 0;
    pageEntry->CONT            = CONT_DISABLE;
    pageEntry->PXN             = PXN_PERMIT_EXEC;
    pageEntry->UXN             = 0;
    pageEntry->ADDR            = TO_PAG_ADDR(physicalAddr);
    pageEntry->IGNORED         = 0;

    /* Store the new entry. */
    L3Table[L3EntryNo]         = pageEntryValue;

    /* Increase L3Table counter in L2Table. */
    tableEntryValue = L2Table[L2EntryNo];
    tableEntry->IGNORED0++;
    L2Table[L2EntryNo] = tableEntryValue;
  }
  else
  {
    /* The page is already mapped. */
    physicalAddr = FROM_PAG_ADDR(pageEntry->ADDR);
  }

  /* Done. */
  return physicalAddr;
}

/*****************************************************************************
 *                          PortTranslationGet()
 ****************************************************************************/

void *PortTranslationGet (void *virtualAddr)
{
  /* Descriptors as integers. */
  uint64_t    tableEntryValue   = 0;
  uint64_t    pageEntryValue    = 0;

  /* Descriptors as structs. */
  TBLENTRY_t *tableEntry        = NULL;
  PAGENTRY_t *pageEntry         = NULL;

  /* Table entry numbers. */
  uint64_t    L0EntryNo         = 0;
  uint64_t    L1EntryNo         = 0;
  uint64_t    L2EntryNo         = 0;
  uint64_t    L3EntryNo         = 0;

  /* Table pointers. */
  uint64_t   *L0Table           = NULL;
  uint64_t   *L1Table           = NULL;
  uint64_t   *L2Table           = NULL;
  uint64_t   *L3Table           = NULL;

  /* The mapped page that will be returned. */
  void       *physicalAddr      = NULL;

  /* Setup pointers. */
  tableEntry   = (TBLENTRY_t *) &tableEntryValue;
  pageEntry    = (PAGENTRY_t *) &pageEntryValue;

  /* Setup entry numbers. */
  L3EntryNo = (((uint64_t) virtualAddr) >> 12) & 0x1FF;
  L2EntryNo = (((uint64_t) virtualAddr) >> 21) & 0x1FF;
  L1EntryNo = (((uint64_t) virtualAddr) >> 30) & 0x1FF;
  L0EntryNo = (((uint64_t) virtualAddr) >> 39) & 0x1FF;

  /* Start from L0Table (PortTTB1). */
  L0Table = PortTTB1;

  /* Read current descriptor at L0Table[L0EntryNo]. */
  tableEntryValue = L0Table[L0EntryNo];

  /* L1Table doesn't even exist? */
  if(tableEntry->VALID == IS_INVALID)
  {
    /* Page is not mapped. */
    return NULL;
  }

  /* Obtain L1Table pointer. */
  L1Table = FROM_TBL_ADDR(tableEntry->ADDR);

  /* Read current descriptor at L1Table[L1EntryNo]. */
  tableEntryValue = L1Table[L1EntryNo];

  /* L2Table doesn't even exist? */
  if(tableEntry->VALID == IS_INVALID)
  {
    /* Page is not mapped. */
    return NULL;
  }

  /* Obtain L2Table pointer. */
  L2Table = FROM_TBL_ADDR(tableEntry->ADDR);

  /* Read current descriptor at L2Table[L2EntryNo]. */
  tableEntryValue = L2Table[L2EntryNo];

  /* L3Table doesn't even exist? */
  if(tableEntry->VALID == IS_INVALID)
  {
    /* Page is not mapped. */
    return NULL;
  }

  /* Obtain L3Table pointer. */
  L3Table = FROM_TBL_ADDR(tableEntry->ADDR);

  /* Read current descriptor at L3Table[L3EntryNo]. */
  pageEntryValue = L3Table[L3EntryNo];

  /* The mapping doesn't even exist? */
  if(pageEntry->VALID == IS_INVALID)
  {
    /* Page is not mapped. */
    return NULL;
  }

  /* Load the physical address of the mapped page. */
  physicalAddr = FROM_PAG_ADDR(pageEntry->ADDR);

  /* Done. */
  return physicalAddr;
}

/*****************************************************************************
 *                          PortTranslationDel()
 ****************************************************************************/

void *PortTranslationDel (void *virtualAddr)
{
  /* Descriptors as integers. */
  uint64_t    invalidEntryValue = 0;
  uint64_t    tableEntryValue   = 0;
  uint64_t    pageEntryValue    = 0;

  /* Descriptors as structs. */
  INVENTRY_t *invalidEntry      = NULL;
  TBLENTRY_t *tableEntry        = NULL;
  PAGENTRY_t *pageEntry         = NULL;

  /* Table entry numbers. */
  uint64_t    L0EntryNo         = 0;
  uint64_t    L1EntryNo         = 0;
  uint64_t    L2EntryNo         = 0;
  uint64_t    L3EntryNo         = 0;

  /* Table pointers. */
  uint64_t   *L0Table           = NULL;
  uint64_t   *L1Table           = NULL;
  uint64_t   *L2Table           = NULL;
  uint64_t   *L3Table           = NULL;

  /* The mapped page that will be removed. */
  void       *physicalAddr      = NULL;

  /* Setup pointers. */
  invalidEntry = (INVENTRY_t *) &invalidEntryValue;
  tableEntry   = (TBLENTRY_t *) &tableEntryValue;
  pageEntry    = (PAGENTRY_t *) &pageEntryValue;

  /* Setup entry numbers. */
  L3EntryNo = (((uint64_t) virtualAddr) >> 12) & 0x1FF;
  L2EntryNo = (((uint64_t) virtualAddr) >> 21) & 0x1FF;
  L1EntryNo = (((uint64_t) virtualAddr) >> 30) & 0x1FF;
  L0EntryNo = (((uint64_t) virtualAddr) >> 39) & 0x1FF;

  /* Setup invalidEntry pointer. */
  invalidEntry->VALID   = IS_INVALID;
  invalidEntry->IGNORED = 0;

  /* Start from L0Table (PortTTB1). */
  L0Table = PortTTB1;

  /* Read current descriptor at L0Table[L0EntryNo]. */
  tableEntryValue = L0Table[L0EntryNo];

  /* L1Table doesn't even exist? */
  if(tableEntry->VALID == IS_INVALID)
  {
    /* Page is not mapped. */
    return NULL;
  }

  /* Obtain L1Table pointer. */
  L1Table = FROM_TBL_ADDR(tableEntry->ADDR);

  /* Read current descriptor at L1Table[L1EntryNo]. */
  tableEntryValue = L1Table[L1EntryNo];

  /* L2Table doesn't even exist? */
  if(tableEntry->VALID == IS_INVALID)
  {
    /* Page is not mapped. */
    return NULL;
  }

  /* Obtain L2Table pointer. */
  L2Table = FROM_TBL_ADDR(tableEntry->ADDR);

  /* Read current descriptor at L2Table[L2EntryNo]. */
  tableEntryValue = L2Table[L2EntryNo];

  /* L3Table doesn't even exist? */
  if(tableEntry->VALID == IS_INVALID)
  {
    /* Page is not mapped. */
    return NULL;
  }

  /* Obtain L3Table pointer. */
  L3Table = FROM_TBL_ADDR(tableEntry->ADDR);

  /* Read current descriptor at L3Table[L3EntryNo]. */
  pageEntryValue = L3Table[L3EntryNo];

  /* The mapping doesn't even exist? */
  if(pageEntry->VALID == IS_INVALID)
  {
    /* Page is not mapped. */
    return NULL;
  }

  /* Load the physical address of the mapped page. */
  physicalAddr = FROM_PAG_ADDR(pageEntry->ADDR);

  /* Mark the descriptor in L3Table as invalid. */
  L3Table[L3EntryNo]    = invalidEntryValue;

  /* Decrease L3Table counter in L2Table. */
  tableEntryValue = L2Table[L2EntryNo];
  tableEntry->IGNORED0--;
  L2Table[L2EntryNo] = tableEntryValue;

  /* Is L3Table still has entries? */
  if (tableEntry->IGNORED0 != 0)
  {
    /* Done. */
    return physicalAddr;
  }

  /* Deallocate L3Table. */
  KernelMemoryPageDeallocate(L3Table);

  /* Mark the descriptor in L2Table as invalid. */
  L2Table[L2EntryNo] = invalidEntryValue;

  /* Decrease L2Table counter in L1Table. */
  tableEntryValue = L1Table[L1EntryNo];
  tableEntry->IGNORED0--;
  L1Table[L1EntryNo] = tableEntryValue;

  /* L2Table still has entries? */
  if (tableEntry->IGNORED0 != 0)
  {
    /* Done. */
    return physicalAddr;
  }

  /* Deallocate L2Table. */
  KernelMemoryPageDeallocate(L2Table);

  /* Mark the descriptor in L1Table as invalid. */
  L1Table[L1EntryNo] = invalidEntryValue;

  /* Decrease L1Table counter in L0Table. */
  tableEntryValue = L0Table[L0EntryNo];
  tableEntry->IGNORED0 --;
  L0Table[L0EntryNo] = tableEntryValue;

  /* L1Table still has entries? */
  if (tableEntry->IGNORED0 != 0)
  {
    /* Done. */
    return physicalAddr;
  }

  /* Deallocate L1Table. */
  KernelMemoryPageDeallocate(L1Table);

  /* Mark the descriptor in L0Table as invalid. */
  L0Table[L0EntryNo] = invalidEntryValue;

  /* Done. */
  return physicalAddr;
}
