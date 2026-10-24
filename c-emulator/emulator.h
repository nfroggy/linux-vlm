/* -*- Mode:C -*- */

/**** Language supplements ****/

#ifndef _EMULATOR_H
#define _EMULATOR_H

#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef unsigned char Byte;
typedef unsigned char Tag;
typedef uint32_t Integer;
typedef int Boolean;
typedef float Float;
typedef void *Pointer;
#define False 0
#define True 1

static inline uint32_t CMask(unsigned width)
{ return width >= 32 ? UINT32_MAX : (UINT32_C(1) << width) - 1; }
#define ldb(ss,pp,source) ((uint32_t)(source) >> (pp) & CMask(ss))
#define dpb(field,ss,pp,background) (((uint32_t)(background) & ~(CMask(ss) << (pp))) | (((uint32_t)(field) & CMask(ss)) << (pp)))
#define ceiling(n,d) (((n) + ((d) - 1)) / (d))
#define SignExtend8(i) ((int32_t)((uint32_t)(i) << 24) >> 24)
#define SignExtend10(i) ((int32_t)((uint32_t)(i) << 22) >> 22)

/* Same data/tag layout as the host LispObj, with typed data accessors. */
typedef union {
  struct {
    union { uint32_t u; int32_t s; float f; } data;
    uint32_t tag;
  } parts;
  uint64_t whole;
} CObject, CPC;
#define DATA parts.data
#define TAG parts.tag
#define COBJECT(tag_, data_) { .parts = { .data = { .u = (uint32_t)(data_) }, .tag = (tag_) } }

typedef struct _InstructionCacheLine
{
  CPC pc;
  CPC next_pc;
  int code;
  int operand;
  unsigned int instruction;
  struct _InstructionCacheLine *next_cp;
} InstructionCacheLine;

#define InstructionCacheSize 2048
#define InstructionCacheLineSize 64

/**** Processor state definitions ****/

#define PageSize 0x100
#define PageNumberMask 0xffffff00
#define PageOffsetMask 0xff
#define AddressPageShift 8

#define QuantumSize 0x100000
#define AddressQuantumShift 20

#define AddressQuantumNumber(vma) ((vma) >> AddressQuantumShift)
#define AddressQuantumOffset(vma) (((vma) & (QuantumSize - 1)) >> AddressPageShift)
#define AddressPageNumber(vma) ((vma) >> AddressPageShift)
#define AddressPageOffset(vma) ((vma) & (PageSize - 1))

#define StackCacheSize 4

typedef struct _ProcessorState
{
  CObject *sp;
  CObject *restartsp;
  CObject *fp;
  CObject *lp;
  CPC pc;
  CPC continuation;
  InstructionCacheLine *InstructionCache;
  CObject *StackCache;
  CObject *StackCacheLimit;
  struct _bar {
	        CObject address;
		CObject *mapped;
	      } bar[4];
  CObject ListCacheArea;
  CObject ListCacheAddress;
  CObject StructureCacheArea;
  CObject StructureCacheAddress;
  CObject CatchBlockPointer;
/* Integer fields are at the end for better alignment */
  Integer control;
  Integer StackCacheBase;
  Integer ArrayEventCount;
  Integer ListCacheLength;
  Integer StructureCacheLength;
  Integer BindingStackPointer;
  Integer BindingStackLimit;
  Boolean DeepBoundP;
  Integer PreemptRegister;
  Integer AluAndRotateControl;
  Integer (*AluOp)(Integer, Integer, Integer);
  Integer ByteSize;
  Integer ByteRotate;
  Integer RotateLatch;
  Boolean ALUOverflow;
  Boolean ALUBorrow;
  Boolean ALULessThan;
  Integer EphemeralOldspaceRegister;
  Integer ZoneOldspaceRegister;
  Integer ControlStackLimit;
  Integer ControlStackExtraLimit;
  Integer DynamicBindingCacheBase;
  Integer DynamicBindingCacheMask;
  Integer FEPModeTrapVectorAddress;
  Integer ChoicePointer;
  Integer StructureStackChoicePointer;
  Integer StackCacheOverflowLimit;
  Integer MappingTableCache;
  Integer MappingTableLength;
  Boolean running;
  uint64_t instruction_count;
  unsigned StackCacheWords;
  int halt_reason;
} CProcessorState;

extern CProcessorState *c_processor;
extern Boolean Trace;

int CInstructionSequencer(void);
void CFlushInstructionCache(void);
void CImportState(void);
void CExportState(void);
int CPollInterrupt(void);
void CRequestPreemptCheck(void);
CObject CCoprocessorRead(unsigned reg);
int CCoprocessorWrite(unsigned reg, CObject value);
void StackCacheScrollDown(void);
void StackCacheScrollUp(void);
void EnsureStackCacheSpace(unsigned words);
int CheckControlStack(int post_trap);
int TakePostTrap(int index, int arity, CObject *nextpc);
int CWriteVirtualMemoryBlock(Integer vma, CObject *object, int count);
int CReadVirtualMemoryBlock(Integer vma, CObject *object, int count);
int CWriteVirtualMemory(Integer vma, CObject *object);
int CReadVirtualMemory(Integer vma, CObject *object);
void TakeMemoryTrap(int vector, Integer vma);
void TakeIllegalOperandTrap(int microstate, CObject *operand);
void TakeInstructionExceptionTrap(void);
int TakePreTrap(Integer index, CObject *extra1, CObject *extra2);
int TakeInstructionException(int instruction, CObject *op2, CObject *nextpc);
extern Integer (*ALUFunctionClass[4])(Integer, Integer, Integer);

typedef enum _MemoryCycleTypes
{
  CycleDataRead,
  CycleDataWrite,
  CycleBindRead,
  CycleBindWrite,
  CycleBindReadNoMonitor,
  CycleBindWriteNoMonitor,
  CycleHeader,
  CycleStructureOffset,
  CycleScavenge,
  CycleCdr,
  CycleGCCopy,
  CycleRaw,
  CycleRawTranslate
} MemoryCycleTypes;

#define MemoryActionIndirect 01
#define MemoryActionMonitor 02
#define MemoryActionTransport 04
#define MemoryActionTrap 010
#define MemoryActionTransform 020
#define MemoryActionBinding 040

extern Byte CMemoryActionTable[13][64];
extern Integer MemoryReadInternal (Integer vma, CObject *object, Byte row[]);
extern int StoreContentsInternal (Integer vma, CObject *object, Byte row[]);

#define MemoryRead(vma, object, cycle) MemoryReadInternal(vma, object, CMemoryActionTable[cycle])
#define MemoryReadData(vma, object) MemoryReadInternal(vma, object, CMemoryActionTable[CycleDataRead])
#define MemoryReadHeader(vma, object) MemoryReadInternal(vma, object, CMemoryActionTable[CycleHeader])
#define MemoryReadCdr(vma, object) MemoryReadInternal(vma, object, CMemoryActionTable[CycleCdr])
#define StoreContents(vma, object, cycle) StoreContentsInternal(vma, object, CMemoryActionTable[cycle])

#endif
